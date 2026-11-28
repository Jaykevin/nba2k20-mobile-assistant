"""Exercise the real native match-speed hook with a simulated game update."""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out' / 'speed-validation'
HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "gen_settings.h"
static int setting = 100;
static int cfg(int id) { assert(id == CFG_GAME_SPEED); return setting; }
static int finite_in(float v, float lo, float hi) { return v >= lo && v <= hi; }
#define LOGI(...) ((void)0)
#include "f_speed.c"

static float seen;
static int calls;
static void game_update(void *process) {
    calls++;
    seen = process ? FIELD(process, PROCESS_FRAME_TIME, float) : -1.0f;
}

int main(void) {
    float process[PROCESS_FRAME_TIME / sizeof(float) + 2] = {0};
    float *elapsed = &FIELD(process, PROCESS_FRAME_TIME, float);
    orig_game_update_common = game_update;
    assert(cfg_default[CFG_GAME_SPEED] == 100);
    assert(cfg_min[CFG_GAME_SPEED] == 50 && cfg_max[CFG_GAME_SPEED] == 200);
    const int speeds[] = {50, 75, 100, 125, 150, 200};
    for (unsigned i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
        setting = speeds[i];
        for (int frame = 0; frame < 120; frame++) {
            *elapsed = 0.016f;
            int before = calls;
            hook_game_update_common(process);
            assert(calls == before + 1);
            assert(fabsf(seen - 0.016f * speeds[i] / 100.0f) < 0.000001f);
            assert(*elapsed == 0.016f);
            assert(process[0] == 0.0f && process[sizeof(process) / sizeof(float) - 1] == 0.0f);
        }
    }
    setting = 200;
    const float invalid[] = {-1.0f, 0.5f, NAN, INFINITY};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        *elapsed = invalid[i];
        hook_game_update_common(process);
        assert(isnan(invalid[i]) ? isnan(seen) && isnan(*elapsed) : seen == invalid[i] && *elapsed == invalid[i]);
    }
    *elapsed = 0.0f;
    hook_game_update_common(process);
    assert(seen == 0.0f && *elapsed == 0.0f);
    hook_game_update_common(NULL);
    assert(seen == -1.0f);
    setting = 100;
    *elapsed = 0.016f;
    hook_game_update_common(process);
    assert(seen == 0.016f && *elapsed == 0.016f);
    puts("PASS: speed range, 720 frames, no accumulation, pause, invalid time, null process, restore");
    return 0;
}
'''


def main():
    config = json.loads((ROOT / 'local_config.json').read_text(encoding='utf-8'))
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'test_game_speed.c'
    source.write_text('#define PROCESS_FRAME_TIME 0x3354\n'
                      '#define FIELD(ptr, off, type) (*(type *)((char *)(ptr) + (off)))\n' + HARNESS,
                      encoding='utf-8')
    exe = OUT / 'test_game_speed.exe'
    subprocess.run([config['zig'], 'cc', '-O2', '-I', str(ROOT / 'src' / 'native'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
