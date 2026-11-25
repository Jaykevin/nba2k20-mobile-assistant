/* Super CPU: attributes, block timing/anticipation, defensive positioning, dribble and shot tendencies,
 * offense bias (three-point vs. attack the rim) with score chasing. Ported from the previous build. */
#include "rt.h"

#define RET_BLOCK_SHOT_CHANCE 0x11ccbf0u
#define RET_BLOCK_DRIVE_CHANCE 0x11ccc9cu
#define RET_BLOCK_RESPONSE_ROLL 0x11cd3f8u   /* final block-response roll, after geometry checks */

static int cpu_roster(void *player) {
    if (!cfg(CFG_SUPER_CPU) || !player || !FIELD(player, PLAYER_ROSTER, void *)) return 0;
    return team_of(player) == 1;   /* CPU side by on-court list, not the swapping team object */
}
static int cpu_player_data(void *data) { return data && cpu_roster(FIELD(data, PLAYERDATA_AI_PLAYER, void *)); }

/* ------------------------------------------------------------------ attributes */
static float (*orig_cpu_attribute)(void *player, int key);
static float hook_cpu_attribute(void *player, int key) {
    float base = orig_cpu_attribute(player, key);
    if (key < 0 || key > 52 || !cpu_roster(player)) return base;
    switch (key) {
        case 20: return .60f;
        case 31: return .72f;
        case 32: return .76f;
        case 33: return .78f;
        case 34: return .94f;
        case 35: return .65f;
        case 36: return .96f;
        case 38: return .85f;
        case 39: return .18f;
        case 48: return base;
        default: return 1.0f;
    }
}

/* ------------------------------------------------------------------ block decision + roll */
static uintptr_t cpu_block_thread;
static float (*orig_cpu_block_shot)(void *player);
static float (*orig_cpu_block_drive)(void *player);
static float (*orig_cpu_uniform)(unsigned bits);

