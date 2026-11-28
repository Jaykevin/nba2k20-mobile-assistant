/* Game bindings: resolved function pointers, named struct offsets and shared helpers.
 * Offsets were derived from disassembly of libnba2k20_clean_opt.so (v98.0.2) and from the
 * previous build's verified behaviour; they are only valid for that exact binary, which ships
 * inside the same APK. */
#include "rt.h"

#define GAME_LIB "libnba2k20_clean_opt.so"
#define PROCESS_FRAME_TIME 0x3354  /* float, Game_UpdateCommon reads the process frame duration */

/* --- AI_NBA_ACTOR / AI_PLAYER (players derive from actors; same pointer) --- */
#define ACTOR_SCALE 0x20          /* float, set by AI_InitNBAActorScale */
#define ACTOR_INPUT 0x28          /* controller input object */
#define ACTOR_STATE 0x30          /* MVS_NBA_ACTOR_DATA* */
#define ACTOR_ANIMATOR 0x38       /* ANM_ANIMATOR* */
#define ACTOR_PROP 0x40           /* physics prop: +0x30 position, +0x34 height, +0x300 velocity */
#define ACTOR_COLLISION 0x80
#define ACTOR_TEAM 0x98           /* AI_TEAM* */
#define ACTOR_TYPE 0xd0           /* 1 = player */
#define PLAYER_ROSTER 0xb70
#define PLAYER_STREAK 0x162c      /* streak state 0..4 (4 = on fire) */
#define ANIMATOR_HANDLER 0x60
#define ANIMATOR_LAYER 0x68       /* layer: +0 animation, +8 float time */
#define PROP_POSITION 0x30
#define PROP_HEIGHT 0x34
#define PROP_AUGMENT 0xb0
#define PROP_VELOCITY 0x300
#define INPUT_COMMAND 0x2c
/* --- AI_BALL --- */
#define BALL_OWNER 0x0
#define BALL_PROP 0x18            /* prop: +0x80 position */
#define BALL_SHOOTER 0x140
#define BALL_FLAGS 0x1ac          /* 0x800 = blocked */
#define BALL_MODE 0x1e4           /* 1 = held, 3..6 = shot in flight */
#define BALL_BLOCKED 0x800
/* --- PLAYERDATA --- */
#define PLAYERDATA_AI_PLAYER 0x50
/* --- AI_TEAM --- */
#define TEAM_PLAYERS 0xa0         /* on-court AI_PLAYER* by position (AI_GetHomeTeamPlayerByPosition) */
#define TEAM_SLOTS 5
/* --- user profiles (Profile_IsUserProfileControlled): home at OFF_PROFILES, away +PROFILE_STRIDE --- */
#define OFF_PROFILES 0x383b060
#define PROFILE_STRIDE 0xd08
#define PROFILE_PLAYER 0x20       /* AI_PLAYER* the profile plays */
/* --- REF_GetPlayState --- */
#define PLAY_STATE_LIVE 10
#define GAME_DATA_GAME_TYPE 0x250 /* GameData_Items: game type */
/* --- static data inside the game library (offsets from load base) --- */
#define OFF_DEFAULT_PLAYBACK 0x20164e0
#define OFF_ANKLE_CURVE_A 0x2016fc0
#define OFF_ANKLE_CURVE_B 0x2016fd8
#define OFF_SHOT_STATS 0x3814bb0

#define FIELD(ptr, off, type) (*(type *)((char *)(ptr) + (off)))

static uintptr_t game_base;

