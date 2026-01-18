# Scubywasm C APIs

This documentation covers the C-level APIs used by Scubywasm:

- the **agent ABI** that a WebAssembly agent module must export, and
- the **engine API** used by hosts/tools to run rounds and query game state.

Game rules and project overview are documented in [README.md](https://github.com/avitase/scubywasm).

## Start here

- **Writing a bot/agent (WASM module):** [scubywasm_agent.h](@ref scubywasm_agent_api)
- **Embedding / driving the simulation (host or tooling):** [engine.h](@ref scubywasm_engine_api)

## Agent ABI (WASM modules)

This ABI describes what an agent module must export so it can be driven by the host.
Although the ABI is expressed in C (for precise types and calling conventions), agents do not have to be authored in C.
You may write an agent in any language that can be compiled to WASM, as long as the resulting module exports the required entry points with compatible signatures and semantics.

### What the header provides

The header documentation is the single source of truth for:

- The required exported functions (the full API surface).
- Parameter and data type semantics.
- The host-driven call pattern (initialization, per-tick updates, action queries, shutdown).

If you implement the exported functions with the documented behavior, your WASM module is a valid Scubywasm agent.

### Minimal example implementation

\anchor scubywasm_freestanding_agent_example

\verbinclude agents/freestanding_agent.c

## Engine API (hosts and tools)

The engine API is used to run a round: create a context, add ships, process per-tick actions, advance time, and observe state (poses, shots, alive status, score).

The `engine.h` documentation also explains the `FREESTANDING` build mode and the singleton argument buffers (e.g., `get_pose_buffer()`, `get_config_buffer()`) that exist primarily for WASM/freestanding usage.

- **API reference:** [engine.h](@ref scubywasm_engine_api)


