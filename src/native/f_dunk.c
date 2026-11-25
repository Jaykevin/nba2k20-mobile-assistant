/* Dunks: range-forced dunks, contact dunks, custom dunk mix/lock, career package unlock, finishing
 * floor. Fixes vs. previous build: Mvs_CanPlayerDunk's out-parameter is a 1-byte bool (was written as
 * a 4-byte int, clobbering caller stack); per-animation data is cached so choosing a dunk no longer
 * recomputes physics for every candidate at take-off. */
#include "rt.h"
#include "gen_dunks.h"

#define DUNK_ENTRY_SIZE 0x68
#define DUNK_TOKEN_RELEASE 0x26
#define RET_SCORING_PRE 0x12abcccu     /* return address of the shot-entry MVS_InitScoringMovePreAnimation call */
#define RET_BASKET_GATE 0x12abd14u     /* return address of the shot-entry distance gate */

static int dunk_in_range(void *player) {
    if (!cfg(CFG_FORCE_DUNK) || !player || FIELD(player, ACTOR_TYPE, unsigned) != 1) return 0;
    if (!FIELD(player, ACTOR_TEAM, void *) || !FIELD(player, ACTOR_PROP, void *) || !is_controlled(player)) return 0;
    float distance = G.rim_distance(player);
    return distance >= 0.0f && distance <= (float)cfg(CFG_DUNK_RANGE);
}

/* ------------------------------------------------------------------ super CPU vs. the user's shot features */
/* With 超强人机 on, the CPU jumped at and blocked practically every shot the user had forced in
 * (三分必中 / 全场必中) and every 强力隔扣 / 范围扣篮, so those features looked broken. While such a shot or
 * dunk is under way, super CPU's extra block help (forced block decision and response roll, anticipation,
 * hand-contact assist, the 3 m jump) stands down; the CPU can still block the way the normal game does.
 * Switch: CPU_SPARE (超强人机不针对我的必中和隔扣). */
static int user_shot_protected(void) {
    if (!cfg(CFG_CPU_SPARE) || !cfg(CFG_SUPER_CPU)) return 0;
    char *ball = game_ball();
    if (!ball) return 0;
    void *owner = ball_owner(ball);
    void *shooter;
    if (owner) {
        shooter = actor_player(owner);
    } else {
        unsigned mode = FIELD(ball, BALL_MODE, unsigned);
        if (mode < 3 || mode > 6) return 0;                  /* not a shot in flight */
        shooter = FIELD(ball, BALL_SHOOTER, void *);
    }
    if (!shooter || !is_controlled(shooter)) return 0;
    if (cfg(CFG_FORCE_ALL) || (cfg(CFG_FORCE_THREE) && G.is_three(shooter))) return 1;
    if (!owner) return 0;                                    /* dunks: while the ball is still in hand */
    if (dunk_in_range(shooter)) return 1;                    /* 范围扣篮 */
    if (cfg(CFG_CONTACT_DUNK) && G.is_shooting(owner)) {     /* 强力隔扣 range */
        float distance = G.rim_distance(shooter);
        if (distance >= 0.0f && distance <= 450.0f) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ shot -> dunk conversion */
static void (*orig_scoring_pre)(void *actor);
static void hook_scoring_pre(void *actor) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) - game_base;
    orig_scoring_pre(actor);
    if (caller != RET_SCORING_PRE || !actor || !dunk_in_range(actor_player(actor))) return;
    char *input = FIELD(actor, ACTOR_INPUT, char *);
    if (!input) return;
    int command = FIELD(input, INPUT_COMMAND, int);
    if (command < 23 || command > 43) return;
    FIELD(input, INPUT_COMMAND, int) = 31;
    FIELD(input, INPUT_COMMAND + 4, int) = 0;
    typedef void (*command_changed_fn)(void *input, int command);
    command_changed_fn changed = FIELD(input, 0x18, command_changed_fn);
    if (changed && command != 31) changed(input, 31);
    stat_add(ST_DUNK_COMMANDS);
}

