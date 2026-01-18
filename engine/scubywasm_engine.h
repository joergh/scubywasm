#ifndef SCUBYWASM_ENGINE_H
#define SCUBYWASM_ENGINE_H

/**
 * \file scubywasm_engine.h
 * \anchor scubywasm_engine_api
 * \brief Scubywasm engine ABI.
 *
 * This header defines the C ABI of the Scubywasm engine (the simulation core).
 * It is designed to be:
 *  - callable from native code (hosted builds), and
 *  - exportable as a WebAssembly (WASM) module in freestanding builds.
 *
 * **Typical call pattern**
 *
 * The engine is driven in three phases:
 *
 * \b 1) Configuration
 *  - Populate a ::Config (e.g., with ::set_default_config()) and optionally
 *    override fields.
 *
 * \b 2) Round execution
 *  - Create a new round state via ::create_context().
 *  - Add ships/agents via ::add_agent().
 *  - For each simulation tick:
 *      - Set actions for agents via ::set_action().
 *      - Advance the simulation via ::tick().
 *      - Observe state via ::get_ship_pose(), ::get_shot_pose(),
 *        ::is_alive(), and ::get_score().
 *
 * \b 3) Shutdown
 *  - Destroy the round state via ::free_context().
 *
 * **Hosted vs. freestanding builds**
 *
 * The compile-time macro \c FREESTANDING controls whether the engine provides
 * helper APIs intended for WASM / freestanding use.
 *
 * - If \c FREESTANDING is \c 1:
 *     - The engine avoids any dependencies, including glibc, libm, etc.
 *     - Allocation is intended to be static/fixed-size (no requirement for
 *       malloc/free in the module).
 *     - The engine exposes singleton argument buffers such as
 *       ::get_pose_buffer() and ::get_config_buffer().
 *
 * - If \c FREESTANDING is \c 0:
 *     - The engine is intended to be used from a hosted/native context.
 *     - Callers can naturally allocate ::Config and ::Pose in their own address
 *       space (stack/heap) and pass pointers directly.
 *     - Therefore the singleton buffer helpers are hidden.
 *
 * **Why singleton buffers exist (WASM use-case)**
 *
 * In WASM, the engine module owns its linear memory. External callers often
 * cannot simply take the address of a host-allocated ::Pose/::Config and pass
 * it across the ABI boundary. The singleton buffer helpers solve this by
 * letting the module provide stable storage:
 *
 *  - Call ::get_pose_buffer() / ::get_config_buffer() to obtain a pointer to
 *    module-owned storage.
 *  - Fill that struct in-place.
 *  - Pass the same pointer back to functions like ::add_agent() or
 *    ::create_context().
 *
 * This avoids requiring a general-purpose allocator inside the module and keeps
 * argument passing simple and deterministic.
 *
 * \warning The singleton buffers are not re-entrant. If you call into the
 * engine from multiple threads or overlap calls that reuse the same buffer, you
 * must provide external synchronization.
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

#ifndef FREESTANDING
#    define FREESTANDING 1
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * \brief Action bitmask.
 *
 * The engine interprets the argument of ::set_action() as a bitwise OR of
 * these flags. Unless explicitly stated otherwise, flags are combinable.
 *
 * Dynamics (turn rate, max velocity, shot velocity, lifetimes, etc.) are
 * defined by the current configuration as provided via ::create_context()
 * using ::Config.
 *
 * \c ACTION_TURN_LEFT and \c ACTION_TURN_RIGHT are logically mutually
 * exclusive. If a caller sets both, the engine may ignore both, pick one
 * deterministically, or apply an engine-defined tie-breaker.
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
     * \c Config::ship_max_velocity. If \c ACTION_THRUST is not set, the ship's
     * velocity is set to zero.
     */
    ACTION_THRUST = 1U,

    /**
     * Turn left for this tick.
     *
     * Turning is binary: if \c ACTION_TURN_LEFT is set, the ship's heading is
     * changed by \c Config::ship_max_turn_rate degrees (left) for this tick. If
     * it is not set, no left turn is applied.
     */
    ACTION_TURN_LEFT = 2U,

    /**
     * Turn right for this tick.
     *
     * Turning is binary: if \c ACTION_TURN_RIGHT is set, the ship's heading is
     * changed by exactly \c Config::ship_max_turn_rate degrees (right) for this
     * tick. If it is not set, no right turn is applied.
     */
    ACTION_TURN_RIGHT = 4U,

    /**
     * Fire a shot.
     *
     * Shots travel with velocity \c Config::shot_velocity and expire after
     * \c Config::shot_lifetime ticks. Each \c agent_id may have at most one
     * active shot at a time; if a shot is already active, firing is ignored.
     */
    ACTION_FIRE = 8U,
};

