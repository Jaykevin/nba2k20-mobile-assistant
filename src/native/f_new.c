/* New features: grab blocks (抓帽), steal threshold (+ keep the ball), hot zones, on-fire streak,
 * takeover, highlight slow-motion.
 *
 * Grab block: every deflecting block runs MVS_HandleBlockCollisionBlockToken -> PHY_DeflectShot. Grab
 * animations later run MVS_HandleBlockCollisionCatchToken, which attaches the loose ball to the
 * blocker. For the human-controlled blocker we (a) open the grab-animation gate with probability p so
 * real grab animations get picked more often, and (b) after any other deflection, run the game's own
 * catch step with probability p. Result: catch rate >= p, 100% = every block is caught. */
#include "rt.h"

/* ------------------------------------------------------------------ highlight slow motion */
static double slowmo_last;

static void slowmo_trigger(int kind) {
    if (!cfg(CFG_SLOWMO) || !cfg(kind)) return;
    if (G.speed_interrupt_active()) return;          /* already slowed (or the game froze time) */
    double now = now_seconds();
    if (now - slowmo_last < 0.4) return;
    slowmo_last = now;
    float speed = (float)cfg(CFG_SLOWMO_SPEED) / 100.0f;
    float duration = (float)cfg(CFG_SLOWMO_TIME) / 10.0f;
    /* (target speed, total duration, blend-out, blend-in) - same mechanism as the Dunk Contest replays */
    G.speed_interrupt(speed, duration, 0.35f, 0.12f);
    stat_add(ST_SLOWMO_COUNT);
}

static void (*orig_dunk_launched)(void *player, float time);
static void hook_dunk_launched(void *player, float time) {
    orig_dunk_launched(player, time);
    if (is_controlled(player)) slowmo_trigger(CFG_SLOWMO_DUNK);
}

/* ------------------------------------------------------------------ deferred catch (steals) */
static struct { void *actor, *ball; unsigned frame; } pending_catch;

static void queue_catch(void *actor, void *ball) {
    pending_catch.ball = ball;
    pending_catch.frame = g_frame;
    pending_catch.actor = actor;
}

/* Runs at the start of the next game frame, after the steal code finished with the loose ball. */
static void process_pending_catch(void) {
    void *actor = pending_catch.actor;
    if (!actor) return;
    pending_catch.actor = NULL;
    if (g_frame - pending_catch.frame > 20u) return;
    void *ball = game_ball();
    if (!ball || ball != pending_catch.ball || ball_owner(ball)) return;   /* someone already has it */
    if (!is_controlled(actor_player(actor)) || !ball_in_play()) return;   /* no catch after a whistle */
    G.ball_caught(actor, ball, 0);   /* the game's own catch event: attach + possession bookkeeping */
    if (ball_owner(ball) == actor) {
        stat_add(ST_STEAL_CATCHES);
        LOGI("steal: ball secured by controlled player");
    }
}

/* ------------------------------------------------------------------ grab block
 * Every block the game records goes through EVT_BallBlocked:
 *   - jump-shot swats: ball vs. hand physics (COL_DoBallPlayerCollisions -> EVT_BallDeflectedOffPlayer)
 *   - layup / dunk block collisions: MVS_HandleBlockCollisionBlockToken -> PHY_DeflectShot
 *   - grab animations: MVS_HandleBlockToken1 (catches by itself only if a hand touches the ball)
 *   - our airborne assist: PHY_DeflectShot
 * The old build only caught after the block-collision token, so ordinary swats were never grabbed.
 * The deflection code keeps working on the ball after the event returns, so the catch runs at the start
 * of the next game frame through EVT_BallCaught - the same call a natural grab block makes (attach +
 * possession/stat bookkeeping) - and only while the ball is live and still loose. A grab animation gets
 * a short grace period to make the catch itself. */
#define GRAB_GRACE_FRAMES 18u
#define GRAB_GIVE_UP_FRAMES 45u

/* 抓帽 probability honouring the master switch (0 = off, original game). */
static int grab_pct(void) { return cfg(CFG_GRAB_ON) ? cfg(CFG_GRAB_PCT) : 0; }

static struct { void *player, *ball; unsigned frame; int force; } pending_grab;
static struct { void *player, *ball; unsigned frame; } last_block;
static unsigned long log_grabs, log_grab_results;
static int super_grabbing;   /* 超级抓帽 is running its own block + catch right now */

