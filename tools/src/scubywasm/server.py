import argparse
import concurrent.futures as cf
import json
import pathlib
import random
import re
import signal

from .game import Game


class Logger:
    def __init__(self, log_dir, *, verbose):
        self._verbose = verbose

        self._log_dir = log_dir
        log_dir.mkdir(parents=True, exist_ok=True)

        pattern = re.compile(r"^scubywasm-log_(\d+)\.json$")
        logs = [p for p in log_dir.glob("*.json") if pattern.match(p.name)]
        largest = max(
            logs, key=lambda p: int(pattern.match(p.name).group(1)), default=None
        )
        self._idx = (
            0 if largest is None else int(pattern.match(largest.name).group(1)) + 1
        )

    def save_log(self, log):
        log_file = self._log_dir / f"scubywasm-log_{self._idx}.json"
        with log_file.open("w") as f:
            json.dump(log, f)
            f.write("\n")

        self._idx += 1

        if self._verbose:
            print(f"Saved game log to {log_file!s}")


def _run_game(*, engine_wasm, agents_dir, agent_multiplicity, seed, max_ticks):
    agent_wasmfiles = []
    pattern = re.compile(r"^agent-v(\d+)\.wasm$")
    for d in agents_dir.iterdir():
        if d.is_dir():
            matches = []
            for p in d.glob("*.wasm"):
                m = pattern.match(p.name)
                if m:
                    matches.append((int(m.group(1)), p))

            if matches:
                agent_wasmfiles.append(max(matches)[1])

    agent_wasmfiles.sort(key=lambda p: p.parent.name)
    if not agent_wasmfiles:
        return dict()

    agent_wasms = [file_name.read_bytes() for file_name in agent_wasmfiles]
    game = Game(
        engine_wasm, agent_wasms, seed=seed, agent_multiplicity=agent_multiplicity
    )

    for _ in range(max_ticks):
        n_teams_alive = game.tick()
        if n_teams_alive <= 1:
            break

    log = game.log["history"]
    final_scores = [team["scores"][-1] for team in log]
    teams = [f"{file.parent.name}/{file.stem}" for file in agent_wasmfiles]

    return dict(teams=teams, final_scores=final_scores) | game.log


def _ignore_sigint():
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)


def _new_seed(rng):
    return rng.getrandbits(32)


def main():
    max_ticks = 1_000
    log_dir = pathlib.Path.cwd()

    parser = argparse.ArgumentParser(
        prog="scubywasm-server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=(
            "run continuous Scubywasm matches in parallel and persist each finished "
            "game as a JSON log.\n\n"
            "what it does\n"
            "  starts --workers independent simulations (one per process) and "
            "continuously schedules new games.\n"
            "  Each finished game is written to disk as a JSON log compatible with "
            "scubywasm-display.\n\n"
            "required inputs\n"
            "  ENGINE_WASM  path to the engine wasm module.\n"
            "  AGENTS_DIR   directory containing one subdirectory per team.\n"
            "\n"
            "required agents_dir layout\n"
            "  AGENTS_DIR/<team_name>/agent-v<version>.wasm\n"
            "\n"
            "  <version> must be an integer (e.g. agent-v0.wasm, agent-v12.wasm).\n"
            "  per team directory, the server selects exactly one wasm file: the "
            "highest version present.\n"
            "  teams are ordered by <team_name>.\n\n"
            "log output naming\n"
            "  logs are written to LOG_DIR as scubywasm-log_<n>.json.\n"
            "  <n> is incremented automatically based on existing scubywasm-log_*.json "
            "files.\n"
        ),
    )
    parser.add_argument(
        "engine_wasmfile",
        type=pathlib.Path,
        metavar="ENGINE_WASM",
        help="path to the engine wasm module",
    )
    parser.add_argument(
        "agents_dir",
        type=pathlib.Path,
        metavar="AGENTS_DIR",
        help=(
            "path to agents directory; must contain team subdirectories with files "
            "named agent-v<version>.wasm"
        ),
    )
    parser.add_argument(
        "--workers",
        default=1,
        type=int,
        metavar="N",
        help="number of worker processes / concurrent simulations (default: 1)",
    )
    parser.add_argument(
        "--seed",
        default=42,
        type=int,
        metavar="SEED",
        help="seed for the server-side RNG that generates per-game seeds",
    )
    parser.add_argument(
        "--multiplicity",
        default=1,
        type=int,
        metavar="N",
        help="number of ships per team, aka agent multiplicity (default: 1)",
    )
    parser.add_argument(
        "--max_ticks",
        default=max_ticks,
        type=int,
        metavar="TICKS",
        help=f"max. number of ticks per game (default: {max_ticks})",
    )
    parser.add_argument(
        "--fuel_limit",
        default=None,
        type=int,
        metavar="FUEL",
        help="optional fuel limit for agent calls; unset disables fuel metering",
    )
    parser.add_argument(
        "--log_dir",
        default=log_dir,
        type=pathlib.Path,
        metavar="LOG_DIR",
        help=(
            f"directory to save logs as scubywasm-log_<n>.json (default: {log_dir!s})"
        ),
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="enable verbose logging",
    )
    args = parser.parse_args()

    if not args.engine_wasmfile.is_file():
        parser.error(f"Couldn't open engine WASM file {args.engine_wasmfile!s}")

    if not args.agents_dir.is_dir():
        parser.error(f"{args.agents_dir!s} is not an existing directory")

    if args.workers < 1:
        parser.error(f"--workers must be >= 1 (got {args.workers})")

    if args.multiplicity < 1:
        parser.error(f"--multiplicity must be >= 1 (got {args.multiplicity})")

    if args.fuel_limit < 100:
        parser.error(f"--multiplicity must be >= 100 (got {args.fuel_limit})")

    if args.max_ticks < 1:
        parser.error(f"--max_ticks must be >= 1 (got {args.max_ticks})")

    rng = random.Random(args.seed)

    kwargs = dict(
        engine_wasm=args.engine_wasmfile.read_bytes(),
        agents_dir=args.agents_dir,
        agent_multiplicity=args.multiplicity,
        max_ticks=args.max_ticks,
        agent_fuel_limit=args.fuel_limit,
    )

    logger = Logger(args.log_dir, verbose=args.verbose)

    stopping = False

    def _on_sigint(signum, frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, _on_sigint)
    signal.signal(signal.SIGTERM, _on_sigint)

    with cf.ProcessPoolExecutor(
        max_workers=args.workers, initializer=_ignore_sigint
    ) as ex:
        futures = set()
        for _ in range(args.workers):
            futures.add(ex.submit(_run_game, **kwargs | {"seed": _new_seed(rng)}))

        while futures:
            done, pending = cf.wait(
                futures,
                return_when=cf.FIRST_COMPLETED,
            )

            futures = pending
            for future in done:
                try:
                    res = future.result()
                    if res:
                        logger.save_log(res)
                    else:
                        print("Warning: worker didn't find any agents!")
                except Exception as e:
                    print(f"Worker failed: {e!r}")

                if not stopping:
                    futures.add(
                        ex.submit(_run_game, **kwargs | {"seed": _new_seed(rng)})
                    )


if __name__ == "__main__":
    main()
