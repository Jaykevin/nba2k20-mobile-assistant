/* Carried-over features: tune sliders, movement multipliers, ankle breakers, foul/violation gates,
 * forced made shots. Behaviour matches the previous build; code is restructured. */
#include "rt.h"

/* Super-CPU player: CPU side, not human controlled, feature enabled. */
static int is_cpu_boosted(void *player) {
    return cfg(CFG_SUPER_CPU) && player && !is_controlled(player) && team_of(player) == 1;
}

/* ------------------------------------------------------------------ TuneData sliders */
static void *(*orig_tune_data)(int index);
static const unsigned short tune_offsets[TUNE_KEYS] = {
    188, 180, 172, 164, 196, 212, 148, 228, 28, 20, 84, 76, 132, 116,
    356, 364, 4, 236, 12, 140, 60, 68, 44, 52, 100, 340, 316, 324};
static int tune_lock;
static void *tune_snapshot;
static unsigned tune_baseline[2][TUNE_KEYS], tune_written[2][TUNE_KEYS];
static unsigned char tune_changed[2][TUNE_KEYS];

static int super_cpu_tunable(int key, int saved) {
    if (!cfg(CFG_SUPER_CPU)) return saved;
    if (key == 9 || key == 10 || key >= 25) return saved;  /* movement + foul/injury keep the user's choice */
    if (key == 14) return 0;                               /* no fatigue */
    if (key == 16 || key == 17) return 60;                 /* steals stay beatable */
    return 100;
}

static void *hook_tune_data(int index) {
    void *data = orig_tune_data(index);
    stat_add(ST_TUNE_READS);
    if (!data || index != 0 || __atomic_exchange_n(&tune_lock, 1, __ATOMIC_ACQUIRE)) return data;
    if (tune_snapshot != data) {
        tune_snapshot = data;
        memset(tune_changed, 0, sizeof(tune_changed));
    }
    /* Columns are physical (what the game reads per team); settings are logical (我方 = the user's
     * team). In 街头 the column of the user's team changes with possession, so map every read. */
    int human_slot = human_tune_slot();
    for (int team = 0; team < 2; team++) {
        int side = team == human_slot ? 0 : 1;
        for (int key = 0; key < TUNE_KEYS; key++) {
            unsigned *field = (unsigned *)((char *)data + tune_offsets[key] + team * 4);
            int setting = tune_setting(side, key);
            if (side == 1) setting = super_cpu_tunable(key, setting);
            unsigned current = *field;
            if (setting < 0) {
                if (tune_changed[team][key] && current == tune_written[team][key])
                    *field = tune_baseline[team][key];
                tune_changed[team][key] = 0;
                continue;
            }
            unsigned desired = float_bits((float)(setting > 100 ? 100 : setting) / 100.0f);
            if (!tune_changed[team][key] || current != tune_written[team][key]) tune_baseline[team][key] = current;
            if (current != desired) *field = desired;
            tune_written[team][key] = desired;
            tune_changed[team][key] = 1;
        }
    }
    __atomic_store_n(&tune_lock, 0, __ATOMIC_RELEASE);
    return data;
}

/* ------------------------------------------------------------------ movement multipliers */
static float (*orig_move_speed)(void *player, float ability, int turbo);
static float (*orig_turn_ratio)(void *mode, void *actor);

static float movement_factor(int key, int value) {
    if (value <= 50) return 0.25f + (float)value * 0.015f;
    if (key == 9) return value <= 100 ? 1.0f + (float)(value - 50) * 0.04f : 3.0f + (float)(value - 100) * 0.02f;
    return value <= 100 ? 1.0f + (float)(value - 50) * 0.06f : 4.0f + (float)(value - 100) * 0.04f;
}

static float apply_movement(void *player, int key, float base) {
    int team = team_of(player);
    if (team < 0) return base;
    int value = tune_setting(team, key);
    return value < 0 ? base : base * movement_factor(key, value);
}

static float hook_move_speed(void *player, float ability, int turbo) {
    return apply_movement(player, 9, orig_move_speed(player, ability, turbo));
}

static float hook_turn_ratio(void *mode, void *actor) {
    float base = orig_turn_ratio(mode, actor);
    if (!actor) return base;
    void **vtable = *(void ***)actor;
    if (!vtable || !vtable[3]) return base;
    void *nba_actor = ((void *(*)(void *))vtable[3])(actor);
    if (!nba_actor || FIELD(nba_actor, ACTOR_TYPE, unsigned) != 1) return base;
    return apply_movement(actor_player(actor), 10, base);
}

/* ------------------------------------------------------------------ ankle breakers */
static float (*orig_lerp)(const float *table, float input);
static float (*orig_badge)(void *manager);
static int (*orig_takeover_ankle)(void *attacker, void *defender);

