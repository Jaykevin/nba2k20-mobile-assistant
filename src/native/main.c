/* NBA 2K20 assistant - native module (single translation unit).
 * Java entry points live in com.local.nba2k20.Native. */
#include "rt.h"
#include "elfhook.c"
#include "game.c"
#include "cfg.c"
#include "f_speed.c"
#include "f_basic.c"
#include "f_dunk.c"
#include "f_jump.c"
#include "f_cpu.c"
#include "f_new.c"
#include "f_diag.c"

/* ------------------------------------------------------------------ per-frame tick (game thread) */
static void (*orig_ai_update)(float dt);
static double last_tick_time;

static void new_session(void) {
    dunk_cache_reset();
    for (int i = 0; i < DUNK_ACTION_COUNT; i++) store_int(&g_dunk_available[i], 0);
    pending_catch.actor = NULL;
    pending_grab.player = NULL;
    last_block.player = NULL;
    g_human_list = NULL;
    g_human_player = NULL;
    jump_reset();
    diag_reset();
    LOGI("new game session");
}

static void hook_ai_update(float dt) {
    double now = now_seconds();
    if (now - last_tick_time > 30.0) new_session();
    last_tick_time = now;
    g_frame++;
    stat_set(ST_IN_GAME, g_frame);
    update_human_team();
    process_pending_catch();
    process_pending_grab();
    super_grab_tick();
    jump_tick();
    observe_cpu_possession();
    diag_tick();
    orig_ai_update(dt);
}

/* ------------------------------------------------------------------ hook table */
#define G_SHOTS 5
#define G_JUMP 7
#define G_CPU_BLOCK 8