/* Device test only (build with -DGRAB_TEST_ANY_BLOCKER): every blocker, CPU included, goes through the
 * grab path, so the catch can be verified with the CPU blocking the tester's shots. Never shipped. */
#ifdef GRAB_TEST_ANY_BLOCKER
#define grab_eligible(player) ((player) != NULL)
#else
#define grab_eligible(player) is_controlled(player)
#endif

static void on_block(void *player, void *ball, int source) {
    if (!player || !ball || !grab_eligible(player)) return;
    /* one block can be reported by more than one path in the same frame */
    if (last_block.player == player && last_block.ball == ball && g_frame - last_block.frame <= 2u) return;
    last_block.player = player;
    last_block.ball = ball;
    last_block.frame = g_frame;
    stat_add(ST_HUMAN_BLOCKS);
    slowmo_trigger(CFG_SLOWMO_BLOCK);
    int pct = grab_pct();
    int natural = G.doing_grab_block(player);
    int force = pct > 0 && roll_percent(pct);
    LOG_LIMITED(log_grabs, 16, "block by controlled player: source=%d state=%d ball_mode=%u grab=%d%% grab_anim=%d",
                source, G.play_state(), FIELD(ball, BALL_MODE, unsigned), pct, natural);
    if (super_grabbing) return;   /* 超级抓帽 catches the ball itself, right after this event */
    if (!force && !natural) return;
    pending_grab.ball = ball;
    pending_grab.frame = g_frame;
    pending_grab.force = force;
    pending_grab.player = player;
}

/* Start of each game frame. */
static void process_pending_grab(void) {
    void *player = pending_grab.player;
    if (!player) return;
    unsigned age = g_frame - pending_grab.frame;
    void *ball = game_ball();
    void *owner = ball_owner(ball);
    const char *result;
    if (ball != pending_grab.ball) result = "ball changed";
    else if (owner == player) { stat_add(ST_GRAB_NATURAL); result = "caught by the grab animation"; }
    else if (owner) { stat_add(ST_GRAB_LOST); result = "someone else got the ball first"; }
    else if (age > GRAB_GIVE_UP_FRAMES || !grab_eligible(player)) result = "expired";
    else if (!ball_in_play()) result = "whistle - ball dead";
    /* A forced grab is taken on the very next frame: waiting for a grab animation's own (later) catch
     * token left the loose ball to whoever reached it first - one of the 生涯 misses at 100%. Unforced
     * (probability roll lost) the animation may still catch by itself. */
    else if (!pending_grab.force && age < GRAB_GRACE_FRAMES && G.doing_grab_block(player)) return;
    else if (!pending_grab.force) result = "grab animation missed (probability roll not won)";
    else {
        G.ball_caught(player, ball, 0);
        if (ball_owner(ball) == player) { stat_add(ST_GRAB_FORCED); result = "caught"; }
        else { stat_add(ST_GRAB_LOST); result = "catch refused"; }
    }
    LOG_LIMITED(log_grab_results, 16, "grab: %s (after %u frames)", result, age);
    pending_grab.player = NULL;
}

static void (*orig_ball_blocked)(void *ball, void *player, const float *position, int type, int a, int b);
static void hook_ball_blocked(void *ball, void *player, const float *position, int type, int a, int b) {
    orig_ball_blocked(ball, player, position, type, a, b);
    if (player && team_of(player) == 0 && !is_controlled(player)) stat_add(ST_BLOCK_TEAMMATE);
    on_block(player, ball, 1);
}

/* Fallback: EVT_BallBlocked sets BALL_BLOCKED itself, so a flag that appeared during this block-collision
 * token is a block even if the event hook were missing; a flag left from an earlier block is ignored. */
static void (*orig_block_token)(void *actor);
static void hook_block_token(void *actor) {
    char *ball = game_ball();
    unsigned before = ball ? FIELD(ball, BALL_FLAGS, unsigned) & BALL_BLOCKED : BALL_BLOCKED;
    orig_block_token(actor);
    if (ball && !before && (FIELD(ball, BALL_FLAGS, unsigned) & BALL_BLOCKED)) on_block(actor_player(actor), ball, 2);
}

/* A grab animation only catches if a hand is inside the ball on this exact frame; otherwise nothing
 * happens at all - not even a block (common with 弹跳增强, which moves the hands away from where the
 * animation expects them). For the controlled player a near miss is accepted: the same sequence the game
 * runs on a hit (deflection event -> attach -> catch event). */
static float grab_slack(void) {
    float slack = cfg(CFG_AIR_BLOCK) ? (float)cfg(CFG_BLOCK_REACH) * BLOCK_REACH_TO_HAND : 0.0f;
    return slack > 35.0f ? slack : 35.0f;
}