static struct {
    int (*is_user)(void *player);
    int (*controller_id)(void *player);
    int (*tune_index)(void *team);
    int (*is_three)(void *player);
    float (*rim_distance)(void *actor);
    void (*generic_dunk_pool)(void **pool, int *count);
    int (*signature_dunk_pool)(int package, void **pool, int *count);
    void *(*animation_callback)(void *animation, int token);
    void (*animation_physics)(void *animation, float a, float b, void *out);
    float (*atan2f)(float y, float x);
    void *(*roster_team)(void *player);
    float (*root_height)(void *animation, float time);
    float (*token_time)(void *animation, int token);
    float (*player_height)(void *player);
    int (*behind_three)(void *player);
    int (*is_in_air)(void *state);
    int (*is_dunk_physics)(void *actor);
    void *(*career_player)(void);
    int (*game_mode)(void);
    void **ball_global;
    void (*deflect_shot)(void *actor, void *ball);
    int (*is_goaltend)(void *ball, void *actor);
    int (*is_shooting)(void *actor);
    float (*elapsed)(void);
    float (*release_time)(void *animation);
    float (*map_span)(void *actor, float span);
    float (*ball_scale)(void);
    int (*able_to_shoot)(void *player);
    int (*dribble_locked)(void *actor);
    int (*needs_clear)(void *player, void *ball);
    int (*run_play_shoot)(void *actor, unsigned target, int action);
    void *(*attached_ball)(void *actor);
    int (*score_difference)(void *team);
    void (*catch_token)(void *actor);
    int (*doing_grab_block)(void *player);
    void (*speed_interrupt)(float speed, float duration, float blend_out, float blend_in);
    int (*speed_interrupt_active)(void);
    void (*init_actor_scale)(void *actor, float scale);
    void (*attach_ball)(void *ball, void *actor);
    void (*ball_caught)(void *actor, void *ball, unsigned flags);
    void (*deflected_off_player)(void *ball, void *actor, int type, int a, int b, int c);
    int (*play_state)(void);
    char *home_team, *away_team;          /* AI_TEAM objects: +0xa0 on-court players by position */
    char *game_data_items;                /* +0x250 game type */
} G;

typedef struct { const char *name; void **slot; int required; } Binding;
#define BIND(field, name) { name, (void **)&G.field, 1 }
#define BIND_OPT(field, name) { name, (void **)&G.field, 0 }