/**
 * \brief Engine configuration.
 *
 * This structure defines the relevant game dynamics and constraints that
 * callers (and agents, indirectly) should use for planning (movement, turning,
 * shooting, collision avoidance, etc.).
 *
 * The configuration is provided exactly once during initialization via
 * ::create_context() and is never changed thereafter for a given \c Context.
 *
 * Typical usage:
 * \code
 * struct Config cfg;
 * set_default_config(&cfg);
 * cfg.ship_max_velocity = .02F;
 * struct Context *ctx = create_context(&cfg);
 * \endcode
 *
 * In freestanding/WASM builds, callers typically use ::get_config_buffer()
 * instead of stack/heap allocation.
 */
struct Config
{
    /**
     * Ship turn rate per tick (in degrees per tick).
     *
     * If \c ACTION_TURN_LEFT or \c ACTION_TURN_RIGHT is set, the ship's
     * heading is changed by this value (left/right) for this tick.
     */
    float ship_max_turn_rate;

    /**
     * Ship speed when thrust is enabled (in torus-units per tick).
     *
     * Ship speed is binary: either zero or this configured maximum.
     * If \c ACTION_THRUST is set, the ship's velocity is set to this value. If
     * \c ACTION_THRUST is not set, the ship's velocity is zero.
     */
    float ship_max_velocity;

    /**
     * Ship hit radius (in torus-units).
     *
     * Ships are considered colliding/touching when their distance satisfies the
     * engine's collision criterion derived from this radius.
     */
    float ship_hit_radius;

    /**
     * Shot velocity (in torus-units per tick).
     *
     * Determines how far a shot advances per tick after \c ACTION_FIRE
     * succeeds.
     */
    float shot_velocity;

    /**
     * Shot lifetime / end-of-life (in ticks).
     *
     * A shot is removed when its lifetime reaches zero.
     */
    int32_t shot_lifetime;
};

/**
 * \brief Pose on the unit torus.
 *
 * A pose describes position and orientation in the engine's 2D world:
 *  - \c x and \c y are coordinates on the unit torus in the interval [0, 1).
 *  - \c heading is an orientation angle in degrees in [0, 360) with:
 *      - 0 deg = up,
 *      - 90 deg = right,
 *      - 180 deg = down,
 *      - 270 deg = left.
 *
 * In freestanding/WASM builds, callers typically populate a ::Pose via
 * ::get_pose_buffer() (module-owned singleton storage) rather than passing a
 * pointer to host-allocated memory.
 */
struct Pose
{
    /** x-position in [0, 1). */
    float x;

    /** y-position in [0, 1). */
    float y;

    /** Heading in degrees in [0, 360). */
    float heading;
};

/**
 * \brief Opaque engine state for a single round.
 *
 * A \c Context owns all mutable simulation state for one running round:
 * ships/agents, shots, scores, and any internal bookkeeping required by the
 * engine. The concrete definition is private to the engine implementation.
 *
 * A context is created with ::create_context() and must be released with
 * ::free_context().
 */
struct Context;

#if FREESTANDING
/**
 * \brief Return a pointer to a module-owned ::Config singleton buffer.
 *
 * This helper exists for freestanding/WASM builds where the caller cannot
 * conveniently allocate a ::Config inside the engine module. It provides
 * module-owned storage that can be populated in-place and then passed to
 * ::create_context().
 *
 * Typical usage:
 * \code
 * struct Config *cfg = get_config_buffer();
 * set_default_config(cfg);
 * cfg->ship_max_velocity = .02F;
 * struct Context *ctx = create_context(cfg);
 * \endcode
 *
 * \return Pointer to a module-owned scratch ::Config.
 *
 * \warning The returned pointer refers to a singleton buffer and is not
 * re-entrant. Its contents are overwritten by the caller; if multiple
 * independent configurations must be retained, copy the struct elsewhere before
 * reusing the buffer.
 */
