/* Jump height boost, airborne block assist (hand contact for humans and super-CPU) and giant mode.
 * Fixes vs. previous build: custom physics-handler table entries are released/recycled instead of
 * filling up for good; an interrupted jump (no landing callback) no longer leaves a stale profile that
 * swallowed every later block-jump callback for that player.
 * 2026-11-23: the extra height never lifts the hands over a ball the player is going for (blocks, grab
 * animations and rebounds used to whiff under the ball); the block assist measures from the hands
 * instead of a cylinder around the body (no more deflections from a metre away); a boosted jump whose
 * actor stops getting physics updates is undone so nobody is left hanging in the air. */
#include "rt.h"

#define TOKEN_AIR_BEGIN 0x26
#define TOKEN_AIR_END 0x27
#define BLOCK_REACH_TO_HAND 0.4f     /* 封盖范围 setting (cm, 50..400) -> allowed ball-to-hand gap */
#define AIM_RADIUS 450.0f            /* ball within this horizontal distance = the jump goes for it */
#define AIM_RATE 25.0f               /* max change of the applied extra height per physics frame (cm) */
#define PHYSICS_TIMEOUT 8u           /* frames without a physics update before a boosted jump is undone */

static void on_block(void *player, void *ball, int source);   /* f_new.c */
static unsigned g_frame;                                      /* advanced by the per-frame tick */

/* ------------------------------------------------------------------ giant mode (experimental) */
static void (*orig_init_scale)(void *actor, float scale);
typedef struct { void *actor; float base; } ScaleEntry;
static ScaleEntry scale_entries[48];

static ScaleEntry *scale_entry(void *actor, int create) {
    ScaleEntry *empty = NULL;
    for (unsigned i = 0; i < COUNTOF(scale_entries); i++) {
        if (scale_entries[i].actor == actor) return &scale_entries[i];
        if (!scale_entries[i].actor && !empty) empty = &scale_entries[i];
    }
    if (!create || !empty) return NULL;
    empty->actor = actor;
    empty->base = 0.0f;
    return empty;
}

static void hook_init_scale(void *actor, float scale) {
    ScaleEntry *entry = scale_entry(actor, 1);
    if (entry) entry->base = scale;
    orig_init_scale(actor, scale);
}

static void giant_update(void *actor) {
    int enabled = cfg(CFG_GIANT);
    ScaleEntry *entry = scale_entry(actor, enabled);
    if (!entry) return;
    float current = FIELD(actor, ACTOR_SCALE, float);
    if (!finite_in(entry->base, 0.3f, 3.0f)) {
        if (!finite_in(current, 0.3f, 3.0f)) return;
        entry->base = current;
    }
    float factor = enabled && is_controlled(actor_player(actor)) ? (float)cfg(CFG_GIANT_SCALE) / 100.0f : 1.0f;
    float desired = entry->base * factor;
    float delta = desired - current;
    if (delta > -0.002f && delta < 0.002f) return;
    orig_init_scale(actor, current + clampf(delta, -0.02f, 0.02f));   /* grow/shrink over a few frames */
    stat_add(ST_GIANT_APPLIED);
}

/* ------------------------------------------------------------------ jump profiles */
struct JumpProfile {
    void *actor, *animation;
    char *prop;
    float begin, peak, end, extra, natural_lift;
    float base_height, boosted_height;
    float applied;                 /* extra height applied on the latest physics frame (after aiming) */
    int blocking, aimed;
    unsigned start_frame, physics_frame;
};
static struct JumpProfile jump_profiles[16];
static struct JumpProfile *active_jump;
static uintptr_t jump_thread;

static struct JumpProfile *find_jump(void *actor) {
    for (unsigned i = 0; i < COUNTOF(jump_profiles); i++)
        if (jump_profiles[i].actor == actor) return &jump_profiles[i];
    return NULL;
}

static char *actor_layer(void *actor) {
    char *animator = actor ? FIELD(actor, ACTOR_ANIMATOR, char *) : NULL;
    return animator ? FIELD(animator, ANIMATOR_LAYER, char *) : NULL;
}