static void (*orig_block_token1)(void *actor);
static void hook_block_token1(void *actor) {
    orig_block_token1(actor);
    if (grab_pct() <= 0) return;
    void *player = actor_player(actor);
    if (!player || !is_controlled(player) || !G.doing_grab_block(player)) return;
    char *ball = game_ball();
    if (!ball || ball_owner(ball) || FIELD(ball, BALL_MODE, unsigned) != 3) return;   /* caught, or no shot */
    if (FIELD(ball, BALL_FLAGS, unsigned) & BALL_BLOCKED) return;
    void *shooter = FIELD(ball, BALL_SHOOTER, void *);
    int shooter_team = team_of(shooter), team = team_of(player);
    if (shooter_team < 0 || team < 0 || shooter_team == team) return;
    float gap = hand_ball_gap(player, ball_position(ball));
    if (!(gap <= grab_slack()) || G.is_goaltend(ball, player)) {
        stat_add(ST_GRAB_WHIFF);
        LOG_LIMITED(log_grabs, 16, "grab animation whiffed (hand gap %.0f cm, allowed %.0f)", (double)gap,
                    (double)grab_slack());
        return;
    }
    G.deflected_off_player(ball, player, 1, 0, 0, 0);   /* records the block (-> EVT_BallBlocked) */
    if (ball_owner(ball) || !ball_in_play()) return;
    G.attach_ball(ball, player);
    G.ball_caught(player, ball, 0);
    stat_add(ST_GRAB_RESCUED);
    LOG_LIMITED(log_grabs, 16, "grab animation near miss rescued (hand gap %.0f cm)", (double)gap);
}

/* ------------------------------------------------------------------ 超级抓帽 (whole court) */
/* Every opponent shot ends in the user's hands wherever the user's player stands: jump shots, layups and
 * tip-ins on the first frame the ball is in flight, dunks as soon as the dunk is under way (dunk physics
 * running with the ball still in hand). Each one runs the game's own block-and-catch sequence - the same
 * calls a natural grab block makes - so it is credited as the user's block, possession changes the
 * normal way and nothing happens during a whistle. Runs at the start of a game frame, outside the
 * animation/physics code that moves the ball. */
static unsigned long log_super;

static void super_grab_take(void *me, char *ball, int in_flight) {
    super_grabbing = 1;
    if (in_flight) {
        /* a ball a teammate already blocked, or one above the rim on its way down, is just caught: no
         * second block, and no deflection event that the referee would call goaltending */
        if (!(FIELD(ball, BALL_FLAGS, unsigned) & BALL_BLOCKED) && !G.is_goaltend(ball, me))
            G.deflected_off_player(ball, me, 1, 0, 0, 0);
    } else {
        pending_forced = 0;
        pending_ball = NULL;
        deflection_thread = current_thread();   /* our own deflection: no forced-shot handling */
        G.deflect_shot(me, ball);               /* knocks the ball out of the dunker's hands (block) */
        deflection_thread = 0;
    }
    super_grabbing = 0;
    if (ball_owner(ball) != me) G.attach_ball(ball, me);   /* also takes it from a dunker still holding it */
    if (ball_owner(ball) != me) {
        LOG_LIMITED(log_super, 16, "super grab: attach refused");
        return;
    }
    G.ball_caught(me, ball, 0);
    stat_add(ST_SUPER_GRABS);
    LOG_LIMITED(log_super, 16, "super grab: %s taken", in_flight ? "shot" : "dunk");
}

/* Start of each game frame (after update_human_team). */
static void super_grab_tick(void) {
    if (!cfg(CFG_SUPER_GRAB) || !ball_in_play()) return;
    void *me = g_human_player;
    if (!me || !on_court(me) || !is_controlled(me)) return;
    char *ball = game_ball();
    if (!ball) return;
    void *owner = ball_owner(ball);
    if (owner) {
        void *holder = actor_player(owner);
        if (!holder || holder == me || team_of(holder) != 1) return;
        if (!G.is_shooting(owner) || !G.is_dunk_physics(owner)) return;   /* a dunk under way */
        super_grab_take(me, ball, 0);
        return;
    }
    if (FIELD(ball, BALL_MODE, unsigned) != 3) return;                    /* shot in flight, untouched */
    void *shooter = FIELD(ball, BALL_SHOOTER, void *);
    if (!shooter || team_of(shooter) != 1) return;                        /* opponents' shots only */
    super_grab_take(me, ball, 1);
}