[[nodiscard]] struct Config *get_config_buffer(void);
#endif

/**
 * \brief Initialize a ::Config to engine defaults.
 *
 * Writes a complete default configuration into \p cfg. Callers may override
 * individual fields afterwards before passing the configuration to
 * ::create_context().
 *
 * \param cfg Output pointer to the configuration to initialize.
 *
 * \warning \p cfg must not be \c NULL.
 *
 * \see Config
 * \see create_context
 * \see get_config_buffer (FREESTANDING only)
 */
void set_default_config(struct Config *cfg);

/**
 * \brief Create a new engine context (start a new round).
 *
 * Allocates and initializes a fresh \c Context using the provided
 * configuration. The returned context is independent from any other context
 * and represents a single round.
 *
 * \param cfg Immutable configuration for the new round.
 *
 * \return A new \c Context on success, or \c NULL on failure (for example, if
 *         static resources are exhausted in freestanding builds).
 *
 * \warning \p cfg must not be \c NULL.
 *
 * \see Config
 * \see free_context
 */
[[nodiscard]] struct Context *create_context(const struct Config *cfg);

/**
 * \brief Destroy a context created by ::create_context().
 *
 * Releases all resources owned by \p ctx.
 *
 * \param ctx Context pointer returned by ::create_context(). If \c NULL, the
 *            function has no effect.
 *
 * \see create_context
 */
void free_context(struct Context *ctx);

#if FREESTANDING
/**
 * \brief Return a pointer to a module-owned ::Pose singleton buffer.
 *
 * This helper exists for freestanding/WASM builds where the caller cannot
 * conveniently allocate a ::Pose inside the engine module. The returned buffer
 * can be used to pass poses into the engine without directly interacting with
 * the module's linear memory allocation scheme.
 *
 * Typical usage:
 * \code
 * struct Pose *pose = get_pose_buffer();
 * pose->x = .5F;
 * pose->y = .5F;
 * pose->heading = 90.F;
 * uint32_t id = add_agent(ctx, pose);
 * \endcode
 *
 * \return Pointer to a module-owned scratch ::Pose.
 *
 * \warning The returned pointer refers to a singleton buffer and is not
 * re-entrant. Its contents are overwritten by the caller; if multiple
 * independent values must be retained, copy the struct elsewhere before
 * reusing the buffer.
 */
[[nodiscard]] struct Pose *get_pose_buffer(void);
#endif

/**
 * \brief Add a new ship/agent to the round.
 *
 * Registers a new ship in \p ctx and assigns it a fresh 32-bit \c agent_id.
 * The returned \c agent_id is the handle used by all other per-ship engine API
 * calls (e.g., ::set_action(), ::get_ship_pose(), ::get_shot_pose(),
 * ::is_alive(), ::get_score()).
 *
 * This function is intended for round setup after ::create_context() and before
 * the first ::tick().
 *
 * \param ctx  Engine context.
 * \param pose Initial ship pose (see ::Pose).
 *
 * \return Opaque 32-bit \c agent_id identifying the newly added ship. Agent IDs
 *         are not required to be zero-based nor sequential; callers must not
 *         assume any particular numbering scheme.
 *
 * \warning \p ctx and \p pose must not be \c NULL.
 *
 * \note In freestanding/WASM builds, \p pose is typically populated via
 * ::get_pose_buffer() (module-owned singleton storage).
 */
[[nodiscard]] uint32_t add_agent(struct Context *ctx, const struct Pose *pose);