static HookDef hooks[] = {
    H(0, "_Z17Game_UpdateCommonP16PROCESS_INSTANCE", hook_game_update_common, orig_game_update_common),
    /* per-frame tick */
    H(0, "_Z9AI_Updatef", hook_ai_update, orig_ai_update),
    /* sliders, movement, ankle breakers */
    H(0, "_Z32GameDataStore_GetTuneDataByIndexi", hook_tune_data, orig_tune_data),
    H(0, "_Z28PlayerMovement_GetSpeedScalePK9AI_PLAYERf18PLAYER_TURBO_STATE", hook_move_speed, orig_move_speed),
    H(0, "_ZNK15MVS_MOTION_MODE16GetTurnRateRatioEPK8AI_ACTOR", hook_turn_ratio, orig_turn_ratio),
    H(0, "_Z9MTH_Lerp3PK9MTH_LERP3f", hook_lerp, orig_lerp),
    H(0, "_ZNK16AI_BADGE_MANAGER30GetDefReactionChanceMultiplierEv", hook_badge, orig_badge),
    H(0, "_Z30Takeover_GetAnklebreakerEffectPK9AI_PLAYERS1_", hook_takeover_ankle, orig_takeover_ankle),
    /* fouls / violations (index == rule id) */
    H(0, "_Z16EVT_ReachingFoulP9AI_PLAYERS0_", rule_0, orig_rule_0),
    H(0, "_Z16EVT_ShootingFoulP9AI_PLAYERS0_5_boolS1_", rule_1, orig_rule_1),
    H(0, "_Z16EVT_ChargingFoulP9AI_PLAYERS0_", rule_2, orig_rule_2),
    H(0, "_Z16EVT_BlockingFoulP9AI_PLAYERS0_5_bool", rule_3, orig_rule_3),
    H(0, "_Z17EVT_LooseballFoulP9AI_PLAYERS0_", rule_4, orig_rule_4),
    H(0, "_Z19EVT_OverTheBackFoulP9AI_PLAYERS0_", rule_5, orig_rule_5),
    H(0, "_Z24EVT_DefensiveOffballFoulP9AI_PLAYERS0_", rule_6, orig_rule_6),
    H(0, "_Z19EVT_IntentionalFoulP9AI_PLAYERS0_5_bool", rule_7, orig_rule_7),
    H(0, "_Z17EVT_TechnicalFoulP12AI_NBA_ACTOR", rule_8, orig_rule_8),
    H(0, "_Z13EVT_TravelingP9AI_PLAYER", rule_9, orig_rule_9),
    H(0, "_Z17EVT_DoubleDribbleP9AI_PLAYER", rule_10, orig_rule_10),
    H(0, "_Z24EVT_ThreeSecondViolationP9AI_PLAYER", rule_11, orig_rule_11),
    H(0, "_Z25EVT_DefenseInKeyViolationP9AI_PLAYER", rule_12, orig_rule_12),
    H(0, "_Z22EVT_BackcourtViolationv", rule_13, orig_rule_13),
    H(0, "_Z24EVT_EightSecondViolationv", rule_14, orig_rule_14),
    H(0, "_Z22EVT_ShotClockViolationv", rule_15, orig_rule_15),
    H(0, "_Z23EVT_FiveSecondViolationP9AI_PLAYER", rule_16, orig_rule_16),
    H(0, "_Z35EVT_FiveSecondBackToBasketViolationP9AI_PLAYER", rule_17, orig_rule_17),
    H(0, "_Z27EVT_CloselyGuardedViolationP9AI_PLAYER", rule_18, orig_rule_18),
    H(0, "_Z12EVT_GoaltendP9AI_PLAYERP7AI_BALL", rule_19, orig_rule_19),
    H(0, "_Z14EVT_KickedBallP12AI_NBA_ACTORP7AI_BALL", rule_20, orig_rule_20),
    /* forced made shots: all four or none */
    H(G_SHOTS, "_Z22Phy_InitShotSimulationP8AI_ACTORP7AI_BALLPKDv4_ff19PHY_SHOT_TRAJECTORYj23MVS_SHOT_RELEASE_TIMING", hook_shot_init, orig_shot_init),
    H(G_SHOTS, "_Z34Phy_DetermineShotSimulationOutcomefP12AI_NBA_ACTORj", hook_shot, orig_shot),
    H(G_SHOTS, "_Z22Phy_GenerateShotOffsetPDv4_fPiPK35PHY_BALL_SHOT_SIMULATION_STATISTICS5_boolf", hook_shot_offset, orig_shot_offset),
    H(G_SHOTS, "_Z13Phy_StartShotP7AI_BALLPKDv4_f", hook_shot_start, orig_shot_start),
    /* dunks + finishing */
    H(0, "_Z31MVS_InitScoringMovePreAnimationP12AI_NBA_ACTOR", hook_scoring_pre, orig_scoring_pre),
    H(0, "_Z34AI_GetDistanceFromNBAActorToBasketPK12AI_NBA_ACTOR", hook_basket_distance, orig_basket_distance),
    H(0, "_Z17Mvs_CanPlayerDunkPK9AI_PLAYER5_boolPS2_", hook_can_dunk, orig_can_dunk),
    H(0, "_Z12MVS_FindDunkP9AI_PLAYERP15MVS_SHOT_PARAMSP17MVS_SHOT_PLAYBACK", hook_find_dunk, orig_find_dunk),
    H(0, "_Z22Mvs_IsDunkPackageValid23PLAYERDATA_DUNK_PACKAGEPK10PLAYERDATA5_bool", hook_dunk_valid, orig_dunk_valid),
    H(0, "_Z23HUR_CalculateDunkChanceP9AI_PLAYERRK26MVS_SHOT_SUCCESS_MODIFIERSfj", hook_dunk_chance, orig_dunk_chance),
    H(0, "_Z24HUR_CalculateLayupChanceP16HUR_SHOT_DEFENSEP9AI_PLAYER21MVS_SCORING_ANIM_TYPERK26MVS_SHOT_SUCCESS_MODIFIERSfj", hook_layup_chance, orig_layup_chance),
    /* jump physics: all or none */
    H(G_JUMP, "_Z25MVS_HandleBlockInAirTokenP12AI_NBA_ACTOR", hook_jump_in_air, orig_jump_in_air),
    H(G_JUMP, "_Z27MVS_HandleBlockLandingTokenP12AI_NBA_ACTORPK12ANM_CALLBACK", hook_jump_landing, orig_jump_landing),
    H(G_JUMP, "_Z30PHY_ApplyActorAnimationPhysicsP8AI_ACTORPK21ANM_ANIMATION_PHYSICS", hook_actor_physics, orig_actor_physics),
    H(G_JUMP, "_Z21ANM_SetPhysicsHandlerP12ANM_ANIMATORPFvP8AI_ACTORPK21ANM_ANIMATION_PHYSICSE", hook_set_physics, orig_set_physics),
    H(G_JUMP, "_Z26ANM_IsActivePhysicsHandlerPK12ANM_ANIMATORPFvP8AI_ACTORPK21ANM_ANIMATION_PHYSICSE", hook_is_physics, orig_is_physics),
    H(G_JUMP, "_Z28PHY_ApplyPhysicsAugmentationP21PHY_AUGMENTATION_DATAP21ANM_ANIMATION_PHYSICS", hook_jump_augmentation, orig_jump_augmentation),
    H(G_JUMP, "_Z20PHY_StartDunkPhysicsP12AI_NBA_ACTORfff", hook_start_dunk_physics, orig_start_dunk_physics),
    H(0, "_Z20AI_InitNBAActorScaleP12AI_NBA_ACTORf", hook_init_scale, orig_init_scale),
    /* super CPU */
    H(0, "_Z32AI_Roster_GetNormalizedAttributeP9AI_PLAYER20AI_ROSTER_ATTRIBUTES", hook_cpu_attribute, orig_cpu_attribute),
    H(G_CPU_BLOCK, "_Z30AI_TuneData_GetBlockShotChancePK9AI_PLAYER", hook_cpu_block_shot, orig_cpu_block_shot),
    H(G_CPU_BLOCK, "_Z35AI_TuneData_GetBlockDunkLayupChancePK9AI_PLAYER", hook_cpu_block_drive, orig_cpu_block_drive),
    H(G_CPU_BLOCK, "_ZN18VCRANDOM_GENERATOR21ComputeUniformDeviateEj", hook_cpu_uniform, orig_cpu_uniform),
    H(0, "_Z36PlayerData_GetDribbleNothingTendencyPK10PLAYERDATA", tend_nothing, orig_tend_nothing),
    H(0, "_Z38PlayerData_GetDribbleCrossoverTendencyPK10PLAYERDATA", tend_cross, orig_tend_cross),
    H(0, "_Z33PlayerData_GetDribbleSpinTendencyPK10PLAYERDATA", tend_spin, orig_tend_spin),
    H(0, "_Z39PlayerData_GetDribbleBehindBackTendencyPK10PLAYERDATA", tend_behind, orig_tend_behind),
    H(0, "_Z40PlayerData_GetDribbleDoubleCrossTendencyPK10PLAYERDATA", tend_double_cross, orig_tend_double_cross),
    H(0, "_Z37PlayerData_GetDribbleHalfSpinTendencyPK10PLAYERDATA", tend_half_spin, orig_tend_half_spin),
    H(0, "_Z37PlayerData_GetDribbleInAndOutTendencyPK10PLAYERDATA", tend_in_out, orig_tend_in_out),
    H(0, "_Z39PlayerData_GetDribbleHesitationTendencyPK10PLAYERDATA", tend_hesitation, orig_tend_hesitation),
    H(0, "_Z37PlayerData_GetDribbleStepBackTendencyPK10PLAYERDATA", tend_step_back, orig_tend_step_back),
    H(0, "_Z28PlayerData_GetPullUpTendencyPK10PLAYERDATA", tend_pull_up, orig_tend_pull_up),
    H(0, "_Z27PlayerData_GetDriveTendencyPK10PLAYERDATA", tend_drive, orig_tend_drive),
    H(0, "_Z31PlayerData_GetShotThreeTendencyPK10PLAYERDATA", tend_three, orig_tend_three),
    H(0, "_Z36PlayerData_GetStepbackJumperTendencyPK10PLAYERDATA", tend_step_jumper, orig_tend_step_jumper),
    H(0, "_Z26PlayerData_GetDunkTendencyPK10PLAYERDATA", tend_dunk, orig_tend_dunk),
    H(0, "_Z34PlayerData_GetFinishAttackTendencyPK10PLAYERDATA", tend_finish, orig_tend_finish),
    H(0, "_Z16BHV_GetShotValuePK9AI_PLAYERS1_P13BHV_SHOT_INFO", hook_cpu_shot_value, orig_cpu_shot_value),
    H(0, "_Z22BHV_UpdateDribbleMovesP9AI_PLAYER5angle5_bool", hook_cpu_dribble_update, orig_cpu_dribble_update),
    H(0, "_Z24Def_GuardManOnBallStanceP10DEF_STANCEP9AI_PLAYERS2_", hook_cpu_guard, orig_cpu_guard),
    H(0, "_Z18BHV_GetBlockTimingP9AI_PLAYERS0_", hook_cpu_block_timing, orig_cpu_block_timing),
    H(0, "_Z21PRC_GetBlockShotDelayP9AI_PLAYERPS0_PfPi", hook_block_delay, orig_block_delay),
    /* new: grab blocks, steals, hot zones, on fire, takeover, slow motion */
    H(0, "_Z15EVT_BallBlockedP7AI_BALLP9AI_PLAYERPKDv4_f14HIS_BLOCK_TYPE5_boolS7_", hook_ball_blocked, orig_ball_blocked),
    H(0, "_Z22MVS_HandleBlockToken1PP8AI_ACTOR", hook_block_token1, orig_block_token1),
    H(0, "_Z34MVS_HandleBlockCollisionBlockTokenP12AI_NBA_ACTOR", hook_block_token, orig_block_token),
    H(0, "_Z30Takeover_CanPlayerDoGrabBlocksPK9AI_PLAYER", hook_grab_gate, orig_grab_gate),
    H(0, "_Z33MVS_StealBallCalculateStealChanceP9AI_PLAYERS0_PK25COL_BALL_PLAYER_COLLISION", hook_steal_chance, orig_steal_chance),
    H(0, "_Z18EVT_BallPokedLooseP9AI_PLAYERS0_P7AI_BALL", hook_poked_loose, orig_poked_loose),
    H(0, "_Z14EVT_PassStolenP9AI_PLAYER", hook_pass_stolen, orig_pass_stolen),
    H(0, "_Z16EVT_DunkLaunchedP9AI_PLAYERf", hook_dunk_launched, orig_dunk_launched),
    H(0, "_Z18Hur_GetHotZoneHeatPK9AI_PLAYER8HOT_ZONE", hook_hot_heat, orig_hot_heat),
    H(0, "_Z15Streak_GetStatePK10PLAYERDATA", hook_streak_state, orig_streak_state),
    H(0, "_Z27Streak_GetAttributeModifierP9AI_PLAYER20AI_ROSTER_ATTRIBUTESf", hook_streak_modifier, orig_streak_modifier),
    H(0, "_Z26Takeover_GetEffectiveStatePK9AI_PLAYER5_bool", hook_takeover_state, orig_takeover_state),
    H(0, "_Z35Takeover_IsPlayerTeamTakeoverActiveP9AI_PLAYER", hook_team_takeover, orig_team_takeover),
};