static void restore_jump_height(struct JumpProfile *p, char *prop) {
    float added = p->boosted_height - p->base_height;
    if (prop && prop == p->prop && added != 0.0f) {
        float *height = &FIELD(prop, PROP_HEIGHT, float);
        float drift = *height - p->boosted_height;
        /* Still our value (the game may nudge it slightly). Anything else means a game transition already
         * replaced the height - leave it alone. */
        if (drift > -2.0f && drift < 2.0f) *height -= added;
    }
    p->boosted_height = p->base_height;
}

static void release_jump(struct JumpProfile *p) {
    restore_jump_height(p, FIELD(p->actor, ACTOR_PROP, char *));
    p->actor = NULL;
}

static void (*orig_start_dunk_physics)(void *actor, float ground, float velocity, float gravity);
static void hook_start_dunk_physics(void *actor, float ground, float velocity, float gravity) {
    // Dunk physics reads the current height to initialize its landing trajectory.
    struct JumpProfile *p = find_jump(actor);
    if (p) release_jump(p);
    orig_start_dunk_physics(actor, ground, velocity, gravity);
}

static int actor_in_air(void *actor) {
    char *state = FIELD(actor, ACTOR_STATE, char *);
    return state && G.is_in_air(state);
}

/* The jump ended without a landing callback (interrupted): the player is back on the ground, or the
 * profile is far older than any jump. Animation switches mid-air are NOT staleness - the game can
 * blend between animations during one jump, and restarting the curve there looked like a double jump. */
static int jump_stale(struct JumpProfile *p) {
    unsigned age = g_frame - p->start_frame;
    return age > 300u || p->prop != FIELD(p->actor, ACTOR_PROP, char *)
        || (age > 6u && !actor_in_air(p->actor));
}

static float jump_target(void *player) {
    if (is_controlled(player)) return cfg(CFG_JUMP_BOOST) ? (float)cfg(CFG_JUMP_HEIGHT) : 0.0f;
    return is_cpu_boosted(player) && !user_shot_protected() ? 300.0f : 0.0f;
}

static void refresh_jump_target(struct JumpProfile *p, void *player) {
    float extra = jump_target(player) - p->natural_lift;
    p->extra = extra > 0.0f && extra <= 320.0f ? extra : 0.0f;
}

static void setup_jump(void *actor) {
    struct JumpProfile *old = find_jump(actor);
    char *layer = actor_layer(actor);
    void *animation = layer ? FIELD(layer, 0, void *) : NULL;
    if (old && !jump_stale(old)) {
        if (old->blocking) return;
        if (old->animation == animation && layer && FIELD(layer, 8, float) >= old->begin
            && FIELD(layer, 8, float) < old->end) return;
    }
    if (old) release_jump(old);
    void *player = actor_player(actor);
    int cpu = is_cpu_boosted(player);
    int human = is_controlled(player);
    if (!cpu && (!human || (!cfg(CFG_JUMP_BOOST) && !cfg(CFG_AIR_BLOCK)))) return;
    if (!animation || G.is_dunk_physics(actor)) return;
    float begin = G.token_time(animation, TOKEN_AIR_BEGIN), end = G.token_time(animation, TOKEN_AIR_END);
    float scale = FIELD(actor, ACTOR_SCALE, float);
    float height = G.player_height(player);
    if (!(begin >= 0.0f && end > begin + .08f && end - begin <= 4.0f) || !finite_in(scale, 0.01f, 100.0f)
        || !finite_in(height, 140.0f, 260.0f)) return;
    float now = FIELD(layer, 8, float);
    if (!(now >= begin - .04f && now < end)) return;
    float start = G.root_height(animation, begin) * scale;
    float peak = start, peak_time = (begin + end) * .5f;
    for (int i = 1; i < 24; i++) {
        float t = begin + (end - begin) * (float)i / 24.0f;
        float v = G.root_height(animation, t) * scale;
        if (v > peak) { peak = v; peak_time = t; }
    }
    float natural_lift = peak - start;
    if (!finite_in(natural_lift, 0.0f, 320.0f)) return;
    char *prop = FIELD(actor, ACTOR_PROP, char *);
    if (!prop) return;
    struct JumpProfile *p = NULL;
    for (unsigned i = 0; i < COUNTOF(jump_profiles) && !p; i++)
        if (!jump_profiles[i].actor) p = &jump_profiles[i];
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->animation = animation;
    p->begin = begin;
    p->peak = peak_time;
    p->end = end;
    p->natural_lift = natural_lift;
    p->prop = prop;
    p->start_frame = g_frame;
    p->physics_frame = g_frame;
    p->actor = actor;
    refresh_jump_target(p, player);
    stat_add(ST_JUMPS);
}

