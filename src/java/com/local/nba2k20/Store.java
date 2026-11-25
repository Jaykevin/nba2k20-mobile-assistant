package com.local.nba2k20;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Persistent settings. Uses the same preference file and keys as the previous build, so existing
 * settings (including the custom dunk combo and weights) carry over. Every write is pushed to native.
 */
final class Store {
    static final String FILE = "local_float_menu_v1";
    private final SharedPreferences prefs;

    Store(Context context) {
        prefs = context.getSharedPreferences(FILE, 0);
    }

    SharedPreferences prefs() { return prefs; }

    // ------------------------------------------------------------------ scalar settings
    int get(int id) {
        if (S.BOOL[id]) return prefs.getBoolean(S.KEY[id], S.DEF[id] != 0) ? 1 : 0;
        return clamp(id, prefs.getInt(S.KEY[id], S.DEF[id]));
    }

    boolean on(int id) { return get(id) != 0; }

    void set(int id, int value) {
        value = clamp(id, value);
        SharedPreferences.Editor editor = prefs.edit();
        if (S.BOOL[id]) editor.putBoolean(S.KEY[id], value != 0); else editor.putInt(S.KEY[id], value);
        editor.apply();
        Native.put(id, value);
        if (id == S.DUNK_MIX) pushDunkWeights();
    }

    private static int clamp(int id, int value) {
        return Math.max(S.MIN[id], Math.min(S.MAX[id], value));
    }

    // ------------------------------------------------------------------ fouls / violations
    boolean rule(int rule) { return prefs.getBoolean("rule_" + rule, false); }

    void setRule(int rule, boolean blocked) {
        prefs.edit().putBoolean("rule_" + rule, blocked).apply();
        Native.put(S.RULE_BASE + rule, blocked ? 1 : 0);
    }

    // ------------------------------------------------------------------ tune sliders (team 0 = 我方, 1 = 电脑)
    static int tuneMax(int key) { return key == 9 || key == 10 ? 200 : 100; }
    boolean tuneOn(int team, int key) { return prefs.getBoolean("t" + team + "_" + key + "_on", false); }
    int tuneValue(int team, int key) { return prefs.getInt("t" + team + "_" + key, 50); }

    void setTune(int team, int key, boolean enabled, int value) {
        value = Math.max(0, Math.min(tuneMax(key), value));
        prefs.edit().putBoolean("t" + team + "_" + key + "_on", enabled).putInt("t" + team + "_" + key, value).apply();
        Native.put(S.TUNE_BASE + team * S.TUNE_KEYS + key, enabled ? value : -1);
    }

    // ------------------------------------------------------------------ dunk combo (same semantics as before)
    boolean mixSelected(int index) { return prefs.getBoolean("dunk_mix_" + index, false); }
    int dunkWeight(int index) { return prefs.getInt("dunk_weight_" + index, 10); }

    int effectiveWeight(int index) {
        return on(S.DUNK_MIX) && !mixSelected(index) ? 0 : dunkWeight(index);
    }

    void pushDunkWeights() {
        for (int i = 0; i < DunkData.COUNT; i++) Native.put(S.DUNK_BASE + i, effectiveWeight(i));
    }

    void setDunkWeight(int index, int weight) {
        weight = Math.max(0, Math.min(100, weight));
        SharedPreferences.Editor editor = prefs.edit().putInt("dunk_weight_" + index, weight);
        if (weight == 0 && get(S.DUNK_LOCKED) == index) {
            editor.putInt(S.KEY[S.DUNK_LOCKED], -1);
            Native.put(S.DUNK_LOCKED, -1);
        }
        editor.apply();
        Native.put(S.DUNK_BASE + index, effectiveWeight(index));
    }

    void setMixSelected(int index, boolean selected) {
        SharedPreferences.Editor editor = prefs.edit().putBoolean("dunk_mix_" + index, selected);
        if (selected && dunkWeight(index) == 0) editor.putInt("dunk_weight_" + index, 10);
        editor.apply();
        Native.put(S.DUNK_BASE + index, effectiveWeight(index));
    }

    void setCategorySelected(int category, boolean selected) {
        SharedPreferences.Editor editor = prefs.edit();
        for (int i = 0; i < DunkData.COUNT; i++) {
            if (!DunkData.inCategory(i, category)) continue;
            editor.putBoolean("dunk_mix_" + i, selected);
            if (selected && dunkWeight(i) == 0) editor.putInt("dunk_weight_" + i, 10);
        }
        editor.apply();
        pushDunkWeights();
    }

    void setMixEnabled(boolean enabled) {
        if (enabled) {
            set(S.DUNK_LOCKED, -1);
            set(S.DUNK_VARIETY, 1);
            set(S.UNLOCK_DUNKS, 1);
        }
        set(S.DUNK_MIX, enabled ? 1 : 0);
    }

    void clearMix() {
        SharedPreferences.Editor editor = prefs.edit().putBoolean(S.KEY[S.DUNK_MIX], false);
        for (int i = 0; i < DunkData.COUNT; i++) editor.remove("dunk_mix_" + i);
        editor.apply();
        Native.put(S.DUNK_MIX, 0);
        pushDunkWeights();
    }