/* ------------------------------------------------------------------ JNI */
static int g_ready, g_installed, g_defaults_done;

static void ensure_defaults(void) {
    if (!g_defaults_done) {
        cfg_defaults();
        g_defaults_done = 1;
    }
}

/* > 0: number of installed hooks. < 0: error (-1 game library not loaded yet, -100-n missing symbol n,
 * -3 unexpected game build, -4 nothing could be hooked). */
EXPORT int Java_com_local_nba2k20_Native_init(void *env, void *clazz) {
    if (g_ready) return g_installed;
    ensure_defaults();
    ElfModule module;
    if (!elf_open(&module, GAME_LIB)) {
        LOGW("game library not loaded yet");
        return -1;
    }
    int missing = game_bind(&module);
    if (missing) return -100 - missing;
    const float *curve = (const float *)(game_base + OFF_ANKLE_CURVE_A);
    if (!(curve[0] < curve[1] && curve[1] < curve[2] && curve[5] <= 1.0f)) {
        LOGE("unexpected game build (ankle curve check failed)");
        return -3;
    }
    shot_stats = (char *)(game_base + OFF_SHOT_STATS);
    rng_state ^= (unsigned)(now_seconds() * 1000003.0);
    int slots = hooks_install(&module, hooks, (int)COUNTOF(hooks));
    int installed = 0, failed = 0;
    for (unsigned i = 0; i < COUNTOF(hooks); i++) {
        if (hooks[i].installed) installed++;
        else {
            failed++;
            LOGW("hook unavailable: %s", hooks[i].symbol);
        }
    }
    LOGI("ready: %d/%d hooks (%d slots) in %s", installed, (int)COUNTOF(hooks), slots, module.path);
    stat_set(ST_HOOKS, (unsigned long)installed);
    stat_set(ST_HOOKS_FAILED, (unsigned long)failed);
    if (installed <= 0) return -4;
    g_installed = installed;
    g_ready = 1;
    stat_set(ST_READY, 1);
    return installed;
}

