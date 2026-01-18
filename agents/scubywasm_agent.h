#ifndef SCUBYWASM_AGENT_H
#define SCUBYWASM_AGENT_H

/**
 * \file scubywasm_agent.h
 * \anchor scubywasm_agent_api
 * \brief Scubywasm agent ABI.
 *
 * Scubywasm executes user-provided bots ("agents") as WebAssembly (WASM)
 * modules. The host interacts with an agent module exclusively through the
 * functions declared in this header.
 *
 * **Teams and per-ship agent IDs**
 *
 * A single WASM module controls a *team* with \c agent_multiplicity members.
 * Each team member controls exactly one ship, and is identified by a 32-bit
 * \c agent_id. For each \c agent_id there is exactly one ship, and at most one
 * active shot at a time. Consequently, ships and shots are referred to by their
 * respective \c agent_id (there are no separate ship/shot IDs in this ABI).
 *
 * **Agent IDs are opaque 32-bit identifiers**
 *
 * The host provides \c agent_id values as 32-bit identifiers. They are neither
 * required to be zero-based nor sequential and the agent must not assume any
 * particular numbering scheme. If the agent implementation requires an
 * internal indexing scheme, it must build and maintain it explicitly.
 *
 * **Opaque context handle (no global state)**
 *
 * ::init_agent() returns a pointer to an implementation-defined \c Context.
 * For the host, this pointer is an opaque handle that must only be passed back
 * to subsequent API calls and never dereferenced.
 *
 * The \c Context is the place to store all agent state. In particular:
 *  - Persistent bot state (e.g., strategy, per-ship memory) must live in
 *    \c Context.
 *  - The same WASM module may be used to play multiple games concurrently; the
 *    host distinguishes such instances by the \c Context pointer.
 *
 * Therefore, agent implementations should avoid module-level global mutable
 * state.
 *
 * **Required exported functions**

 * A valid agent WASM module must export and implement these entry points (C
 * ABI). All other declarations in this header exist to document the semantics
 * of these functions and their parameter types.
 *  - ::init_agent()            Create a new per-round \c Context.
 *  - ::set_config_parameter()  Receive immutable engine configuration.
 *  - ::clear_world_state()     Begin a new per-tick observation frame.
 *  - ::update_ship()           Receive one ship state (per \c agent_id).
 *  - ::update_shot()           Receive one shot state (per \c agent_id).
 *  - ::update_score()          Receive one score value (per \c agent_id).
 *  - ::make_action()           Decide actions for one controlled \c agent_id.
 *  - ::free_context()          Destroy \c Context and release resources.
 *
 * **Typical call pattern**
 *
 * The host drives an agent instance in three phases:
 *
 * \b 1) Initialization (once per round)
 *  - The host creates a new agent instance by calling ::init_agent() and keeps
 *    the returned \c Context* as an opaque handle.
 *  - The host then provides the immutable engine configuration by calling
 *    ::set_config_parameter() exactly once for each relevant ::ConfigParameter.
 *    Agents should cache these values in \c Context for later planning.
 *
 * \b 2) Per-tick update and decision (repeated for each tick)
 *  - At the beginning of each tick, the host starts a fresh observation frame
 *    by calling ::clear_world_state(). (If fuel metering is enabled, the host
 *    may also reset the execution budget for this tick before making further
 *    calls.)
 *  - The host then streams a snapshot of the current world state to the agent
 *    by calling:
 *      - ::update_ship() for ships (identified by \c agent_id),
 *      - ::update_shot() for shots (identified by the owning \c agent_id; a
 *        \c lifetime of 0 indicates an inactive shot), and
 *      - ::update_score() for scores (per \c agent_id).
 *
 *    In a typical setup, the host broadcasts the same complete snapshot to
 *    every agent module, so each agent can observe all teams, not only itself.
 *    Agents must not assume a particular ordering of the \c update_* calls;
 *    they should treat them as an unordered stream that fully describes the
 *    current tick's state between ::clear_world_state() and the subsequent
 *    action queries.
 *
 *  - After the snapshot has been provided, the host requests actions for this
 *    team by calling ::make_action() once for each team member controlled by
 *    this WASM module. The host applies the returned ::ActionFlags bitmask to
 *    the engine for that \c agent_id.
 *
 *    The host will not call ::make_action() for dead ships.
 *
 *  - Once actions for all teams have been collected, the host advances the
 *    simulation by one (or more) engine ticks (e.g., \c engine.tick()) and then
 *    repeats the process for the next tick.
 *
 * \b 3) Shutdown (once per round)
 *  - When the round ends, the host calls ::free_context().
 *
 * **Discovering the team's agent IDs**
 *
 * The agent does not receive an explicit list of the \c agent_id values it
 * controls. Instead, the host calls ::make_action() once per tick for each
 * \c agent_id that belongs to the team controlled by this WASM module. Agents
 * that need a stable roster must infer and maintain the set of controlled
 * \c agent_id values from these calls (e.g., by recording each \c agent_id
 * observed in ::make_action()).
 *
 * **Fuel metering and unresponsive agents**
 *
 * All agent interactions within a tick (including calls to
 * ::clear_world_state(), ::update_ship(), ::update_shot(), ::update_score(),
 * and ::make_action()) are metered in units of wasmtime fuel. Before each tick,
 * the host refuels the agent instance to a fixed budget; the agent must not
 * exceed this budget over the tick. If the fuel is exhausted during a tick, the
 * agent becomes unresponsive and the host will stop calling ::make_action()
 * for that agent for the remainder of the round.
 *
 * **Coordinate conventions**
 *
 * The implicit coordinate conventions are:
 *  - \c x and \c y live on a unit torus with values in [0, 1).
 *  - \c heading is in degrees in [0, 360) with:
 *      - 0 deg = up,
 *      - 90 deg = right,
 *      - 180 deg = down,
 *      - 270 deg = left.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * \brief Action bitmask.
 *
 * The host interprets the return value of ::make_action() as a bitwise OR of
 * these flags. Unless explicitly stated otherwise, flags are combinable.
 *
 * Dynamics (turn rate, max velocity, shot velocity, lifetimes, etc.) are
 * defined by the current configuration as provided via ::set_config_parameter()
 * using ::ConfigParameter.
 *
 * \c ACTION_TURN_LEFT and \c ACTION_TURN_RIGHT are logically mutually
 * exclusive. If an agent sets both, the host may ignore both, pick one
 * deterministically, or apply a host-defined tie-breaker.
 */