    /** Locks one action (temporarily leaves the combo). */
    void lockDunk(int index) {
        prefs.edit().putInt("dunk_weight_" + index, Math.max(1, dunkWeight(index))).apply();
        set(S.DUNK_MIX, 0);
        set(S.DUNK_VARIETY, 1);
        set(S.UNLOCK_DUNKS, 1);
        set(S.DUNK_LOCKED, index);
    }

    /** All actions, equal weights, unlocked, combo off (combo selection kept). */
    void uniformDunks() {
        SharedPreferences.Editor editor = prefs.edit();
        for (int i = 0; i < DunkData.COUNT; i++) editor.remove("dunk_weight_" + i);
        editor.apply();
        set(S.DUNK_LOCKED, -1);
        set(S.DUNK_VARIETY, 1);
        set(S.UNLOCK_DUNKS, 1);
        set(S.DUNK_MIX, 0);
    }

    int mixSelectedCount() {
        int count = 0;
        for (int i = 0; i < DunkData.COUNT; i++) if (mixSelected(i)) count++;
        return count;
    }

    int mixActiveCount() {
        int count = 0;
        for (int i = 0; i < DunkData.COUNT; i++) if (mixSelected(i) && dunkWeight(i) > 0) count++;
        return count;
    }

    // ------------------------------------------------------------------ bulk operations
    /** Pushes every stored setting to the native module (after it started). */
    void applyAll() {
        for (int id = 0; id < S.SCALAR_COUNT; id++) Native.put(id, get(id));
        for (int rule = 0; rule < S.RULES.length; rule++) Native.put(S.RULE_BASE + rule, rule(rule) ? 1 : 0);
        for (int team = 0; team < 2; team++)
            for (int key = 0; key < S.TUNE_KEYS; key++)
                Native.put(S.TUNE_BASE + team * S.TUNE_KEYS + key, tuneOn(team, key) ? tuneValue(team, key) : -1);
        pushDunkWeights();
    }

    /** Back to the original game: every feature off; the dunk combo selection and its weights are kept. */
    void restoreDefaults() {
        SharedPreferences.Editor editor = prefs.edit();
        for (int id = 0; id < S.SCALAR_COUNT; id++) {
            if (S.BOOL[id]) editor.putBoolean(S.KEY[id], S.DEF[id] != 0); else editor.putInt(S.KEY[id], S.DEF[id]);
        }
        for (int i = 0; i < DunkData.COUNT; i++) if (!mixSelected(i)) editor.remove("dunk_weight_" + i);
        for (int team = 0; team < 2; team++)
            for (int key = 0; key < S.TUNE_KEYS; key++)
                editor.remove("t" + team + "_" + key).remove("t" + team + "_" + key + "_on");
        for (int rule = 0; rule < S.RULES.length; rule++) editor.putBoolean("rule_" + rule, false);
        editor.apply();
        applyAll();
    }

    // ------------------------------------------------------------------ presets
    static final String[] PRESET_NAMES = {"街球狂欢", "防守大闸", "神射手", "地狱难度"};
    static final String[] PRESET_HINTS = {
        "高弹跳 + 8 米范围扣篮 + 慢镜头 + 晃倒 + 无限体力 + 提速",
        "抓帽 100% + 抢断 80% + 封盖判定放宽到手边 1 米 + 弹跳 1.5 米 + 防守能力拉满",
        "三分必中 + 全场热区 + 手感火热 + 投篮参数拉满",
        "超强人机 + 均衡进攻（只加强电脑）",
    };

    void applyPreset(int preset) {
        switch (preset) {
            case 0:
                set(S.JUMP_BOOST, 1); set(S.JUMP_HEIGHT, 160);
                set(S.FORCE_DUNK, 1); set(S.DUNK_RANGE, 800); set(S.CONTACT_DUNK, 1); set(S.DUNK_VARIETY, 1);
                set(S.SLOWMO, 1); set(S.SLOWMO_DUNK, 1); set(S.SLOWMO_BLOCK, 1); set(S.SLOWMO_STEAL, 1);
                set(S.ANKLE, 80);
                setTune(0, 13, true, 100); setTune(0, 14, true, 0); setTune(0, 15, true, 100);
                setTune(0, 9, true, 70); setTune(0, 10, true, 65);
                break;
            case 1:
                set(S.GRAB_ON, 1); set(S.GRAB_PCT, 100); set(S.STEAL_PCT, 80); set(S.STEAL_KEEP, 1);
                set(S.AIR_BLOCK, 1); set(S.BLOCK_REACH, 250); set(S.JUMP_BOOST, 1); set(S.JUMP_HEIGHT, 150);
                for (int key = 16; key <= 19; key++) setTune(0, key, true, 100);
                break;
            case 2:
                set(S.FORCE_THREE, 1); set(S.HOT_ZONES, 1); set(S.ON_FIRE, 1);
                for (int key = 0; key <= 4; key++) setTune(0, key, true, 100);
                break;
            case 3:
                set(S.SUPER_CPU, 1); set(S.CPU_THREE_BIAS, 50);
                break;
            default:
                break;
        }
    }
}
