/* Settings written by the Java panel (any thread) and read by hooks (game threads). Plain ints with
 * relaxed atomics: every setting is independent, so no locking is needed. */
#include "rt.h"
#include "gen_settings.h"

static int g_cfg[CFG_SIZE];
static unsigned long g_stats[ST_COUNT];
static unsigned long g_rule_hits[RULE_COUNT];
static int g_dunk_available[DUNK_ACTION_COUNT];   /* 0 unknown, 1 loaded this game, -1 not loaded */

static int cfg(int id) { return load_int(&g_cfg[id]); }
static void stat_add(int id) { __atomic_add_fetch(&g_stats[id], 1, __ATOMIC_RELAXED); }
static void stat_set(int id, unsigned long v) { __atomic_store_n(&g_stats[id], v, __ATOMIC_RELAXED); }

static int tune_setting(int team, int key) { return cfg(CFG_TUNE_BASE + team * TUNE_KEYS + key); }
static int dunk_weight(int index) {
    if (index < 0) return cfg(CFG_DUNK_MIX) ? 0 : 10;   /* runtime actions outside the catalog */
    return cfg(CFG_DUNK_BASE + index);
}

static void cfg_defaults(void) {
    for (int i = 0; i < CFG_SCALAR_COUNT; i++) g_cfg[i] = cfg_default[i];
    for (int i = 0; i < RULE_COUNT; i++) g_cfg[CFG_RULE_BASE + i] = 0;
    for (int i = 0; i < 2 * TUNE_KEYS; i++) g_cfg[CFG_TUNE_BASE + i] = -1;
    for (int i = 0; i < DUNK_ACTION_COUNT; i++) g_cfg[CFG_DUNK_BASE + i] = 10;
}

/* Clamps to the declared range; unknown ids are ignored. */
static void cfg_set(int id, int value) {
    int lo, hi;
    if (id >= 0 && id < CFG_SCALAR_COUNT) { lo = cfg_min[id]; hi = cfg_max[id]; }
    else if (id >= CFG_RULE_BASE && id < CFG_RULE_BASE + RULE_COUNT) { lo = 0; hi = 1; }
    else if (id >= CFG_TUNE_BASE && id < CFG_TUNE_BASE + 2 * TUNE_KEYS) {
        int key = (id - CFG_TUNE_BASE) % TUNE_KEYS;
        lo = -1; hi = (key == 9 || key == 10) ? 200 : 100;
    } else if (id >= CFG_DUNK_BASE && id < CFG_DUNK_BASE + DUNK_ACTION_COUNT) { lo = 0; hi = 100; }
    else return;
    store_int(&g_cfg[id], value < lo ? lo : value > hi ? hi : value);
}

static long long stat_get(int id) {
    if (id >= 0 && id < ST_COUNT) return (long long)__atomic_load_n(&g_stats[id], __ATOMIC_RELAXED);
    if (id >= ST_RULE_BASE && id < ST_RULE_BASE + RULE_COUNT)
        return (long long)__atomic_load_n(&g_rule_hits[id - ST_RULE_BASE], __ATOMIC_RELAXED);
    if (id >= ST_DUNK_BASE && id < ST_DUNK_BASE + DUNK_ACTION_COUNT)
        return load_int(&g_dunk_available[id - ST_DUNK_BASE]);
    return 0;
}