enum ActionFlags : unsigned int
{
    /** Do nothing this tick. */
    ACTION_NONE = 0U,

    /**
     * Enable thrust for this tick.
     *
     * Ship speed is binary: either zero or the configured maximum.
     * If \c ACTION_THRUST is set, the ship's velocity is set to
     * \c CFG_SHIP_MAX_VELOCITY. If \c ACTION_THRUST is not set, the ship's
     * velocity is set to zero.
     */
    ACTION_THRUST = 1U,

    /**
     * Turn left for this tick.
     *
     * Turning is binary: if \c ACTION_TURN_LEFT is set, the ship's heading is
     * changed by \c CFG_SHIP_MAX_TURN_RATE degrees (left) for this tick. If it
     * is not set, no left turn is applied.
     */
    ACTION_TURN_LEFT = 2U,

    /**
     * Turn right for this tick.
     *
     * Turning is binary: if \c ACTION_TURN_RIGHT is set, the ship's heading is
     * changed by \c CFG_SHIP_MAX_TURN_RATE degrees (right) for this tick. If it
     * is not set, no right turn is applied.
     */
    ACTION_TURN_RIGHT = 4U,

    /**
     * Fire a shot.
     *
     * Shots travel with velocity \c CFG_SHOT_VELOCITY and expire after
     * \c CFG_SHOT_LIFETIME ticks. Each \c agent_id may have at most one active
     * shot at a time; if a shot is already active, firing is ignored.
     */
    ACTION_FIRE = 8U,
};

/**
 * \brief Engine configuration parameters.
 *
 * These parameters define the relevant game dynamics and constraints that
 * agents should use for planning (movement, turning, shooting, collision
 * avoidance, etc.).
 *
 * Configuration parameters are set exactly once during initialization (after
 * ::init_agent() and before the first tick). They are never changed thereafter.
 * Each round uses a fresh WASM instance, so agents must not rely on
 * configuration carrying over between rounds.
 */
enum ConfigParameter : unsigned int
{
    /**
     * Ship turn rate per tick (in degrees per tick).
     *
     * If \c ACTION_TURN_LEFT or \c ACTION_TURN_RIGHT is set, the ship's
     * heading is changed by this value.
     */
    CFG_SHIP_MAX_TURN_RATE = 0U,

    /**
     * Ship speed when thrust is enabled (in torus-units per tick).
     *
     * If \c ACTION_THRUST is set, the ship's velocity is set to this value. If
     * \c ACTION_THRUST is not set, the ship's velocity is zero.
     */
    CFG_SHIP_MAX_VELOCITY = 1U,