EXPORT void Java_com_local_nba2k20_Native_set(void *env, void *clazz, int id, int value) {
    ensure_defaults();
    cfg_set(id, value);
}

EXPORT long long Java_com_local_nba2k20_Native_stat(void *env, void *clazz, int id) {
    return stat_get(id);
}

EXPORT int Java_com_local_nba2k20_Native_hookCount(void *env, void *clazz) {
    return (int)COUNTOF(hooks);
}

/* ------------------------------------------------------------------ career player (改身高) */
#define PLAYERDATA_HEIGHT 0x18      /* float, centimetres (PlayerData_IncHeight / AI_GetPlayerHeight) */
#define CAREER_GET_HEIGHT 0
#define CAREER_SET_HEIGHT 1

/* Returns height in tenths of a cm; -1 no career player loaded, -2 module not ready, -3 unexpected data. */
EXPORT int Java_com_local_nba2k20_Native_career(void *env, void *clazz, int op, int arg) {
    if (!g_ready || !G.career_player) return -2;
    char *data = G.career_player();
    if (!data) return -1;
    float *height = &FIELD(data, PLAYERDATA_HEIGHT, float);
    if (!finite_in(*height, 100.0f, 300.0f)) return -3;     /* sanity check before touching the save */
    if (op == CAREER_GET_HEIGHT) return (int)(*height * 10.0f + 0.5f);
    if (op == CAREER_SET_HEIGHT) {
        int tenths = arg < 1500 ? 1500 : arg > 2450 ? 2450 : arg;
        float before = *height;
        *height = (float)tenths / 10.0f;
        LOGI("career height %.1f -> %.1f cm", (double)before, (double)*height);
        return tenths;
    }
    return -4;
}