static Binding game_bindings[] = {
    BIND(is_user, "_Z31Profile_IsUserProfileControlledP9AI_PLAYER"),
    BIND(controller_id, "_Z18AI_GetControllerIdP9AI_PLAYER"),
    BIND(tune_index, "_Z21AI_GetPlayerTuneIndexPK7AI_TEAM"),
    BIND(is_three, "_Z28REF_IsThreePointShotByPlayerPK9AI_PLAYER"),
    BIND(rim_distance, "_Z27PHY_GetDistanceToPlayersRimPK12AI_NBA_ACTOR"),
    BIND(generic_dunk_pool, "_Z22Mvs_GetGenericDunkPoolPPK9DUNK_DATAPi"),
    BIND(signature_dunk_pool, "_Z18Mvs_GetSigDunkPool23PLAYERDATA_DUNK_PACKAGEPPK9DUNK_DATAPi"),
    BIND(animation_callback, "_Z28ANM_GetFirstCallbackInstancePK13ANM_ANIMATION18ANM_CALLBACK_TOKEN"),
    BIND(animation_physics, "_Z22ANM_ComputePhysicsDataPK13ANM_ANIMATIONffP21ANM_ANIMATION_PHYSICS"),
    BIND(atan2f, "_Z11float_atan2ff"),
    BIND(roster_team, "_Z29AI_Roster_GetTeamFromAIRosterP9AI_PLAYER"),
    BIND(root_height, "_Z21ANM_ComputeRootHeightPK13ANM_ANIMATIONf"),
    BIND(token_time, "_Z36ANM_GetSingletonCallbackInstanceTimePK13ANM_ANIMATION18ANM_CALLBACK_TOKEN"),
    BIND(player_height, "_Z18AI_GetPlayerHeightPK9AI_PLAYER"),
    BIND(behind_three, "_Z32REF_IsPlayerBehindThreePointLinePK9AI_PLAYER"),
    BIND(is_in_air, "_Z11MVS_IsInAirPK18MVS_NBA_ACTOR_DATA"),
    BIND(is_dunk_physics, "_Z23PHY_IsDunkPhysicsActivePK12AI_NBA_ACTOR"),
    BIND(career_player, "_Z30CareerModeData_GetRosterPlayerv"),
    BIND(game_mode, "_Z16GameMode_GetModev"),
    BIND(ball_global, "gAi_GameBall"),
    BIND(deflect_shot, "_Z15PHY_DeflectShotP8AI_ACTORP7AI_BALL"),
    BIND(is_goaltend, "_Z26REF_IsTouchingBallGoaltendP7AI_BALLP12AI_NBA_ACTOR"),
    BIND(is_shooting, "_Z28MVS_IsActorInAnyShootingMovePK12AI_NBA_ACTOR"),
    BIND(elapsed, "_Z25AI_GetUnscaledElapsedTimev"),
    BIND(release_time, "_Z22MVS_GetReleaseAnimTimePK13ANM_ANIMATION"),
    BIND(map_span, "_Z29MVS_MapAnimTimeSpanToRealTimePK12AI_NBA_ACTORf"),
    BIND(ball_scale, "_Z16PHY_GetBallScalev"),
    BIND(able_to_shoot, "_Z17MVS_IsAbleToShootPK9AI_PLAYER"),
    BIND(dribble_locked, "_Z23MVS_IsDribbleMoveLockedPK12AI_NBA_ACTOR"),
    BIND(needs_clear, "_Z42REF_IsHalfCourtClearBallNecessaryForPlayerP9AI_PLAYERP7AI_BALL"),
    BIND(run_play_shoot, "_Z16BHV_RunPlayShootP12AI_NBA_ACTORj15BHV_SHOT_ACTION"),
    BIND(attached_ball, "_Z26AI_GetNBAActorAttachedBallPK12AI_NBA_ACTOR"),
    BIND(score_difference, "_Z26REF_GetTeamScoreDifferencePK7AI_TEAM"),
    BIND(catch_token, "_Z34MVS_HandleBlockCollisionCatchTokenP12AI_NBA_ACTOR"),
    BIND(doing_grab_block, "_Z26MVS_IsPlayerDoingGrabBlockP9AI_PLAYER"),
    BIND(speed_interrupt, "_Z32AI_ActivateGameSpeedInterruptionffff"),
    BIND(speed_interrupt_active, "_Z32AI_IsGameSpeedInterruptionActivev"),
    BIND(init_actor_scale, "_Z20AI_InitNBAActorScaleP12AI_NBA_ACTORf"),
    BIND(attach_ball, "_Z13AI_AttachBallP7AI_BALLP12AI_NBA_ACTOR"),
    BIND(ball_caught, "_Z14EVT_BallCaughtP8AI_ACTORP7AI_BALLj"),
    BIND(deflected_off_player, "_Z26EVT_BallDeflectedOffPlayerP7AI_BALLP12AI_NBA_ACTOR24EVT_BALL_DEFLECTION_TYPE5_boolS4_S4_"),
    BIND(play_state, "_Z16REF_GetPlayStatev"),
    BIND_OPT(home_team, "gAi_HomeTeam"),
    BIND_OPT(away_team, "gAi_AwayTeam"),
    BIND_OPT(game_data_items, "GameData_Items"),
};