static float hook_lerp(const float *table, float input) {
    float value = orig_lerp(table, input);
    uintptr_t offset = (uintptr_t)table - game_base;
    if (LIKELY(offset != OFF_ANKLE_CURVE_A && offset != OFF_ANKLE_CURVE_B)) return value;
    int strength = cfg(CFG_ANKLE);
    if (strength >= 100) {
        stat_add(ST_ANKLE_FORCED);
        return 1.0e9f;
    }
    float delta = (float)(strength - 50);
    float scale = strength <= 50 ? (float)strength / 50.0f : 1.0f + delta * delta * delta / 10.0f;
    value *= scale;
    if (!(value >= 0.0f)) value = 0.0f;
    return value > 1.0e9f ? 1.0e9f : value;
}

static float hook_badge(void *manager) { return cfg(CFG_ANKLE) >= 100 ? 1.0f : orig_badge(manager); }
static int hook_takeover_ankle(void *attacker, void *defender) {
    return cfg(CFG_ANKLE) >= 100 ? 0 : orig_takeover_ankle(attacker, defender);
}

/* ------------------------------------------------------------------ foul / violation gates */
static int rule_blocks(int rule) {
    if (!cfg(CFG_RULE_BASE + rule)) return 0;
    unsigned long n = __atomic_add_fetch(&g_rule_hits[rule], 1, __ATOMIC_RELAXED);
    if (n <= 3) LOGI("blocked rule %d (n=%lu)", rule, n);
    return 1;
}

/* All gated events return void and take at most four integer/pointer arguments. */
#define RULE_HOOK(n) \
    static void (*orig_rule_##n)(void *, void *, void *, void *); \
    static void rule_##n(void *a, void *b, void *c, void *d) { if (!rule_blocks(n)) orig_rule_##n(a, b, c, d); }
RULE_HOOK(0) RULE_HOOK(1) RULE_HOOK(2) RULE_HOOK(3) RULE_HOOK(4) RULE_HOOK(5) RULE_HOOK(6)
RULE_HOOK(7) RULE_HOOK(8) RULE_HOOK(9) RULE_HOOK(10) RULE_HOOK(11) RULE_HOOK(12) RULE_HOOK(13)
RULE_HOOK(14) RULE_HOOK(15) RULE_HOOK(16) RULE_HOOK(17) RULE_HOOK(18) RULE_HOOK(19) RULE_HOOK(20)

/* ------------------------------------------------------------------ forced made shots */
static void (*orig_shot_init)(void *actor, void *ball, const float *position, float distance,
                              int trajectory, unsigned flags, int timing);
static void (*orig_shot)(float chance, void *actor, unsigned flags);
static void (*orig_shot_offset)(float *offset, int *kind, const void *stats, int last, float factor);
static void (*orig_shot_start)(void *ball, const float *offset);
static char *shot_stats;
static void *pending_ball;
static int pending_forced;
static uintptr_t deflection_thread;   /* our own PHY_DeflectShot call is running on this thread */

static int force_shot_for(void *player) {
    if (deflection_thread && deflection_thread == current_thread()) return 0;
    if (!player || (!cfg(CFG_FORCE_ALL) && !cfg(CFG_FORCE_THREE)) || !is_controlled(player)) return 0;
    return cfg(CFG_FORCE_ALL) || G.is_three(player);
}

static void hook_shot_init(void *actor, void *ball, const float *position, float distance,
                           int trajectory, unsigned flags, int timing) {
    pending_forced = 0;
    pending_ball = ball;
    orig_shot_init(actor, ball, position, distance, trajectory, flags, timing);
    pending_forced = force_shot_for(actor_player(actor));
}

static void hook_shot(float chance, void *actor, unsigned flags) {
    void *player = actor_player(actor);
    pending_forced = force_shot_for(player);
    if (is_cpu_boosted(player)) {
        float floor = G.behind_three(player) ? .65f : .78f;
        if (chance >= 0.0f && chance < floor) chance = floor;
    }
    if (pending_forced) {
        flags = (flags & ~64u) | 32u;
        chance = 1.0f;
    }
    orig_shot(chance, actor, flags);
    if (pending_forced) stat_add(ST_FORCED_SHOTS);
}

static void center_shot_stats(void) {
    if (!shot_stats) return;
    FIELD(shot_stats, 0x3a8, unsigned) = 1;
    FIELD(shot_stats, 0x48, float) = 1.0f;
    FIELD(shot_stats, 0x3b4, unsigned) = 0;
    FIELD(shot_stats, 0x3b8, unsigned) = 0;
}

static void hook_shot_offset(float *offset, int *kind, const void *stats, int last, float factor) {
    orig_shot_offset(offset, kind, stats, last, factor);
    if (!pending_forced || stats != shot_stats || !offset) return;
    center_shot_stats();
    for (int i = 0; i < 4; i++) offset[i] = 0.0f;
    if (kind) *kind = -1;
}

static void hook_shot_start(void *ball, const float *offset) {
    if (!pending_forced) {
        orig_shot_start(ball, offset);
        return;
    }
    center_shot_stats();
    const float centered[4] = {0, 0, 0, 0};
    orig_shot_start(ball, centered);
    if (ball == pending_ball) {
        pending_forced = 0;
        pending_ball = NULL;
    }
}