static void (*orig_jump_in_air)(void *actor);
static void hook_jump_in_air(void *actor) {
    struct JumpProfile *p = find_jump(actor);
    if (p && p->blocking) {
        if (actor_in_air(actor) && !jump_stale(p)) return;
        release_jump(p);
    }
    orig_jump_in_air(actor);
    setup_jump(actor);
    p = find_jump(actor);
    if (p) p->blocking = 1;
}

static void (*orig_jump_landing)(void *actor, void *callback);
static void hook_jump_landing(void *actor, void *callback) {
    struct JumpProfile *p = find_jump(actor);
    if (p) release_jump(p);
    orig_jump_landing(actor, callback);
}

/* 弹跳增强 must not lift the hands over a ball the player is going for (block, rebound, tip, catch): the
 * game chose the jump so the hands meet the ball at the natural height, and the extra height made blocks
 * and grab animations whiff under the ball. Returns the extra height that brings the higher hand up to
 * the ball (0 if the natural jump already reaches it); jumps with the ball (shots, layups), after one's
 * own shot, or with the ball far away keep the full curve. */
static float jump_aim_cap(struct JumpProfile *p, void *player, float curve) {
    char *ball = game_ball();
    if (!ball || !(curve > 0.0f)) return curve;
    void *owner = ball_owner(ball);
    if (owner == p->actor || owner == player) return curve;
    if (owner && team_of(actor_player(owner)) == team_of(player)) return curve;       /* teammate has it */
    unsigned mode = FIELD(ball, BALL_MODE, unsigned);
    if (!owner && mode >= 3 && mode <= 6 && FIELD(ball, BALL_SHOOTER, void *) == player) return curve;
    const float *b = ball_position(ball);
    char *prop = FIELD(p->actor, ACTOR_PROP, char *);
    if (!b || !prop) return curve;
    const float *me = (const float *)(prop + PROP_POSITION);
    float dx = b[0] - me[0], dz = b[2] - me[2];
    if (!(dx * dx + dz * dz <= AIM_RADIUS * AIM_RADIUS)) return curve;
    if (G.is_goaltend(ball, p->actor)) return 0.0f;   /* never push a hand into a goaltending touch */
    float hand = highest_hand_y(p->actor);
    if (!finite_in(hand, -1000.0f, 3000.0f)) return curve;
    float need = b[1] - (hand - p->applied);   /* the hands were placed with last frame's extra height */
    return need <= 0.0f ? 0.0f : minf(need, curve);
}

static void add_jump_height(struct JumpProfile *p, float *height) {
    if (p->boosted_height != p->base_height) return;
    if (!p->actor || p->prop != FIELD(p->actor, ACTOR_PROP, char *) || !actor_in_air(p->actor)) return;
    char *layer = actor_layer(p->actor);
    if (!layer || FIELD(layer, 0, void *) != p->animation || !finite_in(*height, -10000.0f, 10000.0f)) return;
    float time = FIELD(layer, 8, float);
    if (!(time > p->begin && time < p->end)) return;
    void *player = actor_player(p->actor);
    refresh_jump_target(p, player);
    if (!(p->extra > 0.0f)) return;

    float f = time <= p->peak ? (time - p->begin) / (p->peak - p->begin) : (p->end - time) / (p->end - p->peak);
    float curve = p->extra * f * f * (3.0f - 2.0f * f);
    float target = jump_aim_cap(p, player, curve);
    if (target < curve - 1.0f && !p->aimed) {
        p->aimed = 1;
        stat_add(ST_JUMP_AIM);
    }
    /* smooth: no pops when the aim engages or the profile starts mid-air; never above the curve */
    float applied = p->applied + clampf(target - p->applied, -AIM_RATE, AIM_RATE);
    applied = clampf(applied, 0.0f, curve);
    p->applied = applied;
    p->base_height = *height;
    p->boosted_height = *height + applied;
    *height = p->boosted_height;
}

