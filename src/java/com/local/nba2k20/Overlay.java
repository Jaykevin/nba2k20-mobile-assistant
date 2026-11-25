package com.local.nba2k20;

import android.app.Activity;
import android.graphics.PixelFormat;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.util.ArrayList;
import java.util.List;

/** Floating bubble + panel attached to the game activity window (no overlay permission needed). */
final class Overlay {
    private static final String[] NAV = {"快捷", "进攻", "防守", "人机", "趣味", "规则", "扣篮", "参数", "生涯"};

    final Activity activity;
    final Store store;
    final Ui ui;
    private final WindowManager windows;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final TextView bubble;
    private final WindowManager.LayoutParams bubbleLp;
    private final LinearLayout panel;
    private final WindowManager.LayoutParams panelLp;
    private final FrameLayout content;
    private final TextView status;
    private final TextView[] nav = new TextView[NAV.length];
    private final Page[] pages = new Page[NAV.length];
    private int current = -1;
    private boolean bubbleShown, panelShown, panelOpen, visible, destroyed;
    private int startAttempts;

    /** A cached page: its root view, value re-sync callbacks and live-stat callbacks. */
    static final class Page {
        View root;
        final List<Runnable> syncs = new ArrayList<>();
        final List<Runnable> stats = new ArrayList<>();
        void sync() { for (Runnable r : syncs) r.run(); }
        void refreshStats() { for (Runnable r : stats) r.run(); }
    }

