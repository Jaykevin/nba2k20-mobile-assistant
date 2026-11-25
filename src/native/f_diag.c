/* Diagnostics for per-mode behaviour (辉煌生涯, 街头 1v1/3v3, quick games): who the module considers
 * controlled, team sides, play state and player heights - logged under the 2K20Assist tag only. */
#include "rt.h"

static unsigned diag_logs, diag_next_frame;

static int input_controller(void *player) {
    char *input = FIELD(player, ACTOR_INPUT, char *);
    return input ? FIELD(input, 0, int) : -99;
}

static void diag_player(const char *side, int slot, void *player) {
    void *team = FIELD(player, ACTOR_TEAM, void *);
    char *prop = FIELD(player, ACTOR_PROP, char *);
    struct JumpProfile *jump = find_jump(player);
    LOGI("diag %s%d %p side=%d raw_tune=%d ctl=%d input=%d user=%d profile=%d controlled=%d y=%.1f air=%d jump=%.0f",
         side, slot, player, team_of(player), team ? G.tune_index(team) : -1, G.controller_id(player),
         input_controller(player), G.is_user(player),
         profile_player(team) == player, is_controlled(player), prop ? (double)FIELD(prop, PROP_HEIGHT, float) : -1.0,
         actor_in_air(player), jump ? (double)jump->applied : -1.0);
}

static void diag_snapshot(const char *why) {
    int type = G.game_data_items ? FIELD(G.game_data_items, GAME_DATA_GAME_TYPE, int) : -1;
    LOGI("diag [%s] mode=%d game_type=%d play_state=%d career=%p frame=%u ball_owner=%p", why, G.game_mode(),
         type, G.play_state(), G.career_player(), g_frame, ball_owner(game_ball()));
    /* raw profile slots (static data inside the game library), to confirm the layout per mode */
    LOGI("diag profiles: home=%p away=%p | user team=%s tune slot=%d",
         FIELD(game_base + OFF_PROFILES, PROFILE_PLAYER, void *),
         FIELD(game_base + OFF_PROFILES + PROFILE_STRIDE, PROFILE_PLAYER, void *),
         !g_human_list ? "unknown" : g_human_list == G.home_team ? "home" : "away", human_tune_slot());
    char *teams[2] = {G.home_team, G.away_team};
    for (int t = 0; t < 2; t++) {
        if (!teams[t]) continue;
        for (int i = 0; i < TEAM_SLOTS; i++) {
            void *player = FIELD(teams[t], TEAM_PLAYERS + i * 8, void *);
            if (player) diag_player(t ? "away" : "home", i, player);
        }
    }
}

static int diag_last_state = -1;
static unsigned long log_states;

/* Once per game frame. */
static void diag_tick(void) {
    int state = G.play_state();
    stat_set(ST_DIAG_STATE, (unsigned long)state);
    stat_set(ST_DIAG_USER, (unsigned long)((!g_human_list ? 0 : g_human_list == G.home_team ? 10 : 20)
                                          + human_tune_slot()));
    void *owner = ball_owner(game_ball());
    void *holder = owner ? actor_player(owner) : NULL;
    int who = !holder ? 0 : is_controlled(holder) ? 1 : team_of(holder) == 0 ? 2 : 3;
    void *human = g_human_player;
    int profile = !human || !G.is_user(human) ? 0 : profile_player(FIELD(human, ACTOR_TEAM, void *)) ? 1 : 2;
    stat_set(ST_DIAG_OWNER, (unsigned long)(who + 10 * profile));
    if (state != diag_last_state) {
        LOG_LIMITED(log_states, 40, "play state %d -> %d (frame %u, ball owner %p)", diag_last_state, state,
                    g_frame, ball_owner(game_ball()));
        diag_last_state = state;
    }
    if (diag_logs < 10 && state == PLAY_STATE_LIVE && g_frame >= diag_next_frame) {
        if (!diag_logs) stat_set(ST_DIAG_MODE, (unsigned long)G.game_mode());
        diag_snapshot(diag_logs ? "periodic" : "first live frame");
        diag_logs++;
        diag_next_frame = g_frame + 900;
    }
}

static void diag_reset(void) {
    diag_logs = 0;
    diag_next_frame = 0;
    diag_last_state = -1;
}