static struct JumpProfile *prepare_player_physics(void *actor) {
    struct JumpProfile *p = find_jump(actor);
    char *prop = actor ? FIELD(actor, ACTOR_PROP, char *) : NULL;
    if (p) {
        restore_jump_height(p, prop);
        p->physics_frame = g_frame;
        if (jump_stale(p)) {
            p->actor = NULL;
            p = NULL;
        }
    }
    void *player = actor_player(actor);
    if (p) refresh_jump_target(p, player);
    if (cfg(CFG_JUMP_BOOST) && is_controlled(player)) {
        char *layer = actor_layer(actor);
        if (layer && actor_in_air(actor) && (!p || (!p->blocking && p->animation != FIELD(layer, 0, void *)))) {
            setup_jump(actor);
            p = find_jump(actor);
        }
    }
    return p;
}

/* ------------------------------------------------------------------ airborne block assist */
/* Allowed gap between the ball centre and a hand surface. Super-CPU: real contact (one ball radius).
 * Human: the 封盖范围 setting scaled to a hand distance (default 1.8 m -> 72 cm), so the swat still
 * happens next to a hand instead of anywhere within a 1.8 m cylinder around the body. */
static float block_gap_limit(int cpu) {
    if (cpu) {
        float scale = G.ball_scale();
        return finite_in(scale, 0.01f, 4.0f) ? 11.811f * scale : -1.0f;
    }
    return (float)cfg(CFG_BLOCK_REACH) * BLOCK_REACH_TO_HAND;
}

static void assist_block(void *actor) {
    void *player = actor_player(actor);
    int cpu = is_cpu_boosted(player);
    if (!cpu && !(cfg(CFG_AIR_BLOCK) && is_controlled(player))) return;
    struct JumpProfile *jump = find_jump(actor);
    if (!jump || !jump->blocking) return;
    char *layer = actor_layer(actor);
    if (!layer || FIELD(layer, 0, void *) != jump->animation) return;
    float time = FIELD(layer, 8, float);
    if (!(time > jump->begin && time < jump->end)) return;
    char *ball = game_ball();
    if (!ball || (FIELD(ball, BALL_FLAGS, unsigned) & BALL_BLOCKED)) return;
    unsigned mode = FIELD(ball, BALL_MODE, unsigned);
    if (mode != 1 && (mode < 3 || mode > 6)) return;
    void *shooter = FIELD(ball, BALL_SHOOTER, void *);
    if (mode == 1) {
        void *owner = ball_owner(ball);
        if (!owner || !G.is_shooting(owner)) return;
        char *state = FIELD(owner, ACTOR_STATE, char *);
        if (!state || !G.is_in_air(state)) return;
        shooter = actor_player(owner);
    }
    int shooter_team = team_of(shooter), defender_team = team_of(player);
    if (shooter_team < 0 || defender_team < 0 || shooter_team == defender_team) return;
    if (cpu && (!is_controlled(shooter) || user_shot_protected())) return;
    const float *position = ball_position(ball);
    if (!position) return;
    float gap = hand_ball_gap(actor, position);
    if (!(gap <= block_gap_limit(cpu))) return;
    if (G.is_goaltend(ball, actor)) return;
    pending_forced = 0;
    pending_ball = NULL;
    deflection_thread = current_thread();
    G.deflect_shot(actor, ball);          /* -> EVT_BallBlocked -> on_block (grab) for the human */
    deflection_thread = 0;
    if (!(FIELD(ball, BALL_FLAGS, unsigned) & BALL_BLOCKED)) return;
    if (cpu) stat_add(ST_CPU_BLOCKS);
    else on_block(player, ball, 3);       /* no-op if the block event already reported it */
}

/* ------------------------------------------------------------------ per-frame safety */
/* A boosted jump whose actor stopped receiving physics updates (warp after a basket in 街头, cut-scene,
 * replay) would leave the player hanging at the boosted height. Only actors updated recently are
 * touched; older entries are dropped without reading the actor (it may be gone). */
static void jump_tick(void) {
    for (unsigned i = 0; i < COUNTOF(jump_profiles); i++) {
        struct JumpProfile *p = &jump_profiles[i];
        if (!p->actor) continue;
        unsigned idle = g_frame - p->physics_frame;
        if (idle <= PHYSICS_TIMEOUT) continue;
        if (idle <= 120u && p->boosted_height != p->base_height) {
            restore_jump_height(p, p->prop);
            stat_add(ST_FLOAT_FIXES);
        }
        p->actor = NULL;
    }
}