    Overlay(Activity owner) {
        activity = owner;
        store = new Store(owner);
        ui = new Ui(owner);
        windows = (WindowManager) owner.getSystemService(Activity.WINDOW_SERVICE);

        bubble = ui.bold("2K", 16, Ui.ACCENT);
        bubble.setGravity(Gravity.CENTER);
        bubble.setBackgroundDrawable(ui.outlined(0xEE111720, 24, Ui.ACCENT));
        bubbleLp = params(ui.dp(48), ui.dp(48));
        bubbleLp.x = store.prefs().getInt("bubble_x", ui.dp(8));
        bubbleLp.y = store.prefs().getInt("bubble_y", ui.dp(56));
        bubble.setOnTouchListener(new Drag(bubbleLp, true));

        panel = ui.vertical();
        panel.setBackgroundDrawable(ui.outlined(Ui.PANEL, 14, 0x22FFFFFF));
        panelLp = params(ui.dp(600), ui.dp(340));

        LinearLayout header = ui.horizontal();
        header.setPadding(ui.dp(14), 0, ui.dp(6), 0);
        header.addView(ui.bold("2K20 助手", 16, Ui.TEXT), new LinearLayout.LayoutParams(-2, -2));
        header.addView(ui.text(" v" + S.VERSION, 11, Ui.MUTED), new LinearLayout.LayoutParams(-2, -2));
        status = ui.bold("", 11, Ui.MUTED);
        status.setPadding(ui.dp(8), ui.dp(3), ui.dp(8), ui.dp(3));
        LinearLayout.LayoutParams statusLp = new LinearLayout.LayoutParams(-2, -2);
        statusLp.setMargins(ui.dp(10), 0, 0, 0);
        header.addView(status, statusLp);
        header.addView(new View(owner), ui.weight(1, 1));
        TextView close = ui.bold("×", 22, Ui.MUTED);
        close.setGravity(Gravity.CENTER);
        close.setOnClickListener(v -> collapse());
        header.addView(close, new LinearLayout.LayoutParams(ui.dp(44), ui.dp(40)));
        header.setOnTouchListener(new Drag(panelLp, false));
        panel.addView(header, ui.fill(ui.dp(42)));

        LinearLayout body = ui.horizontal();
        body.setGravity(Gravity.TOP);
        ScrollView sideScroll = new ScrollView(owner);
        sideScroll.setVerticalScrollBarEnabled(false);
        LinearLayout side = ui.vertical();
        side.setPadding(ui.dp(6), ui.dp(2), ui.dp(6), ui.dp(8));
        for (int i = 0; i < NAV.length; i++) {
            final int index = i;
            nav[i] = ui.bold(NAV[i], 14, Ui.MUTED);
            nav[i].setGravity(Gravity.CENTER);
            nav[i].setOnClickListener(v -> select(index));
            LinearLayout.LayoutParams lp = ui.fill(ui.dp(38));
            lp.setMargins(0, 0, 0, ui.dp(3));
            side.addView(nav[i], lp);
        }
        sideScroll.addView(side);
        sideScroll.setBackgroundDrawable(ui.round(Ui.SIDEBAR, 10));
        LinearLayout.LayoutParams sideLp = new LinearLayout.LayoutParams(ui.dp(72), -1);
        sideLp.setMargins(ui.dp(8), 0, 0, ui.dp(8));
        body.addView(sideScroll, sideLp);
        content = new FrameLayout(owner);
        LinearLayout.LayoutParams contentLp = new LinearLayout.LayoutParams(0, -1, 1);
        contentLp.setMargins(ui.dp(8), 0, ui.dp(8), ui.dp(8));
        body.addView(content, contentLp);
        panel.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));

        startNative();
        select(0);
        owner.getWindow().getDecorView().post(this::show);
    }

    private WindowManager.LayoutParams params(int w, int h) {
        WindowManager.LayoutParams lp = new WindowManager.LayoutParams(w, h,
            WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN, PixelFormat.TRANSLUCENT);
        lp.gravity = Gravity.TOP | Gravity.LEFT;
        lp.setTitle("2K20 Assist");
        return lp;
    }

    // ------------------------------------------------------------------ native startup
    private void startNative() {
        int result = Native.start();
        if (result > 0) {
            store.applyAll();
        } else if (result == -1 && startAttempts++ < 30 && !destroyed) {
            handler.postDelayed(this::startNative, 1000);   // game library not loaded yet
        }
        updateStatus();
    }

    private void updateStatus() {
        int code = Native.status();
        long failed = Native.get(S.ST_HOOKS_FAILED);
        if (code > 0 && failed == 0) {
            status.setText("● 已就绪");
            status.setTextColor(Ui.GOOD);
        } else if (code > 0) {
            status.setText("● 部分可用 · " + failed + " 项未接入");
            status.setTextColor(Ui.WARN);
        } else if (code == -1) {
            status.setText("● 等待游戏加载");
            status.setTextColor(Ui.WARN);
        } else {
            status.setText("● 模块未加载 (" + code + ")");
            status.setTextColor(Ui.BAD);
        }
        status.setBackgroundDrawable(ui.round(0x22FFFFFF, 10));
    }

    // ------------------------------------------------------------------ pages
    void select(int index) {
        if (pages[index] == null) {
            pages[index] = new Page();
            pages[index].root = build(index, pages[index]);
        } else {
            pages[index].sync();
        }
        current = index;
        for (int i = 0; i < nav.length; i++) {
            boolean on = i == index;
            nav[i].setTextColor(on ? Ui.ACCENT : Ui.MUTED);
            nav[i].setBackgroundDrawable(on ? ui.round(Ui.ACCENT_SOFT, 8) : null);
        }
        content.removeAllViews();
        content.addView(pages[index].root, new FrameLayout.LayoutParams(-1, -1));
        pages[index].refreshStats();
    }

    private View build(int index, Page page) {
        if (index == 6) return new DunkPage(this, page).root();
        ScrollView scroll = new ScrollView(activity);
        scroll.setVerticalScrollBarEnabled(false);
        LinearLayout root = ui.vertical();
        scroll.addView(root, new ScrollView.LayoutParams(-1, -2));
        switch (index) {
            case 0: buildQuick(root, page); break;
            case 1: buildOffense(root, page); break;
            case 2: buildDefense(root, page); break;
            case 3: buildCpu(root, page); break;
            case 4: buildFun(root, page); break;
            case 5: buildRules(root, page); break;
            case 7: buildTune(root, page); break;
            case 8: buildCareer(root, page); break;
            default: break;
        }
        return scroll;
    }

    /** Re-reads every control on every built page (after presets / reset). */
    void syncAll() {
        for (Page p : pages) if (p != null) p.sync();
    }

    // ------------------------------------------------------------------ bound controls
    Ui.Toggle toggle(Page page, LinearLayout parent, String title, String help, final int id) {
        final Ui.Toggle t = ui.toggleRow(parent, title, help, store.on(id), on -> store.set(id, on ? 1 : 0));
        page.syncs.add(() -> t.setChecked(store.on(id), false));
        return t;
    }

    Ui.Slider slider(Page page, LinearLayout parent, String title, String help, final int id, int step, Ui.Format format) {
        final Ui.Slider s = ui.sliderRow(parent, title, help, S.MIN[id], S.MAX[id], step, store.get(id), format,
            v -> store.set(id, v));
        page.syncs.add(() -> s.set(store.get(id)));
        return s;
    }

    TextView statLine(Page page, LinearLayout parent, final StatText text) {
        final TextView view = ui.text("", 12, Ui.MUTED);
        view.setPadding(0, ui.dp(4), 0, ui.dp(2));
        parent.addView(view);
        page.stats.add(() -> view.setText(text.get()));
        return view;
    }

    interface StatText { String get(); }

    LinearLayout chips(LinearLayout parent) {
        LinearLayout row = ui.horizontal();
        LinearLayout.LayoutParams lp = ui.fill(ui.dp(32));
        lp.setMargins(0, ui.dp(4), 0, ui.dp(6));
        parent.addView(row, lp);
        return row;
    }

    TextView chip(LinearLayout row, String label, View.OnClickListener click) {
        TextView chip = ui.button(label, false);
        chip.setTextSize(12.5f);
        chip.setOnClickListener(click);
        LinearLayout.LayoutParams lp = ui.weight(1, -1);
        lp.setMargins(ui.dp(2), 0, ui.dp(2), 0);
        row.addView(chip, lp);
        return chip;
    }

    void toast(String message) { Toast.makeText(activity, message, Toast.LENGTH_SHORT).show(); }

    private static long st(int id) { return Native.get(id); }

    // ------------------------------------------------------------------ 快捷
    private void buildQuick(LinearLayout root, Page page) {
        LinearLayout state = ui.card(root, "状态");
        statLine(page, state, () -> {
            if (!Native.ready()) return "原生模块未就绪，功能暂不可用。重启游戏后再试。";
            return "已接入 " + st(S.ST_HOOKS) + "/" + Native.expectedHooks() + " 项 · "
                + (st(S.ST_IN_GAME) > 0 ? "已进入过比赛" : "等待进入比赛");
        });
        statLine(page, state, () -> "本次运行：盖帽 " + st(S.ST_HUMAN_BLOCKS) + " · 抓帽 "
            + (st(S.ST_GRAB_FORCED) + st(S.ST_GRAB_NATURAL) + st(S.ST_SUPER_GRABS)) + " · 断球 " + st(S.ST_STEAL_POKES)
            + " · 慢镜头 " + st(S.ST_SLOWMO_COUNT));
        statLine(page, state, () -> {   // troubleshooting line: quote it when something misbehaves
            long user = st(S.ST_DIAG_USER);
            String side = user >= 20 ? "客队" : user >= 10 ? "主队" : "未识别";
            long owner = st(S.ST_DIAG_OWNER);
            String[] holders = {"无人", "所控球员", "AI 队友", "对手"};
            String[] profiles = {"", " · 生涯档案已识别", " · 生涯档案未识别"};
            return "诊断：帧 " + st(S.ST_IN_GAME) + " · 状态 " + st(S.ST_DIAG_STATE) + " · 模式 " + st(S.ST_DIAG_MODE)
                + " · 我方=" + side + "（第 " + (user % 10) + " 列）· 球在" + holders[(int) (owner % 10) & 3]
                + profiles[(int) Math.min(2, owner / 10)];
        });

        LinearLayout presets = ui.card(root, "一键娱乐预设");
        for (int row = 0; row < 2; row++) {
            LinearLayout line = chips(presets);
            for (int col = 0; col < 2; col++) {
                final int preset = row * 2 + col;
                chip(line, Store.PRESET_NAMES[preset], v -> {
                    store.applyPreset(preset);
                    syncAll();
                    toast("已应用「" + Store.PRESET_NAMES[preset] + "」：" + Store.PRESET_HINTS[preset]);
                });
            }
        }
        LinearLayout resetRow = chips(presets);
        final TextView reset = chip(resetRow, "恢复原版（全部关闭）", null);
        reset.setOnClickListener(new View.OnClickListener() {
            private long armed;
            @Override public void onClick(View v) {
                long now = System.currentTimeMillis();
                if (now - armed > 3000) {
                    armed = now;
                    reset.setText("再点一次确认恢复");
                    handler.postDelayed(() -> reset.setText("恢复原版（全部关闭）"), 3000);
                    return;
                }
                armed = 0;
                store.restoreDefaults();
                syncAll();
                reset.setText("恢复原版（全部关闭）");
                toast("已恢复原版；扣篮组合与比重已保留");
            }
        });
        TextView hint = ui.text("预设在当前设置上叠加；恢复原版会关闭所有功能，保留你的扣篮组合。", 11.5f, Ui.MUTED);
        presets.addView(hint);

        LinearLayout common = ui.card(root, "常用");
        toggle(page, common, "抓帽", "总开关；关掉就完全是原版盖帽。概率和细节在「防守」页。", S.GRAB_ON);
        slider(page, common, "抓帽概率", GRAB_HELP, S.GRAB_PCT, 1, Ui::percent);
        toggle(page, common, "超级抓帽（全场）", SUPER_GRAB_HELP, S.SUPER_GRAB);
        toggle(page, common, "三分必中（所控球员）", null, S.FORCE_THREE);
        toggle(page, common, "高光慢镜头", "扣篮、盖帽、抢断时进入子弹时间，细节在「趣味」页调。", S.SLOWMO);
        toggle(page, common, "超强人机", "只加强电脑一方，详见「人机」页。", S.SUPER_CPU);
    }

    // ------------------------------------------------------------------ 进攻
    private void buildOffense(LinearLayout root, Page page) {
        LinearLayout shots = ui.card(root, "投篮");
        toggle(page, shots, "三分必中（含中场）", "所控球员的三分出手判定为命中并把球路对准篮心。被盖不算。", S.FORCE_THREE);
        toggle(page, shots, "全场投篮必中", "所控球员所有跳投必中。", S.FORCE_ALL);
        slider(page, shots, "上篮 / 扣篮命中下限", "所控球员上篮和扣篮的命中率至少为此值；100% 基本必进，被盖、被断照常。",
            S.FINISH_FLOOR, 1, Ui::percent);

        LinearLayout dunks = ui.card(root, "扣篮");
        toggle(page, dunks, "强力隔扣", "所控球员篮下 4.5 米内可隔扣，扣篮结算成功率 99.9%。", S.CONTACT_DUNK);
        toggle(page, dunks, "范围内按投篮即扣篮", "在下面的半径内持球按投篮会改成扣篮，超出范围照常投篮。", S.FORCE_DUNK);
        final Ui.Slider range = slider(page, dunks, "扣篮半径", null, S.DUNK_RANGE, 50, Ui::meters);
        LinearLayout rangeChips = chips(dunks);
        int[] values = {300, 800, 1500, 3000};
        for (final int cm : values) chip(rangeChips, (cm / 100) + " 米", v -> { store.set(S.DUNK_RANGE, cm); range.set(cm); });

        LinearLayout jump = ui.card(root, "弹跳");
        toggle(page, jump, "弹跳增强（所控球员）", "所有起跳动作按下面的高度离地，不会降低本来更高的起跳。"
            + "封盖、抢篮板时会自动对准球的高度，手不会跳过球；投篮、上篮照常跳满。扣篮由游戏自己的扣篮轨迹处理。", S.JUMP_BOOST);
        slider(page, jump, "弹跳高度", null, S.JUMP_HEIGHT, 5, Ui::meters);

        LinearLayout hot = ui.card(root, "手感");
        toggle(page, hot, "全场热区（我方）", "我方球员每个投篮区域都按热区计算加成。", S.HOT_ZONES);
        toggle(page, hot, "手感火热（我方）", "我方球员一直处于最火热的手感状态，享受游戏内置的能力加成。", S.ON_FIRE);

        LinearLayout move = ui.card(root, "跑动 · 晃倒");
        tuneRow(page, move, 0, 9);
        tuneRow(page, move, 0, 10);
        LinearLayout normal = chips(move);
        chip(normal, "回到常速 ×1", v -> {
            store.setTune(0, 9, false, 50);
            store.setTune(0, 10, false, 50);
            syncAll();
        });
        final Ui.Slider ankle = slider(page, move, "晃倒强度", "0 关闭 · 50 原版 · 80 激进 · 100 强制晃倒。作用双方。",
            S.ANKLE, 1, String::valueOf);
        LinearLayout ankleChips = chips(move);
        int[] ankles = {50, 80, 100};
        String[] names = {"原版 50", "激进 80", "强制 100"};
        for (int i = 0; i < ankles.length; i++) {
            final int value = ankles[i];
            chip(ankleChips, names[i], v -> { store.set(S.ANKLE, value); ankle.set(value); });
        }
    }

    // ------------------------------------------------------------------ 防守
    static final String GRAB_HELP = "你操控的球员每次盖帽成功时，按这个概率把球直接抓在手里（可精确到 1%，长按 −/+ 连续调）。"
        + "跳投扇帽、上篮扣篮封盖、空中封盖都算；盖到球后下一帧收进手里；裁判吹哨（犯规、干扰球）时不抓。"
        + "只对你操控的球员生效，电脑盖帽不会抓。";

    static String handGap(int reachCm) { return "手边 " + Math.round(reachCm * 0.4f) + " 厘米"; }

    static final String SUPER_GRAB_HELP = "打开后不管你在场上哪里，对手每一次投篮都会被你抓到手里，算你一次盖帽："
        + "跳投、上篮在球出手的一瞬间，扣篮在对手起跳扣的时候。不受上面的总开关和概率影响；裁判吹哨（犯规、罚球）时不生效。";

    private void buildDefense(LinearLayout root, Page page) {
        LinearLayout grab = ui.card(root, "抓帽");
        toggle(page, grab, "开启抓帽", "总开关。关掉后完全是原版盖帽，概率设置会保留。", S.GRAB_ON);
        slider(page, grab, "抓帽概率", GRAB_HELP, S.GRAB_PCT, 1, Ui::percent);
        toggle(page, grab, "优先使用游戏自带抓帽动作", "打开后游戏会尽量换成双手抓球的动作，好看，但时机不准时可能整个扑空；"
            + "关掉（默认）就用普通盖帽动作，盖到就抓，最稳。街头 1v1/3v3 里游戏本身不吃这个设置。", S.GRAB_ANIM);
        toggle(page, grab, "超级抓帽（全场）", SUPER_GRAB_HELP, S.SUPER_GRAB);
        statLine(page, grab, () -> "本次运行：盖帽 " + st(S.ST_HUMAN_BLOCKS) + " 次 · 抓到球 "
            + (st(S.ST_GRAB_FORCED) + st(S.ST_GRAB_NATURAL) + st(S.ST_SUPER_GRABS)) + " 次（超级抓帽 "
            + st(S.ST_SUPER_GRABS) + "）· 被抢先 " + st(S.ST_GRAB_LOST) + " 次");
        statLine(page, grab, () -> "细节：自带抓帽动作接住 " + st(S.ST_GRAB_NATURAL) + "（补救 " + st(S.ST_GRAB_RESCUED)
            + "）· 抓帽动作扑空 " + st(S.ST_GRAB_WHIFF) + " · 队友盖帽 " + st(S.ST_BLOCK_TEAMMATE)
            + "。如果你自己盖到却记成队友，截图发我。");

        LinearLayout steal = ui.card(root, "抢断");
        slider(page, steal, "抢断成功率", "你操控的球员伸手碰到球时，断球成功率至少为此值。0% 为原版。", S.STEAL_PCT, 1, Ui::percent);
        toggle(page, steal, "断下后直接抱住球", "抢断成功率大于 0 时生效：球被拍掉后直接进到你手里。", S.STEAL_KEEP);
        statLine(page, steal, () -> "本次运行：断球成功 " + st(S.ST_STEAL_POKES) + " 次 · 直接抱住 " + st(S.ST_STEAL_CATCHES) + " 次");

        LinearLayout air = ui.card(root, "空中封盖");
        toggle(page, air, "增强空中封盖判定", "所控球员起跳封盖时，球离任意一只手在下面这个距离以内就判定为封盖。"
            + "按手的位置算，不会离着身子隔空把球扇走；不增加起跳高度。", S.AIR_BLOCK);
        slider(page, air, "封盖判定距离", null, S.BLOCK_REACH, 10, Overlay::handGap);
    }

    // ------------------------------------------------------------------ 人机
    private void buildCpu(LinearLayout root, Page page) {
        LinearLayout cpu = ui.card(root, "超强人机");
        toggle(page, cpu, "开启超强人机", "电脑会预判你的出手提前起跳封盖、贴身跟防、更会运球和终结；抢断上限 60%。"
            + "只作用电脑一方，关闭后恢复。", S.SUPER_CPU);
        toggle(page, cpu, "不针对我的必中和隔扣", "开启（默认）时，你用三分必中、全场必中、强力隔扣、范围扣篮出手的那一下，"
            + "电脑不会额外预判起跳、强行封盖，只按游戏原本的水平防守，这些功能不会被超强人机废掉。关掉则照样全力封盖。", S.CPU_SPARE);
        statLine(page, cpu, () -> "本次运行：电脑封盖 " + st(S.ST_CPU_BLOCKS) + " · 电脑三分决策 " + st(S.ST_CPU_THREES));

        LinearLayout style = ui.card(root, "人机进攻偏好");
        final Ui.Slider bias = slider(page, style, "三分 / 冲框", "向左偏突破冲框隔扣，向右偏三分、后撤和急停；落后时自动提高三分倾向追分。",
            S.CPU_THREE_BIAS, 1, v -> "三分 " + v + " / 冲框 " + (100 - v));
        LinearLayout presets = chips(style);
        int[] values = {15, 50, 90};
        String[] names = {"冲框隔扣", "均衡进攻", "三分主攻"};
        for (int i = 0; i < values.length; i++) {
            final int value = values[i];
            chip(presets, names[i], v -> { store.set(S.CPU_THREE_BIAS, value); bias.set(value); });
        }
        TextView more = ui.text("电脑的能力数值在「参数」页单独调。", 11.5f, Ui.MUTED);
        style.addView(more);
    }

    // ------------------------------------------------------------------ 趣味
    private void buildFun(LinearLayout root, Page page) {
        LinearLayout slow = ui.card(root, "高光慢镜头");
        toggle(page, slow, "开启高光慢镜头", "你操控的球员扣篮起跳、盖帽、抢断时，整场进入短暂的子弹时间（游戏扣篮大赛同款慢放）。", S.SLOWMO);
        slider(page, slow, "慢放速度", null, S.SLOWMO_SPEED, 5, v -> (v / 100f) + "×");
        slider(page, slow, "慢放时长", null, S.SLOWMO_TIME, 1, v -> (v / 10f) + " 秒");
        toggle(page, slow, "扣篮时触发", null, S.SLOWMO_DUNK);
        toggle(page, slow, "盖帽时触发", null, S.SLOWMO_BLOCK);
        toggle(page, slow, "抢断时触发", null, S.SLOWMO_STEAL);
        statLine(page, slow, () -> "本次运行已触发 " + st(S.ST_SLOWMO_COUNT) + " 次");

        LinearLayout lab = ui.card(root, "实验性（效果需实测）");
        toggle(page, lab, "无限接管 Takeover", "我方球员一直按接管状态结算各项加成。手游版是否显示接管特效未验证。", S.TAKEOVER);
        toggle(page, lab, "巨人模式", "你操控的球员按下面的倍数放大；切换操控球员时自动恢复原体型。动作和碰撞可能不完全贴合。", S.GIANT);
        slider(page, lab, "体型倍数", null, S.GIANT_SCALE, 5, v -> (v / 100f) + "×");
        statLine(page, lab, () -> "接管判定 " + st(S.ST_TAKEOVER_CALLS) + " 次 · 体型调整 " + st(S.ST_GIANT_APPLIED) + " 次");
    }

    // ------------------------------------------------------------------ 规则
    private void buildRules(LinearLayout root, Page page) {
        LinearLayout bulk = ui.card(root, "一键");
        LinearLayout row = chips(bulk);
        chip(row, "免除全部侵人犯规", v -> { for (int i = 0; i <= 7; i++) store.setRule(i, true); syncAll(); });
        chip(row, "免除全部违例", v -> { for (int i = 9; i < S.RULES.length; i++) store.setRule(i, true); syncAll(); });
        chip(row, "全部恢复判罚", v -> { for (int i = 0; i < S.RULES.length; i++) store.setRule(i, false); syncAll(); });
        bulk.addView(ui.text("开关作用全场双方，直接取消该判罚，不是降低概率；已经吹罚的不会撤销。", 11.5f, Ui.MUTED));

        LinearLayout list = ui.card(root, "逐项设置");
        for (int i = 0; i < S.RULES.length; i++) {
            final int rule = i;
            final Ui.Toggle t = ui.toggleRow(list, "免" + S.RULES[i], null, store.rule(i), on -> store.setRule(rule, on));
            page.syncs.add(() -> t.setChecked(store.rule(rule), false));
        }
        statLine(page, list, () -> {
            StringBuilder text = new StringBuilder("本次运行已拦截：");
            int shown = 0;
            for (int i = 0; i < S.RULES.length; i++) {
                long n = st(S.ST_RULE_BASE + i);
                if (n > 0) { text.append(shown++ > 0 ? "、" : "").append(S.RULES[i]).append(' ').append(n); }
            }
            return shown == 0 ? "本次运行还没有拦截判罚" : text.toString();
        });
    }

    // ------------------------------------------------------------------ 参数
    private int tuneTeam;

    private void buildTune(LinearLayout root, Page page) {
        LinearLayout head = ui.card(root, null);
        LinearLayout tabs = chips(head);
        final LinearLayout list = ui.vertical();
        final TextView[] tab = new TextView[2];
        String[] names = {"我方参数", "电脑参数"};
        for (int i = 0; i < 2; i++) {
            final int team = i;
            tab[i] = chip(tabs, names[i], v -> {
                tuneTeam = team;
                fillTune(list, page);
                for (int k = 0; k < 2; k++) tab[k].setBackgroundDrawable(ui.round(k == team ? Ui.ACCENT : Ui.BUTTON, 8));
            });
        }
        tab[0].setBackgroundDrawable(ui.round(Ui.ACCENT, 8));
        head.addView(ui.text("打开开关后生效，50 为原版。速度、敏捷最高 200（跑动 ×5 / 转向 ×8）。超强人机开启时电脑的部分参数会被拉满。",
            11.5f, Ui.MUTED));
        root.addView(list, ui.fill(-2));
        fillTune(list, page);
    }

    private void fillTune(LinearLayout list, Page page) {
        list.removeAllViews();
        page.syncs.clear();
        LinearLayout card = ui.card(list, tuneTeam == 0 ? "我方" : "电脑");
        for (int key = 0; key < S.TUNE_KEYS; key++) tuneRow(page, card, tuneTeam, key);
    }

    static String tuneLabel(int key, int value) {
        if (key != 9 && key != 10) return String.valueOf(value);
        float factor;
        if (value <= 50) factor = 0.25f + value * 0.015f;
        else if (key == 9) factor = value <= 100 ? 1 + (value - 50) * 0.04f : 3 + (value - 100) * 0.02f;
        else factor = value <= 100 ? 1 + (value - 50) * 0.06f : 4 + (value - 100) * 0.04f;
        return value + " · ×" + Math.round(factor * 100) / 100.0f;
    }

    /** Enable switch + slider for one tune key. */
    void tuneRow(Page page, LinearLayout parent, final int team, final int key) {
        String title = S.TUNABLES[key] + (key == 9 ? " · 跑动倍率" : key == 10 ? " · 转向倍率" : "");
        final Ui.Slider[] holder = new Ui.Slider[1];
        final Ui.Toggle toggle = ui.toggleRow(parent, title, null, store.tuneOn(team, key), on -> {
            store.setTune(team, key, on, holder[0].get());
            holder[0].setEnabled(on);
        });
        LinearLayout sliderBox = ui.vertical();
        parent.addView(sliderBox, ui.fill(-2));
        holder[0] = ui.sliderRow(sliderBox, "数值", null, 0, Store.tuneMax(key), 1, store.tuneValue(team, key),
            v -> tuneLabel(key, v), v -> store.setTune(team, key, toggle.isChecked(), v));
        holder[0].setEnabled(store.tuneOn(team, key));
        page.syncs.add(() -> {
            toggle.setChecked(store.tuneOn(team, key), false);
            holder[0].set(store.tuneValue(team, key));
            holder[0].setEnabled(store.tuneOn(team, key));
        });
    }

    // ------------------------------------------------------------------ 生涯
    private int careerOriginal = -1;   // height before the first change in this session (tenths of a cm)

    static String heightLabel(int tenths) {
        float cm = tenths / 10f;
        int inches = Math.round(cm / 2.54f);
        return String.format(java.util.Locale.US, "%.1f 厘米（%d'%d\"）", cm, inches / 12, inches % 12);
    }

    private void buildCareer(LinearLayout root, Page page) {
        LinearLayout card = ui.card(root, "生涯球员身高");
        final TextView state = ui.text("", 12.5f, Ui.MUTED);
        state.setPadding(0, 0, 0, ui.dp(4));
        card.addView(state);
        final Ui.Slider[] height = new Ui.Slider[1];
        height[0] = ui.sliderRow(card, "身高", "直接修改生涯球员档案里的真实身高，会影响体型和与身高相关的能力计算；"
            + "下一场比赛生效，随游戏自动存档保存。游戏自带的捏人界面在生涯里禁止改身高，这里绕过了那个限制。",
            1500, 2450, 10, 2000, Overlay::heightLabel, v -> {
                int now = Native.careerOp(Native.CAREER_GET_HEIGHT, 0);
                if (now < 0) {
                    toast("还没载入生涯存档：先进入「辉煌生涯」");
                    return;
                }
                if (careerOriginal < 0) careerOriginal = now;
                Native.careerOp(Native.CAREER_SET_HEIGHT, v);
            });
        LinearLayout row = chips(card);
        chip(row, "撤销本次修改", v -> {
            if (careerOriginal < 0) return;
            if (Native.careerOp(Native.CAREER_SET_HEIGHT, careerOriginal) >= 0) {
                height[0].set(Math.round(careerOriginal / 10f) * 10);
                toast("已恢复为 " + heightLabel(careerOriginal));
            }
        });
        Runnable refresh = () -> {
            int h = Native.careerOp(Native.CAREER_GET_HEIGHT, 0);
            boolean ok = h > 0;
            state.setText(ok ? "当前生涯球员身高：" + heightLabel(h)
                : h == -1 ? "还没载入生涯存档：进入「辉煌生涯」后再打开这一页" : "生涯数据暂不可用（" + h + "）");
            if (ok) height[0].set(Math.round(h / 10f) * 10);
            height[0].setEnabled(ok);
        };
        page.syncs.add(refresh);
        refresh.run();

        LinearLayout more = ui.card(root, "换衣服 · 换球员");
        more.addView(ui.text("开发中，下一版加入。动生涯存档前，建议先用项目里 backup/ 的存档备份工具把存档导出到电脑。", 12, Ui.MUTED));
    }

    // ------------------------------------------------------------------ windows
    private int screenWidth() {
        int w = activity.getWindow().getDecorView().getWidth();
        return w > 0 ? w : activity.getResources().getDisplayMetrics().widthPixels;
    }

    private int screenHeight() {
        int h = activity.getWindow().getDecorView().getHeight();
        return h > 0 ? h : activity.getResources().getDisplayMetrics().heightPixels;
    }

    private void clamp(WindowManager.LayoutParams lp) {
        lp.x = Math.max(0, Math.min(lp.x, screenWidth() - lp.width));
        lp.y = Math.max(0, Math.min(lp.y, screenHeight() - lp.height));
    }

    private boolean attach(View view, WindowManager.LayoutParams lp) {
        lp.token = activity.getWindow().getDecorView().getWindowToken();
        if (lp.token == null || activity.isFinishing()) return false;
        try {
            clamp(lp);
            windows.addView(view, lp);
            return true;
        } catch (Throwable error) {
            Log.e(Native.TAG, "attach failed", error);
            return false;
        }
    }

    private void detach(View view) {
        try { windows.removeViewImmediate(view); } catch (Throwable ignored) { }
    }

    private void update(View view, WindowManager.LayoutParams lp) {
        clamp(lp);
        try { windows.updateViewLayout(view, lp); } catch (Throwable ignored) { }
    }

    void show() {
        visible = true;
        if (destroyed) return;
        if (panelOpen) {
            if (!panelShown) panelShown = attach(panel, panelLp);
        } else if (!bubbleShown) {
            bubbleShown = attach(bubble, bubbleLp);
        }
        if (!bubbleShown && !panelShown) handler.postDelayed(() -> { if (visible) show(); }, 500);
        else if (panelShown) handler.postDelayed(ticker, 1000);
    }

    void hide() {
        visible = false;
        handler.removeCallbacksAndMessages(null);
        if (bubbleShown) { detach(bubble); bubbleShown = false; }
        if (panelShown) { detach(panel); panelShown = false; }
    }

    void destroy() {
        destroyed = true;
        hide();
    }

    private void expand() {
        int w = Math.min(screenWidth() - ui.dp(24), ui.dp(640));
        int h = Math.min(screenHeight() - ui.dp(16), ui.dp(420));
        panelLp.width = w;
        panelLp.height = h;
        panelLp.x = (screenWidth() - w) / 2;
        panelLp.y = (screenHeight() - h) / 2;
        if (bubbleShown) { detach(bubble); bubbleShown = false; }
        panelOpen = true;
        if (!panelShown) panelShown = attach(panel, panelLp);
        updateStatus();
        if (current >= 0) select(current);
        handler.removeCallbacks(ticker);
        handler.postDelayed(ticker, 1000);
    }

    private void collapse() {
        panelOpen = false;
        handler.removeCallbacks(ticker);
        if (panelShown) { detach(panel); panelShown = false; }
        if (!bubbleShown) bubbleShown = attach(bubble, bubbleLp);
    }

    private final Runnable ticker = new Runnable() {
        @Override public void run() {
            if (!panelOpen || !panelShown) return;
            updateStatus();
            if (current >= 0 && pages[current] != null) pages[current].refreshStats();
            handler.postDelayed(this, 1000);
        }
    };

    /** Drag for the bubble (tap opens the panel, release snaps to the nearest side) and the panel header. */
    private final class Drag implements View.OnTouchListener {
        private final WindowManager.LayoutParams lp;
        private final boolean isBubble;
        private float downX, downY;
        private int startX, startY;
        private boolean moved;

        Drag(WindowManager.LayoutParams lp, boolean isBubble) {
            this.lp = lp;
            this.isBubble = isBubble;
        }

        @Override public boolean onTouch(View view, MotionEvent event) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    downX = event.getRawX();
                    downY = event.getRawY();
                    startX = lp.x;
                    startY = lp.y;
                    moved = false;
                    return true;
                case MotionEvent.ACTION_MOVE:
                    float dx = event.getRawX() - downX, dy = event.getRawY() - downY;
                    if (Math.abs(dx) + Math.abs(dy) > ui.dp(6)) moved = true;
                    if (moved) {
                        lp.x = startX + (int) dx;
                        lp.y = startY + (int) dy;
                        update(isBubble ? bubble : panel, lp);
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                    if (isBubble) {
                        if (!moved) {
                            expand();
                            return true;
                        }
                        lp.x = lp.x + lp.width / 2 < screenWidth() / 2 ? ui.dp(4) : screenWidth() - lp.width - ui.dp(4);
                        update(bubble, lp);
                        store.prefs().edit().putInt("bubble_x", lp.x).putInt("bubble_y", lp.y).apply();
                    }
                    return true;
                default:
                    return true;
            }
        }
    }
}