static float (*orig_basket_distance)(void *actor);
static float hook_basket_distance(void *actor) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) - game_base;
    float distance = orig_basket_distance(actor);
    if (caller == RET_BASKET_GATE && distance > 100.0f && dunk_in_range(actor_player(actor))) return 100.0f;
    return distance;
}

static int (*orig_can_dunk)(void *player, int moving, unsigned char *contact);
static int hook_can_dunk(void *player, int moving, unsigned char *contact) {
    int strong = is_cpu_boosted(player) || (cfg(CFG_CONTACT_DUNK) && is_controlled(player));
    if (strong) {
        float distance = G.rim_distance(player);
        if (distance >= 0.0f && distance <= 450.0f) {
            if (contact) *contact = 1;
            return 1;
        }
    }
    if (dunk_in_range(player)) {
        if (contact) *contact = 0;
        return 1;
    }
    return orig_can_dunk(player, moving, contact);
}

/* ------------------------------------------------------------------ finishing chance */
static float finish_floor(void) {
    int floor = cfg(CFG_FINISH_FLOOR);
    return floor <= 0 ? 0.0f : floor >= 100 ? 1.0f : (float)floor / 100.0f;
}

static float (*orig_dunk_chance)(void *player, void *modifiers, float timing, unsigned flags);
static float hook_dunk_chance(void *player, void *modifiers, float timing, unsigned flags) {
    float base = orig_dunk_chance(player, modifiers, timing, flags);
    if (!finite_in(base, 0.0f, 1.0f)) return base;
    float target = base;
    if (is_controlled(player)) {
        if (cfg(CFG_CONTACT_DUNK)) target = maxf(target, .999f);
        target = maxf(target, finish_floor());
    } else if (is_cpu_boosted(player)) {
        target = maxf(target, .999f);
    }
    if (target > base) stat_add(ST_FINISH_BOOSTS);
    return target;
}

static float (*orig_layup_chance)(void *defense, void *player, int type, void *modifiers, float timing, unsigned flags);
static float hook_layup_chance(void *defense, void *player, int type, void *modifiers, float timing, unsigned flags) {
    float base = orig_layup_chance(defense, player, type, modifiers, timing, flags);
    if (!finite_in(base, 0.0f, 1.0f) || !is_controlled(player)) return base;
    float floor = finish_floor();
    if (floor > base) {
        stat_add(ST_FINISH_BOOSTS);
        return floor;
    }
    return base;
}

/* ------------------------------------------------------------------ career dunk packages */
static int (*orig_dunk_valid)(int package, void *data, int ignore_equipped);
static int hook_dunk_valid(int package, void *data, int ignore_equipped) {
    if (cfg(CFG_UNLOCK_DUNKS) && package >= 1 && package <= 64 && data && G.game_mode() == 3
        && data == G.career_player()) return 1;
    return orig_dunk_valid(package, data, ignore_equipped);
}

/* ------------------------------------------------------------------ custom dunk selection */
typedef struct { void *animation; unsigned crc; unsigned char ok; float x, z; } DunkCache;
static DunkCache dunk_cache[2048];
static void *recent_dunks[64];
static unsigned dunk_rotation;
static unsigned dunk_seed = 0x724b2031u;

static void dunk_cache_reset(void) { memset(dunk_cache, 0, sizeof(dunk_cache)); }

static int dunk_action_index(unsigned crc) {
    int low = 0, high = DUNK_ACTION_COUNT;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (dunk_sorted_crc[mid] < crc) low = mid + 1; else high = mid;
    }
    return low < DUNK_ACTION_COUNT && dunk_sorted_crc[low] == crc ? dunk_sorted_index[low] : -1;
}

