/* Scale the match frame before AI, physics, clocks, camera and replay consume it. */
static void (*orig_game_update_common)(void *process);
static int speed_last = 100;

static void hook_game_update_common(void *process) {
    int percent = cfg(CFG_GAME_SPEED);
    if (!process || percent == 100) {
        if (process && speed_last != 100) {
            LOGI("match speed restored to 100%%");
            speed_last = 100;
        }
        orig_game_update_common(process);
        return;
    }
    float *elapsed = &FIELD(process, PROCESS_FRAME_TIME, float);
    float saved = *elapsed;
    if (!finite_in(saved, 0.0f, 0.25f)) {
        orig_game_update_common(process);
        return;
    }
    *elapsed = saved * ((float)percent / 100.0f);
    if (saved > 0.0f && speed_last != percent) {
        LOGI("match speed %d%%: frame %.6f -> %.6f s", percent, (double)saved, (double)*elapsed);
        speed_last = percent;
    }
    orig_game_update_common(process);
    /* The process frame time is also used outside the match update; never let scaling accumulate. */
    *elapsed = saved;
}