static float cpu_block_chance(void *player, uintptr_t caller, uintptr_t expected, float base) {
    int active = is_cpu_boosted(player) && !user_shot_protected();
    if (caller == expected) __atomic_store_n(&cpu_block_thread, active ? current_thread() : 0, __ATOMIC_RELAXED);
    return active ? 8.0f : base;
}
static float hook_cpu_block_shot(void *player) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) - game_base;
    return cpu_block_chance(player, caller, RET_BLOCK_SHOT_CHANCE, orig_cpu_block_shot(player));
}
static float hook_cpu_block_drive(void *player) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) - game_base;
    return cpu_block_chance(player, caller, RET_BLOCK_DRIVE_CHANCE, orig_cpu_block_drive(player));
}
static float hook_cpu_uniform(unsigned bits) {
    float value = orig_cpu_uniform(bits);
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) - game_base;
    if (LIKELY(caller != RET_BLOCK_RESPONSE_ROLL) || !cfg(CFG_SUPER_CPU)) return value;
    uintptr_t thread = current_thread();
    if (!__atomic_compare_exchange_n(&cpu_block_thread, &thread, 0, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return value;
    return value < .98f ? 0.0f : 1.0f;
}

/* ------------------------------------------------------------------ offense decisions */
struct CpuDecision {
    void *player, *ball, *owner;
    float possession_start, next_shot, next_score_check;
    int prefer_three, score_difference, offense_bias;
    unsigned three_roll;
};
static struct CpuDecision cpu_decisions[12];
static unsigned cpu_decision_slot;

static struct CpuDecision *cpu_decision(void *player) {
    for (unsigned i = 0; i < COUNTOF(cpu_decisions); i++)
        if (cpu_decisions[i].player == player) return &cpu_decisions[i];
    struct CpuDecision *entry = &cpu_decisions[cpu_decision_slot++ % COUNTOF(cpu_decisions)];
    memset(entry, 0, sizeof(*entry));
    entry->player = player;
    return entry;
}

static int cpu_three_preference(int score_difference) {
    int bias = cfg(CFG_CPU_THREE_BIAS);
    int bonus = score_difference <= -9 ? 25 : score_difference <= -4 ? 22 : score_difference < 0 ? 16 : 0;
    return bias + bonus > 100 ? 100 : bias + bonus;
}

static void update_cpu_score(struct CpuDecision *e, void *player, float now, int initial) {
    void *team = FIELD(player, ACTOR_TEAM, void *);
    int difference = team ? G.score_difference(team) : 0;
    if (difference < -300 || difference > 300) difference = 0;
    int bias = cfg(CFG_CPU_THREE_BIAS);
    if (initial || difference != e->score_difference || bias != e->offense_bias) {
        e->score_difference = difference;
        e->offense_bias = bias;
        e->prefer_three = e->three_roll < (unsigned)cpu_three_preference(difference);
        if (!initial && difference < 0 && e->next_shot > now + .35f) e->next_shot = now + .35f;
    }
    e->next_score_check = now + .25f;
}

/* Called once per frame: forget possessions that ended. */
static void observe_cpu_possession(void) {
    if (!cfg(CFG_SUPER_CPU)) return;
    void *ball = game_ball();
    void *owner = ball_owner(ball);
    float now = G.elapsed();
    for (unsigned i = 0; i < COUNTOF(cpu_decisions); i++) {
        struct CpuDecision *e = &cpu_decisions[i];
        if (e->player && (e->ball != ball || (owner && owner != e->owner) || now < e->possession_start)) e->ball = NULL;
    }
}

static struct CpuDecision *cpu_attack(void *player) {
    if (!is_cpu_boosted(player)) return NULL;
    void *ball = G.attached_ball(player);
    if (!ball || ball != game_ball()) return NULL;
    float now = G.elapsed();
    if (!finite_in(now, 0.0f, 1.0e8f)) return NULL;
    struct CpuDecision *e = cpu_decision(player);
    if (e->ball != ball || now < e->possession_start) {
        e->ball = ball;
        e->owner = player;
        e->possession_start = now;
        float delay = (float)(rng_next() % 700u) / 1000.0f;
        e->three_roll = rng_next() % 100u;
        update_cpu_score(e, player, now, 1);
        e->next_shot = now + (e->score_difference < 0 ? .35f + delay * .6f : .55f + delay);
    } else if (now >= e->next_score_check || e->offense_bias != cfg(CFG_CPU_THREE_BIAS)) {
        update_cpu_score(e, player, now, 0);
    }
    return e;
}

/* ------------------------------------------------------------------ tendencies */
#define TENDENCY_HOOK(tag, expr) \
    static int (*orig_tend_##tag)(void *data); \
    static int tend_##tag(void *data) { \
        if (!cpu_player_data(data)) return orig_tend_##tag(data); \
        int bias = cfg(CFG_CPU_THREE_BIAS); (void)bias; \
        return (expr); \
    }
TENDENCY_HOOK(nothing, 12)
TENDENCY_HOOK(cross, 95)
TENDENCY_HOOK(spin, 75)
TENDENCY_HOOK(behind, 90)
TENDENCY_HOOK(double_cross, 85)
TENDENCY_HOOK(half_spin, 60)
TENDENCY_HOOK(in_out, 85)
TENDENCY_HOOK(hesitation, 80)
TENDENCY_HOOK(step_back, 25 + bias * 75 / 100)
TENDENCY_HOOK(pull_up, 10 + bias * 90 / 100)
TENDENCY_HOOK(drive, 100 - bias * 95 / 100)
TENDENCY_HOOK(three, bias)
TENDENCY_HOOK(step_jumper, 15 + bias * 85 / 100)
TENDENCY_HOOK(dunk, 100)
TENDENCY_HOOK(finish, 100 - bias * 35 / 100)

static float (*orig_cpu_shot_value)(void *player, void *defender, void *info);
static float hook_cpu_shot_value(void *player, void *defender, void *info) {
    float base = orig_cpu_shot_value(player, defender, info);
    if (!is_cpu_boosted(player) || !G.behind_three(player)) return base;
    float distance = G.rim_distance(player);
    if (!(distance >= 500.0f && distance <= 1050.0f)) return base;
    struct CpuDecision *e = cpu_attack(player);
    if (!e || !e->prefer_three) return base;
    float desire = e->score_difference < 0 ? .98f : .95f;
    if (!(base < desire)) return base;
    stat_add(ST_CPU_THREES);
    return desire;
}

static void (*orig_cpu_dribble_update)(void *player, int direction, int allow);
static void hook_cpu_dribble_update(void *player, int direction, int allow) {
    struct CpuDecision *e = cpu_attack(player);
    if (e && e->prefer_three && G.elapsed() >= e->next_shot && G.behind_three(player)
        && G.rim_distance(player) <= 1050.0f && G.able_to_shoot(player) && !G.dribble_locked(player)
        && !G.is_shooting(player) && !G.needs_clear(player, e->ball)) {
        e->next_shot = G.elapsed() + (e->score_difference < 0 ? .8f : 1.2f);
        if (G.run_play_shoot(player, 6, 0)) return;
    }
    orig_cpu_dribble_update(player, direction, allow);
}

/* ------------------------------------------------------------------ defense */
static void (*orig_cpu_guard)(void *stance, void *player, void *opponent);
static void hook_cpu_guard(void *stance, void *player, void *opponent) {
    orig_cpu_guard(stance, player, opponent);
    if (!stance || !is_cpu_boosted(player) || !is_controlled(opponent)) return;
    void *ball = game_ball();
    if (!ball || ball_owner(ball) != opponent) return;
    char *prop = FIELD(opponent, ACTOR_PROP, char *);
    char *defender_prop = FIELD(player, ACTOR_PROP, char *);
    if (!prop || !defender_prop) return;
    const float *position = (const float *)(prop + PROP_POSITION);
    const float *defender = (const float *)(defender_prop + PROP_POSITION);
    const float *velocity = (const float *)(prop + PROP_VELOCITY);
    float dx = position[0] - defender[0], dz = position[2] - defender[2];
    if (!(dx * dx + dz * dz < 1600.0f * 1600.0f)) return;
    float *goal = stance;
    for (int axis = 0; axis <= 2; axis += 2) {
        float lead = velocity[axis] * .18f;
        if (!finite_in(lead, -300.0f, 300.0f) || !finite_in(goal[axis], -10000.0f, 10000.0f)) return;
    }
    for (int axis = 0; axis <= 2; axis += 2) {
        float lead = clampf(velocity[axis] * .18f, -140.0f, 140.0f);
        goal[axis] = position[axis] + lead + (goal[axis] - position[axis]) * .6f;
    }
}

static float shot_release_remaining(void *player) {
    char *layer = actor_layer(player);
    void *animation = layer ? FIELD(layer, 0, void *) : NULL;
    if (!animation) return -1.0f;
    float release = G.release_time(animation), time = FIELD(layer, 8, float);
    if (!(release >= 0.0f && release <= 10.0f && time >= 0.0f && time <= release + .04f)) return -1.0f;
    float remaining = G.map_span(player, release > time ? release - time : 0.0f);
    return finite_in(remaining, 0.0f, 4.0f) ? remaining : -1.0f;
}

static float (*orig_cpu_block_timing)(void *player, void *shooter);
static float hook_cpu_block_timing(void *player, void *shooter) {
    float base = orig_cpu_block_timing(player, shooter);
    if (!is_cpu_boosted(player) || !is_controlled(shooter) || !G.is_shooting(shooter) || user_shot_protected())
        return base;
    float remaining = shot_release_remaining(shooter);
    if (remaining < 0.0f) return base;
    float delay = remaining - .22f;
    return delay > 0.0f ? delay : 0.0f;
}

static int (*orig_block_delay)(void *player, void **shooter, float *delay, int *kind);
static int hook_block_delay(void *player, void **shooter, float *delay, int *kind) {
    int result = orig_block_delay(player, shooter, delay, kind);
    if (!is_cpu_boosted(player) || user_shot_protected()) return result;
    if (result) {
        if (delay && *delay > .015f) *delay = .015f;
        return result;
    }
    if (!shooter || !delay || !kind) return 0;
    char *ball = game_ball();
    void *owner = ball_owner(ball);
    if (!owner || !is_controlled(owner) || !G.is_shooting(owner) || team_of(owner) == team_of(player)) return 0;
    char *prop = FIELD(player, ACTOR_PROP, char *);
    char *opponent_prop = FIELD(owner, ACTOR_PROP, char *);
    char *state = FIELD(player, ACTOR_STATE, char *);
    if (!prop || !opponent_prop || !state || G.is_in_air(state)) return 0;
    float remaining = shot_release_remaining(owner);
    if (!(remaining >= 0.0f && remaining <= 1.2f)) return 0;
    const float *target = (const float *)(opponent_prop + PROP_POSITION);
    const float *position = (const float *)(prop + PROP_POSITION);
    const float *velocity = (const float *)(opponent_prop + PROP_VELOCITY);
    float dx = target[0] + velocity[0] * .15f - position[0];
    float dz = target[2] + velocity[2] * .15f - position[2];
    if (!(dx * dx + dz * dz <= 550.0f * 550.0f)) return 0;
    *shooter = owner;
    *delay = .005f;
    *kind = 0;
    return 1;
}