/* Release callback + root offset of an animation; stable while the animation stays loaded. */
static int dunk_animation_data(void *animation, unsigned crc, float *x, float *z) {
    uintptr_t h = ((uintptr_t)animation >> 4) ^ (crc * 2654435761u);
    DunkCache *slot = NULL;
    for (unsigned probe = 0; probe < 16; probe++) {
        DunkCache *c = &dunk_cache[(h + probe) & (COUNTOF(dunk_cache) - 1)];
        if (c->animation == animation && c->crc == crc) { *x = c->x; *z = c->z; return c->ok; }
        if (!c->animation) { slot = c; break; }
    }
    char *callback = G.animation_callback(animation, DUNK_TOKEN_RELEASE);
    float release = callback ? FIELD(callback, 4, float) : -1.0f;
    float physics[16] = {0};
    int ok = callback && release > 0.0f && release <= 10.0f;
    if (ok) G.animation_physics(animation, 0.0f, 0.0f, physics);
    *x = physics[0];
    *z = physics[2];
    if (slot) {
        slot->crc = crc;
        slot->ok = (unsigned char)ok;
        slot->x = physics[0];
        slot->z = physics[2];
        slot->animation = animation;
    }
    return ok;
}

/* How well an animation's own approach length matches the real distance to the rim, as a weight
 * factor. The custom mix used to ignore distance, so a dunk animated from 1.5 m was launched from 5 m and
 * the dunk physics stretched it into a glide (or squashed it into a hop). 4 = plays as animated,
 * 1 = acceptable, 0 = visibly stretched. Squared lengths keep this free of libm. */
static unsigned dunk_fit(float length_sq, float actual) {
    float a = actual < 120.0f ? 120.0f : actual;
    float ratio = length_sq / (a * a);
    if (ratio >= 0.55f && ratio <= 1.8f) return 4;     /* within about -26% .. +34% */
    if (ratio >= 0.3f && ratio <= 3.2f) return 1;      /* within about -45% .. +79% */
    return 0;
}