/**
 * \brief Process an agent action for the next tick.
 *
 * Interprets \p flags as a bitwise OR of ::ActionFlags and updates the
 * corresponding state in \p ctx immediately (ship kinematics and, if requested
 * and allowed, shot creation), using the configuration embedded in the context
 * (see ::Config).
 *
 * Call this exactly once per ship per simulation tick, before calling ::tick().
 *
 * \warning Do not call this multiple times for the same \p agent_id within a
 * single tick. The function performs immediate state updates; repeated calls
 * will stack effects in engine-defined ways. Nothing good comes of it.
 *
 * \param ctx      Engine context.
 * \param agent_id Ship/agent identifier.
 * \param flags    Bitmask of ::ActionFlags.
 *
 * \return 0 on success, a negative integer otherwise
 *
 * \see ActionFlags
 * \see tick
 */
int32_t
set_action(struct Context *ctx, uint32_t agent_id, enum ActionFlags flags);

/**
 * \brief Advance the simulation by one or more ticks.
 *
 * Advances the engine state in \p ctx by \p n_times simulation ticks. This
 * updates all time-dependent state (ship/shot motion, lifetimes, collisions,
 * scoring, etc.).
 *
 * \p tick() may be called repeatedly without any additional setup. In
 * particular, if no new actions are processed via ::set_action(), the
 * simulation simply continues from the current ship state.
 *
 * \param ctx     Engine context.
 * \param n_times Number of ticks to simulate.
 *
 * \return The number of ships that are alive after advancing the simulation.
 *
 * \warning \p ctx must not be \c NULL.
 *
 * \see set_action
 * \see is_alive
 */
uint32_t tick(struct Context *ctx, uint32_t n_times);

/**
 * \brief Get the current pose of a ship.
 *
 * Writes the current ship pose (position on the unit torus and heading) of
 * \p agent_id into \p pose.
 *
 * \param ctx      Engine context.
 * \param agent_id Ship/agent identifier.
 * \param pose     Output pointer receiving the ship pose.
 *
 * \warning \p ctx and \p pose must not be \c NULL.
 *
 * \see Pose
 * \see get_pose_buffer (FREESTANDING only)
 * \see tick
 */
void get_ship_pose(const struct Context *ctx,
                   uint32_t agent_id,
                   struct Pose *pose);

/**
 * \brief Get the current pose of the active shot of a ship.
 *
 * If \p agent_id currently has an active shot, writes its pose into \p pose and
 * returns the remaining lifetime of that shot (in ticks). If no shot is active,
 * returns 0.
 *
 * \param ctx      Engine context.
 * \param agent_id Ship/agent identifier (shot owner).
 * \param pose     Output pointer receiving the shot pose if a shot is active.
 *
 * \return Remaining shot lifetime in ticks (> 0) if a shot is active, 0 if shot
 *         is inactive.
 *
 * \warning \p ctx and \p pose must not be \c NULL.
 *
 * \note If the return value is <= 0, the contents of \p pose are unspecified.
 *
 * \see Pose
 * \see get_pose_buffer (FREESTANDING only)
 * \see tick
 */
int32_t
get_shot_pose(const struct Context *ctx, uint32_t agent_id, struct Pose *pose);

/**
 * \brief Check whether a ship is alive.
 *
 * A ship is either alive (\c 1) or not alive (\c 0). Ships may transition from
 * alive to not alive during ::tick() (e.g., due to collisions).
 *
 * \param ctx      Engine context.
 * \param agent_id Ship/agent identifier.
 *
 * \return \c 1 if the ship is alive, \c 0 if it is not alive.
 *
 * \warning \p ctx must not be \c NULL.
 *
 * \see tick
 */
[[nodiscard]] int32_t is_alive(const struct Context *ctx, uint32_t agent_id);

/**
 * \brief Get the current score of a ship.
 *
 * Returns the score accumulated by \p agent_id in the current round.
 * Score updates occur during ::tick() (e.g., when shots hit or ships are
 * destroyed), so callers typically query scores after advancing the simulation.
 *
 * \param ctx      Engine context.
 * \param agent_id Ship/agent identifier.
 *
 * \return Current score (signed).
 *
 * \warning \p ctx must not be \c NULL.
 *
 * \see tick
 */
[[nodiscard]] int32_t get_score(const struct Context *ctx, uint32_t agent_id);

#ifdef __cplusplus
}
#endif
#endif // SCUBYWASM_ENGINE_H