    /**
     * Ship hit radius (in torus-units).
     *
     * Ships are considered colliding/touching when their distance satisfies the
     * engine's collision criterion derived from this radius.
     */
    CFG_SHIP_HIT_RADIUS = 2U,

    /**
     * Shot velocity (in torus-units per tick).
     *
     * Determines how far a shot advances per tick after \c ACTION_FIRE
     * succeeds.
     */
    CFG_SHOT_VELOCITY = 3U,

    /**
     * Shot lifetime / end-of-life (in ticks).
     *
     * A shot is removed when its lifetime reaches zero.
     */
    CFG_SHOT_LIFETIME = 4U,
};

/**
 * \brief Opaque per-instance agent context.
 *
 * The agent defines and owns \c Context. The host must treat pointers to
 * \c Context as opaque handles (never dereference them) and pass them back
 * unchanged to subsequent calls.
 *
 * All mutable agent state should be stored in \c Context (no module-level
 * global state), enabling multiple concurrent game instances via distinct
 * \c Context pointers.
 */
struct Context;

/**
 * \brief Create a new per-round agent context.
 *
 * \param n_agents Total number of agents in the world (across all teams).
 * \param agent_multiplicity Number of team members.
 * \param seed 32 random bits that can be used to seed PRNGs.
 *
 * \return A new \c Context instance, or \c NULL on failure.
 */
struct Context *
init_agent(uint32_t n_agents, uint32_t agent_multiplicity, uint32_t seed);

/**
 * \brief Destroy an agent context created by ::init_agent().
 *
 * Releases all resources owned by \p ctx. The host will not use \p ctx after
 * this call.
 *
 * \param ctx Context pointer returned by ::init_agent().
 */
void free_context(struct Context *ctx);

/**
 * \brief Set an immutable configuration parameter.
 *
 * Called exactly once per parameter during initialization, before the first
 * tick.
 *
 * \param ctx Context pointer returned by ::init_agent().
 * \param param Parameter to set.
 * \param value Parameter value (units implied by \p param).
 */
void set_config_parameter(struct Context *ctx,
                          enum ConfigParameter param,
                          float value);

/**
 * \brief Clear all observations for the next tick.
 *
 * Called at the beginning of each tick, before any \c update_* calls.
 *
 * \param ctx Context pointer returned by ::init_agent().
 */
void clear_world_state(struct Context *ctx);

/**
 * \brief Provide the current state of a ship.
 *
 * Called once per ship per tick to stream the full world state.
 *
 * \param ctx Context pointer returned by ::init_agent().
 * \param agent_id 32-bit ID of the ship (and its controlling agent).
 * \param hp Ship "health": \c 1 if alive, \c 0 if not alive.
 * \param x Ship x-position on the unit torus.
 * \param y Ship y-position on the unit torus.
 * \param heading Ship heading in degrees.
 */
void update_ship(struct Context *ctx,
                 uint32_t agent_id,
                 int32_t hp,
                 float x,
                 float y,
                 float heading);

/**
 * \brief Provide the current state of a shot.
 *
 * Called once per (active) shot per tick to stream the full world state.
 *
 * Whether the host calls this function for dead shots (i.e., \c lifetime == 0)
 * is engine-defined.
 *
 * \param ctx Context pointer returned by ::init_agent().
 * \param agent_id 32-bit id of the shot owner (and associated ship).
 * \param lifetime Remaining lifetime in ticks. A value of \c 0 indicates that
 *        the shot is no longer active.
 * \param x Shot x-position on the unit torus.
 * \param y Shot y-position on the unit torus.
 * \param heading Shot heading in degrees.
 */
void update_shot(struct Context *ctx,
                 uint32_t agent_id,
                 int32_t lifetime,
                 float x,
                 float y,
                 float heading);

/**
 * \brief Provide the current score for one agent.
 *
 * Called once per agent per tick to stream the scores.
 *
 * \param ctx Context pointer returned by ::init_agent().
 * \param agent_id 32-bit agent ID.
 * \param score Current score.
 */
void update_score(struct Context *ctx, uint32_t agent_id, int32_t score);

/**
 * \brief Compute the action for one controlled team member.
 *
 * Called once per tick for each \c agent_id in the team.
 *
 * \param ctx Context pointer returned by ::init_agent().
 * \param agent_id 32-bit ID of the ship/agent to act for.
 * \param tick Current tick number.
 *
 * \return Bitmask of ::ActionFlags.
 */
uint32_t make_action(struct Context *ctx, uint32_t agent_id, uint32_t tick);

#ifdef __cplusplus
}
#endif
#endif // SCUBYWASM_AGENT_H