/* Opening the grab-animation gate (Takeover_CanPlayerDoGrabBlocks) makes the game pick two-hand grab
 * animations instead of swats. They look best but catch only on an exact hand/ball contact, so when the
 * timing is off the block whiffs completely; that is opt-in now (优先使用游戏自带抓帽动作). Without it
 * the game swats as usual and every block the user's player makes is still caught by on_block. The game
 * ignores this gate in 街头 half-court games (game type 8: grab animations by rating only). */
static int (*orig_grab_gate)(void *player);
static int hook_grab_gate(void *player) {
    int pct = cfg(CFG_GRAB_ANIM) ? grab_pct() : 0;
    if (pct > 0 && is_controlled(player) && roll_percent(pct)) {
        stat_add(ST_GRAB_GATE);
        return 1;
    }
    return orig_grab_gate(player);
}

/* ------------------------------------------------------------------ steals */
static float (*orig_steal_chance)(void *handler, void *defender, void *collision);
static float hook_steal_chance(void *handler, void *defender, void *collision) {
    float value = orig_steal_chance(handler, defender, collision);
    if (is_cpu_boosted(defender)) return value > .60f ? .60f : value;
    int pct = cfg(CFG_STEAL_PCT);
    if (pct > 0 && is_controlled(defender)) {
        float floor = pct >= 100 ? 1.0f : (float)pct / 100.0f;
        if (!(value >= floor)) {
            stat_add(ST_STEAL_BOOSTS);
            return floor;
        }
    }
    return value;
}

static void (*orig_poked_loose)(void *defender, void *handler, void *ball);
static void hook_poked_loose(void *defender, void *handler, void *ball) {
    orig_poked_loose(defender, handler, ball);
    if (!is_controlled(defender)) return;
    stat_add(ST_STEAL_POKES);
    slowmo_trigger(CFG_SLOWMO_STEAL);
    if (cfg(CFG_STEAL_PCT) > 0 && cfg(CFG_STEAL_KEEP)) queue_catch(defender, ball);
}

static void (*orig_pass_stolen)(void *player);
static void hook_pass_stolen(void *player) {     /* player = the interceptor */
    orig_pass_stolen(player);
    if (is_controlled(player)) slowmo_trigger(CFG_SLOWMO_STEAL);
}

/* ------------------------------------------------------------------ hot zones / on fire */
#define HEAT_HOT 2
#define STREAK_ON_FIRE 4

static int (*orig_hot_heat)(void *player, int zone);
static int hook_hot_heat(void *player, int zone) {
    if (cfg(CFG_HOT_ZONES) && zone >= 1 && zone <= 14 && is_user_team(player)) {
        stat_add(ST_HOT_CALLS);
        return HEAT_HOT;
    }
    return orig_hot_heat(player, zone);
}

static int (*orig_streak_state)(void *data);
static int hook_streak_state(void *data) {
    if (cfg(CFG_ON_FIRE) && data) {
        void *player = FIELD(data, PLAYERDATA_AI_PLAYER, void *);
        if (player && is_user_team(player)) return STREAK_ON_FIRE;
    }
    return orig_streak_state(data);
}

/* Evaluates the game's own modifier table as if the player were on fire. */
static float (*orig_streak_modifier)(void *player, int attribute, float value);
static float hook_streak_modifier(void *player, int attribute, float value) {
    if (!cfg(CFG_ON_FIRE) || !player || !is_user_team(player)) return orig_streak_modifier(player, attribute, value);
    int *state = &FIELD(player, PLAYER_STREAK, int);
    int saved = *state;
    *state = STREAK_ON_FIRE;
    float result = orig_streak_modifier(player, attribute, value);
    *state = saved;
    stat_add(ST_FIRE_CALLS);
    return result;
}

/* ------------------------------------------------------------------ takeover (experimental) */
#define TAKEOVER_ACTIVE 2

static int (*orig_takeover_state)(void *player, int flag);
static int hook_takeover_state(void *player, int flag) {
    if (cfg(CFG_TAKEOVER) && is_user_team(player)) {
        stat_add(ST_TAKEOVER_CALLS);
        return TAKEOVER_ACTIVE;
    }
    return orig_takeover_state(player, flag);
}

static int (*orig_team_takeover)(void *player);
static int hook_team_takeover(void *player) {
    if (cfg(CFG_TAKEOVER) && is_user_team(player)) return 1;
    return orig_team_takeover(player);
}