static void *fallback_dunk(void *player, void *params, void *playback) {
    int variety = cfg(CFG_DUNK_VARIETY) && is_controlled(player);
    int locked = variety ? cfg(CFG_DUNK_LOCKED) : -1;
    int unlock = cfg(CFG_UNLOCK_DUNKS);
    float actual = G.rim_distance(player);
    char *best = NULL;
    void *best_animation = NULL;
    float best_score = 1.0e20f, best_x = 0.0f, best_z = 0.0f;
    int best_index = -1;
    unsigned eligible = 0, enabled_candidates = 0;
    static unsigned char seen[DUNK_ACTION_COUNT];
    /* passes 0-2: distance fit + shrinking recent-history filter; pass 3: any enabled dunk */
    for (int pass = 0; pass < 4 && !best; pass++) {
        memset(seen, 0, sizeof(seen));
        unsigned total_weight = 0;
        eligible = 0;
        int use_fit = variety && locked < 0 && pass < 3;
        if (variety && !pass)
            for (int i = 0; i < DUNK_ACTION_COUNT; i++) store_int(&g_dunk_available[i], -1);
        int last_package = variety ? 66 : 0;
        for (int package = -1; package < last_package; package++) {
            char *pool = NULL;
            int count = 0;
            if (package < 0) G.generic_dunk_pool((void **)&pool, &count);
            else G.signature_dunk_pool(package, (void **)&pool, &count);
            if (!pool || count < 1 || count > 4096) continue;
            for (int i = 0; i < count; i++) {
                char *entry = pool + i * DUNK_ENTRY_SIZE;
                /* single-player dunks that carry the release callback only */
                if (FIELD(entry, 4, unsigned) != 0 || !(FIELD(entry, 0x40, unsigned) & 0xa000)) continue;
                void *animation = FIELD(entry, 8, void *);
                if (!animation) continue;
                unsigned crc = FIELD(entry, 0, unsigned);
                int index = dunk_action_index(crc);
                if (index >= 0 && seen[index]) continue;
                float phys_x, phys_z;
                if (!dunk_animation_data(animation, crc, &phys_x, &phys_z)) continue;
                float x = FIELD(entry, 0x1c, float) - phys_x, z = FIELD(entry, 0x20, float) - phys_z;
                if (!finite_in(x, -1.0e5f, 1.0e5f) || !finite_in(z, -1.0e5f, 1.0e5f)) continue;
                float length_sq = x * x + z * z;
                if (!(length_sq > 1.0f)) continue;
                if (index >= 0) {
                    seen[index] = 1;
                    if (variety) store_int(&g_dunk_available[index], 1);
                }
                int weight = variety ? dunk_weight(index) : 1;
                if (variety) {
                    if (locked >= 0 && locked != index) continue;
                    if (!unlock && package >= 0) continue;
                    if (!weight || (index < 0 && locked >= 0)) continue;
                    if (!pass) ++enabled_candidates;
                    if (use_fit) {
                        unsigned fit = dunk_fit(length_sq, actual);
                        if (!fit) continue;
                        weight *= (int)fit;
                    }
                    unsigned shorter = enabled_candidates / 2;
                    if (shorter > 16) shorter = 16;
                    unsigned history = pass == 0 ? 64 : pass == 1 ? shorter : 0;
                    int recent = 0;
                    for (unsigned h = 0; h < history && h < dunk_rotation; h++)
                        if (recent_dunks[(dunk_rotation - h - 1) % 64] == animation) { recent = 1; break; }
                    if (locked < 0 && recent) continue;
                }
                float score = actual * actual - length_sq;
                if (score < 0.0f) score = -score;
                int select = score < best_score;
                if (variety) {
                    dunk_seed ^= dunk_seed << 13; dunk_seed ^= dunk_seed >> 17; dunk_seed ^= dunk_seed << 5;
                    ++eligible;
                    total_weight += (unsigned)weight;
                    select = dunk_seed % total_weight < (unsigned)weight;
                }
                if (select) {
                    best = entry; best_animation = animation; best_score = score;
                    best_x = x; best_z = z; best_index = index;
                }
            }
        }
    }
    if (variety) {
        unsigned available = 0;
        for (int i = 0; i < DUNK_ACTION_COUNT; i++) if (load_int(&g_dunk_available[i]) == 1) available++;
        stat_set(ST_DUNK_AVAILABLE, available);
    }
    if (!best) {
        if (variety && locked >= 0) stat_add(ST_DUNK_LOCK_MISSES);
        return NULL;
    }
    float angle = G.atan2f(best_x, best_z);
    if (!finite_in(angle, -1.0e6f, 1.0e6f)) return NULL;
    const unsigned *defaults = (const unsigned *)(game_base + OFF_DEFAULT_PLAYBACK);
    unsigned *output = playback;
    for (int i = 0; i < 16; i++) output[i] = defaults[i];
    FIELD(playback, 8, void *) = best_animation;
    FIELD(playback, 0x18, int) = FIELD(params, 0x110, int) - (int)angle;
    if (variety) {
        recent_dunks[dunk_rotation++ % 64] = best_animation;
        stat_set(ST_DUNK_LAST, (unsigned long)(best_index + 1));
        unsigned fit = dunk_fit(best_x * best_x + best_z * best_z, actual);
        if (fit >= 4) stat_add(ST_DUNK_FIT);
        LOGI("dunk pick action=%d candidates=%u weight=%d locked=%d fit=%u rim=%.0fcm",
             best_index, eligible, dunk_weight(best_index), locked, fit, (double)actual);
    }
    return best;
}

static void *(*orig_find_dunk)(void *player, void *params, void *playback);
static void *hook_find_dunk(void *player, void *params, void *playback) {
    void *result = orig_find_dunk(player, params, playback);
    int custom = cfg(CFG_DUNK_VARIETY) && is_controlled(player);
    int eligible = dunk_in_range(player) || (custom && result);
    if (!eligible && is_cpu_boosted(player)) {
        float distance = G.rim_distance(player);
        eligible = distance >= 0.0f && distance <= 450.0f;
    }
    if (!params || !playback || !eligible) return result;
    if (!result || custom) {
        void *selection = fallback_dunk(player, params, playback);
        if (selection) result = selection;
        else if (custom) result = NULL;   /* never swap a locked/mixed choice for an unselected dunk */
    }
    if (result) stat_add(ST_DUNK_CHOICES);
    return result;
}