/* New game session (no game frame for 30 s): forget jump profiles without touching actors - they may
 * belong to the previous game. A paused jump simply loses its boost; the game recomputes the height
 * from the animation on the next physics frame. Giant-mode and physics-handler tables are kept on
 * purpose: they are keyed by live objects and re-learned/overwritten by the game's own init calls. */
static void jump_reset(void) {
    memset(jump_profiles, 0, sizeof(jump_profiles));
}

/* ------------------------------------------------------------------ physics hooks */
typedef struct { void *animator; void (*handler)(void *, const float *); unsigned last_frame; } PhysHandler;
static PhysHandler phys_handlers[128];

static PhysHandler *phys_find(void *animator) {
    for (unsigned i = 0; i < COUNTOF(phys_handlers); i++)
        if (phys_handlers[i].animator == animator) return &phys_handlers[i];
    return NULL;
}

static PhysHandler *phys_alloc(void) {
    PhysHandler *oldest = NULL;
    for (unsigned i = 0; i < COUNTOF(phys_handlers); i++) {
        PhysHandler *e = &phys_handlers[i];
        if (!e->animator) return e;
        if (g_frame - e->last_frame > 3600u && (!oldest || e->last_frame < oldest->last_frame)) oldest = e;
    }
    return oldest;   /* unused for over a minute of game frames */
}

static void (*orig_actor_physics)(void *actor, const float *physics);
static void (*orig_set_physics)(void *animator, void *handler);
static int (*orig_is_physics)(void *animator, void *handler);
static void hook_custom_physics(void *actor, const float *physics);

static void hook_actor_physics(void *actor, const float *physics) {
    giant_update(actor);
    struct JumpProfile *p = prepare_player_physics(actor);
    active_jump = p && p->extra > 0.0f ? p : NULL;
    jump_thread = active_jump ? current_thread() : 0;
    orig_actor_physics(actor, physics);
    active_jump = NULL;
    jump_thread = 0;
    assist_block(actor);
}

static void hook_set_physics(void *animator, void *handler) {
    PhysHandler *entry = phys_find(animator);
    if (!handler || handler == (void *)&hook_actor_physics || handler == (void *)&hook_custom_physics) {
        if (entry) entry->animator = NULL;   /* back to the default handler: free the slot */
        orig_set_physics(animator, handler);
        return;
    }
    if (!entry) entry = phys_alloc();
    if (!entry) {
        stat_add(ST_PHYS_TABLE);              /* table full: this animator keeps its own handler */
        orig_set_physics(animator, handler);
        return;
    }
    entry->handler = (void (*)(void *, const float *))handler;
    entry->last_frame = g_frame;
    entry->animator = animator;
    orig_set_physics(animator, (void *)&hook_custom_physics);
}

static int hook_is_physics(void *animator, void *handler) {
    if (animator && FIELD(animator, ANIMATOR_HANDLER, void *) == (void *)&hook_custom_physics) {
        PhysHandler *entry = phys_find(animator);
        if (entry) return (void *)entry->handler == handler;
    }
    return orig_is_physics(animator, handler);
}

static void hook_custom_physics(void *actor, const float *physics) {
    char *animator = FIELD(actor, ACTOR_ANIMATOR, char *);
    PhysHandler *entry = animator ? phys_find(animator) : NULL;
    if (!entry || !entry->handler) {
        hook_actor_physics(actor, physics);
        return;
    }
    entry->last_frame = g_frame;
    giant_update(actor);
    prepare_player_physics(actor);
    entry->handler(actor, physics);
    struct JumpProfile *p = find_jump(actor);
    char *prop = FIELD(actor, ACTOR_PROP, char *);
    if (p && prop) add_jump_height(p, &FIELD(prop, PROP_HEIGHT, float));
    assist_block(actor);
}

static int (*orig_jump_augmentation)(void *data, float *physics);
static int hook_jump_augmentation(void *data, float *physics) {
    int result = orig_jump_augmentation(data, physics);
    struct JumpProfile *p = active_jump;
    if (p && p->actor && physics && jump_thread == current_thread() && data == p->prop + PROP_AUGMENT)
        add_jump_height(p, &physics[1]);
    return result;
}