/* Returns 0 on success, otherwise 1 + index of the first missing required binding. */
static int game_bind(const ElfModule *m) {
    game_base = m->base;
    for (unsigned i = 0; i < COUNTOF(game_bindings); i++) {
        uintptr_t address = elf_symbol_address(m, game_bindings[i].name);
        *game_bindings[i].slot = (void *)address;
        if (!address && game_bindings[i].required) {
            LOGE("missing game symbol %s", game_bindings[i].name);
            return (int)i + 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ helpers */

static void *game_ball(void) { return G.ball_global ? *G.ball_global : NULL; }
static void *ball_owner(void *ball) { return ball ? FIELD(ball, BALL_OWNER, void *) : NULL; }

/* AI_NBA_ACTOR -> AI_PLAYER through the actor vtable (slot 8). */
static void *actor_player(void *actor) {
    if (!actor) return NULL;
    void **vtable = *(void ***)actor;
    if (!vtable || !vtable[8]) return NULL;
    return ((void *(*)(void *))vtable[8])(actor);
}

/* On-court players of a team list (gAi_HomeTeam/gAi_AwayTeam +0xa0, by position; street 1v1/3v3 fill
 * fewer slots). */
static int team_has_player(void *team, void *player) {
    if (!team || !player) return 0;
    for (int i = 0; i < TEAM_SLOTS; i++)
        if (FIELD(team, TEAM_PLAYERS + i * 8, void *) == player) return 1;
    return 0;
}

static int on_court(void *player) {
    return team_has_player(G.home_team, player) || team_has_player(G.away_team, player);
}

/* Which side the user plays for. Neither the player's team pointer nor the game's tune index is stable:
 * in 街头 half-court games the players swap AI_TEAM objects on every change of possession (seen on
 * device: the user's player moved from the away to the home object, and its tune index from 0 to 1), so
 * everything keyed on "我方/电脑" - sliders, hot zones, on fire, takeover, super CPU - swapped sides
 * mid-game. The on-court lists of gAi_HomeTeam/gAi_AwayTeam stay put, so sides are decided by list
 * membership; the user's list is the one holding a controlled player (update_human_team). */
static char *g_human_list;      /* G.home_team or G.away_team */
static void *g_human_player;    /* the controlled player seen last */

/* 0 = user side, 1 = CPU side, -1 = unknown. */
static int team_of(void *player) {
    if (!player) return -1;
    if (g_human_list) {
        char *other = g_human_list == G.home_team ? G.away_team : G.home_team;
        if (team_has_player(g_human_list, player)) return 0;
        if (other && team_has_player(other, player)) return 1;
    }
    void *team = FIELD(player, ACTOR_TEAM, void *);     /* off court, or no user seen yet */
    if (!team) return -1;
    void *human_team = g_human_player ? FIELD(g_human_player, ACTOR_TEAM, void *) : NULL;
    if (human_team) return team == human_team ? 0 : 1;
    int index = G.tune_index(team);
    return index == 0 || index == 1 ? index : -1;
}

/* TuneData column the game reads for the user's team right now (it follows the team object the user's
 * player currently belongs to); 0 when unknown. */
static int human_tune_slot(void) {
    void *team = g_human_player ? FIELD(g_human_player, ACTOR_TEAM, void *) : NULL;
    return team && G.tune_index(team) == 1 ? 1 : 0;
}

/* The user's own player on a profile-controlled team (辉煌生涯 and other "one player per user" modes).
 * Read from the profile Profile_IsUserProfileControlled consults; NULL unless it names an on-court player
 * (never dereferenced before that check). */
static void *profile_player(void *team) {
    if (!team || !G.home_team) return NULL;
    uintptr_t profile = game_base + OFF_PROFILES + (team == (void *)G.home_team ? 0 : PROFILE_STRIDE);
    void *player = FIELD(profile, PROFILE_PLAYER, void *);
    return on_court(player) ? player : NULL;
}

/* The player the user is driving right now. On profile-controlled teams the game also reports the
 * team's current ball handler as "user controlled" (Profile_IsUserProfileControlled), although that
 * teammate is AI-driven - so in 生涯 the boosts meant for 所控球员 leaked to AI teammates holding the
 * ball. There only the profile's own player counts. */
static int is_controlled(void *player) {
    if (!player) return 0;
    if (!G.is_user(player)) return G.controller_id(player) >= 0;
    /* Only the ball handler is ambiguous: for anyone else (a blocker, a stealer, an off-ball cutter) the
     * game's answer already means "the profile's own player", so defence never depends on the profile
     * layout below. */
    void *owner = ball_owner(game_ball());
    if (!owner || actor_player(owner) != player) return 1;
    void *own = profile_player(FIELD(player, ACTOR_TEAM, void *));
    return own ? own == player : 1;   /* unknown profile layout: trust the game */
}

static int is_user_team(void *player) { return team_of(player) == 0; }

/* Once per game frame: the list holding an on-court player the user controls. Kept when nobody is
 * controlled for a moment (timeout, replay, 生涯 player on the bench); cleared on a new session. */
static void update_human_team(void) {
    char *lists[2] = {G.home_team, G.away_team};
    for (int t = 0; t < 2; t++) {
        if (!lists[t]) continue;
        for (int i = 0; i < TEAM_SLOTS; i++) {
            void *player = FIELD(lists[t], TEAM_PLAYERS + i * 8, void *);
            if (player && is_controlled(player)) {
                g_human_list = lists[t];
                g_human_player = player;
                return;
            }
        }
    }
}

/* Live ball (REF_SetNewState starts in-play logic on state 10; 8 = change of possession, 18 = whistle). */
static int ball_in_play(void) { return G.play_state() == PLAY_STATE_LIVE; }

static const float *ball_position(void *ball) {
    char *prop = ball ? FIELD(ball, BALL_PROP, char *) : NULL;
    return prop ? (const float *)(prop + 0x80) : NULL;
}

/* Hand collision spheres of an actor (left 0x0e / right 0x14, world space), the same spheres the game's
 * grab-block catch test in MVS_HandleBlockToken1 uses. Returns how many valid hands were found. */
typedef struct { const float *center; float radius; } HandSphere;
static int actor_hands(void *actor, HandSphere hands[2]) {
    char *collision = actor ? FIELD(actor, ACTOR_COLLISION, char *) : NULL;
    char *list = collision ? FIELD(collision, 0x108, char *) : NULL;
    int count = list ? FIELD(list, 0, int) : 0;
    char *spheres = list ? FIELD(list, 8, char *) : NULL;
    if (!spheres || count <= 0 || count > 128) return 0;
    int found = 0;
    for (int i = 0; i < count && found < 2; i++) {
        char *sphere = spheres + i * 0x20;
        int node = FIELD(sphere, 0x14, int);
        if (node != 0x0e && node != 0x14) continue;
        float radius = FIELD(sphere, 0x10, float);
        const float *center = (const float *)sphere;
        if (!finite_in(radius, 0.01f, 40.0f) || !finite_in(center[1], -1000.0f, 3000.0f)) continue;
        hands[found].center = center;
        hands[found].radius = radius;
        found++;
    }
    return found;
}

/* Distance from the ball centre to the nearest hand surface, in cm (1e9 when unknown). */
static float hand_ball_gap(void *actor, const float *ball) {
    HandSphere hands[2];
    int n = ball ? actor_hands(actor, hands) : 0;
    float best_sq = 1.0e18f, best_radius = 0.0f;
    for (int i = 0; i < n; i++) {
        float dx = ball[0] - hands[i].center[0], dy = ball[1] - hands[i].center[1], dz = ball[2] - hands[i].center[2];
        float d = dx * dx + dy * dy + dz * dz;
        if (d < best_sq) { best_sq = d; best_radius = hands[i].radius; }
    }
    if (!n || !(best_sq < 1.0e17f)) return 1.0e9f;
    return sqrt_rt(best_sq) - best_radius;
}

/* Height (world y) of the higher hand; -1e9 when unknown. */
static float highest_hand_y(void *actor) {
    HandSphere hands[2];
    int n = actor_hands(actor, hands);
    float y = -1.0e9f;
    for (int i = 0; i < n; i++) if (hands[i].center[1] > y) y = hands[i].center[1];
    return y;
}

/* xorshift32; only touched from game threads, races are harmless for randomness. */
static unsigned rng_state = 0x6b2f9a31u;
static unsigned rng_next(void) {
    unsigned x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x ? x : 0x6b2f9a31u;
    return rng_state;
}
/* True with probability percent/100 (0 never, 100 always). */
static int roll_percent(int percent) {
    if (percent <= 0) return 0;
    if (percent >= 100) return 1;
    return (int)(rng_next() % 100u) < percent;
}

static unsigned float_bits(float value) {
    union { float f; unsigned u; } bits = { .f = value };
    return bits.u;
}

static uintptr_t current_thread(void) { return (uintptr_t)__builtin_thread_pointer(); }
