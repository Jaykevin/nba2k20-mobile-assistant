package com.local.nba2k20;

import android.graphics.drawable.GradientDrawable;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Dunk action picker: recycling list, category filter, combo selection, weights and lock. */
final class DunkPage {
    private static final int CAT_ALL = -2, CAT_MIX = -4, CAT_ROTATION = -3, CAT_GENERIC = -1;

    private final Overlay overlay;
    private final Store store;
    private final Ui ui;
    private final LinearLayout root;
    private final ListView list;
    private final Adapter adapter = new Adapter();
    private final List<Integer> actions = new ArrayList<>();
    private final List<Integer> categories = new ArrayList<>();
    private TextView summary, categoryButton, adjustedButton, mixButton;
    private int category = CAT_ALL;
    private boolean onlyAdjusted, choosingCategory;

    DunkPage(Overlay overlay, Overlay.Page page) {
        this.overlay = overlay;
        store = overlay.store;
        ui = overlay.ui;
        root = ui.vertical();
        list = new ListView(overlay.activity);
        list.setDivider(null);
        list.setVerticalScrollBarEnabled(false);
        list.setCacheColorHint(0);
        list.setSelector(new GradientDrawable());
        list.addHeaderView(buildHeader(page), null, false);
        list.setAdapter(adapter);
        root.addView(list, new LinearLayout.LayoutParams(-1, -1));
        buildCategories();
        reload();
        page.syncs.add(this::reload);
        page.stats.add(() -> { updateSummary(); adapter.notifyDataSetChanged(); });
    }

    View root() { return root; }

    private View buildHeader(Overlay.Page page) {
        LinearLayout box = ui.vertical();
        LinearLayout card = ui.card(box, "扣篮动作");
        overlay.toggle(page, card, "使用自选扣篮动作", "普通扣篮和范围扣篮都从下面的动作库里抽选，优先避开最近用过的 64 个动作；"
            + "会优先挑起跳距离和你离篮筐距离相近的动作，避免动作被拉长成「飘」过去。锁定单个动作时不做这个筛选。", S.DUNK_VARIETY);
        overlay.toggle(page, card, "生涯解锁全部扣篮包", "生涯角色可以使用本地全部 64 个扣篮包里的动作，不改写购买记录。", S.UNLOCK_DUNKS);
        summary = ui.text("", 12, Ui.MUTED);
        summary.setLineSpacing(ui.dp(2), 1f);
        summary.setPadding(0, ui.dp(4), 0, ui.dp(2));
        card.addView(summary);
        LinearLayout modes = overlay.chips(card);
        mixButton = overlay.chip(modes, "", v -> {
            store.setMixEnabled(!store.on(S.DUNK_MIX));
            overlay.syncAll();
        });
        overlay.chip(modes, "全库均匀混选", v -> {
            store.uniformDunks();
            overlay.syncAll();
            overlay.toast("已恢复全库均匀混选（组合名单保留）");
        });
        overlay.chip(modes, "清空组合", v -> {
            store.clearMix();
            overlay.syncAll();
        });
        LinearLayout filters = overlay.chips(card);
        categoryButton = overlay.chip(filters, "", v -> {
            choosingCategory = !choosingCategory;
            reload();
        });
        adjustedButton = overlay.chip(filters, "", v -> {
            onlyAdjusted = !onlyAdjusted;
            reload();
        });
        LinearLayout bulk = overlay.chips(card);
        overlay.chip(bulk, "本分类全部加入组合", v -> {
            store.setCategorySelected(category == CAT_MIX ? CAT_ALL : category, true);
            reload();
        });
        overlay.chip(bulk, "本分类移出组合", v -> {
            store.setCategorySelected(category == CAT_MIX ? CAT_ALL : category, false);
            reload();
        });
        return box;
    }

    private void buildCategories() {
        categories.add(CAT_ALL);
        categories.add(CAT_MIX);
        categories.add(CAT_ROTATION);
        categories.add(CAT_GENERIC);
        for (int p = 1; p <= 64; p++) if (count(p) > 0) categories.add(p);
    }

    private String categoryName(int c) {
        if (c == CAT_ALL) return "全部动作";
        if (c == CAT_MIX) return "我的组合";
        if (c == CAT_ROTATION) return "旋转扣篮 · 360°";
        if (c == CAT_GENERIC) return "通用动作";
        return DunkData.PACKAGE_NAMES[c];
    }

    private boolean inCategory(int index, int c) {
        return c == CAT_MIX ? store.mixSelected(index) : DunkData.inCategory(index, c);
    }

    private int count(int c) {
        int n = 0;
        for (int i = 0; i < DunkData.COUNT; i++) if (inCategory(i, c)) n++;
        return n;
    }

    private void reload() {
        actions.clear();
        int locked = store.get(S.DUNK_LOCKED);
        for (int i = 0; i < DunkData.COUNT; i++) {
            if (!inCategory(i, category)) continue;
            if (onlyAdjusted && store.dunkWeight(i) == 10 && locked != i && !store.mixSelected(i)) continue;
            actions.add(i);
        }
        Collections.sort(actions, (a, b) -> {
            int pack = DunkData.PACKAGE[a] - DunkData.PACKAGE[b];
            return pack != 0 ? pack : DunkData.VARIANT[a] - DunkData.VARIANT[b];
        });
        categoryButton.setText(choosingCategory ? "收起分类 ▴" : "分类：" + categoryName(category) + " ▾");
        adjustedButton.setText(onlyAdjusted ? "显示全部" : "只看已选 / 已调整");
        updateSummary();
        adapter.notifyDataSetChanged();
    }

    private void updateSummary() {
        int locked = store.get(S.DUNK_LOCKED);
        boolean mix = store.on(S.DUNK_MIX);
        String mode = !store.on(S.DUNK_VARIETY) ? "自选动作已关闭（使用游戏默认选择）"
            : locked >= 0 ? "锁定：" + DunkData.title(locked)
            : mix ? "仅用我的组合：已选 " + store.mixSelectedCount() + " 个，权重 > 0 的 " + store.mixActiveCount() + " 个"
            : "全库按权重混选";
        long available = Native.get(S.ST_DUNK_AVAILABLE);
        long last = Native.get(S.ST_DUNK_LAST) - 1;
        summary.setText(mode + "\n本场已加载 " + available + " 个动作"
            + (last >= 0 && last < DunkData.COUNT ? " · 上一次：" + DunkData.title((int) last) : "")
            + " · 当前列表 " + actions.size() + " 个");
        mixButton.setText(mix ? "组合：开 ✓" : "组合：关");
        mixButton.setBackgroundDrawable(ui.round(mix ? Ui.ACCENT : Ui.BUTTON, 8));
        mixButton.setTextColor(mix ? 0xFF1A1206 : Ui.TEXT);
    }

    // ------------------------------------------------------------------ list
    private static final class Holder {
        View dot;
        TextView title, mix, weight, minus, plus, lock;
        int index;
    }

    private final class Adapter extends BaseAdapter {
        @Override public int getCount() { return choosingCategory ? categories.size() : actions.size(); }
        @Override public Object getItem(int position) { return position; }
        @Override public long getItemId(int position) { return position; }
        @Override public int getViewTypeCount() { return 2; }
        @Override public int getItemViewType(int position) { return choosingCategory ? 1 : 0; }

        @Override public View getView(int position, View convert, ViewGroup parent) {
            return choosingCategory ? categoryRow(position, convert) : actionRow(position, convert);
        }
    }

    private View categoryRow(int position, View convert) {
        TextView row = convert instanceof TextView ? (TextView) convert : null;
        if (row == null) {
            row = ui.text("", 14, Ui.TEXT);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(ui.dp(12), 0, ui.dp(12), 0);
            row.setMinHeight(ui.dp(40));
        }
        final int c = categories.get(position);
        row.setText(categoryName(c) + " · " + count(c) + " 个");
        row.setTextColor(c == category ? Ui.ACCENT : Ui.TEXT);
        row.setOnClickListener(v -> {
            category = c;
            choosingCategory = false;
            list.setSelection(0);
            reload();
        });
        return row;
    }

    private View actionRow(int position, View convert) {
        Holder h = convert != null && convert.getTag() instanceof Holder ? (Holder) convert.getTag() : null;
        View row = convert;
        if (h == null) {
            h = new Holder();
            LinearLayout line = ui.horizontal();
            line.setPadding(ui.dp(4), 0, ui.dp(4), 0);
            line.setMinimumHeight(ui.dp(44));
            h.dot = new View(overlay.activity);
            LinearLayout.LayoutParams dotLp = new LinearLayout.LayoutParams(ui.dp(8), ui.dp(8));
            dotLp.setMargins(0, 0, ui.dp(8), 0);
            line.addView(h.dot, dotLp);
            h.title = ui.text("", 13.5f, Ui.TEXT);
            h.title.setSingleLine(true);
            h.title.setEllipsize(TextUtils.TruncateAt.END);
            line.addView(h.title, ui.weight(1, -2));
            h.mix = small(line, 44);
            h.minus = small(line, 28);
            h.weight = ui.bold("", 13, Ui.ACCENT);
            h.weight.setGravity(Gravity.CENTER);
            line.addView(h.weight, new LinearLayout.LayoutParams(ui.dp(34), ui.dp(28)));
            h.plus = small(line, 28);
            h.lock = small(line, 44);
            h.minus.setText("−");
            h.plus.setText("+");
            line.setTag(h);
            final Holder holder = h;
            h.mix.setOnClickListener(v -> {
                store.setMixSelected(holder.index, !store.mixSelected(holder.index));
                bind(holder);
                updateSummary();
            });
            h.minus.setOnClickListener(v -> stepWeight(holder, -5));
            h.plus.setOnClickListener(v -> stepWeight(holder, 5));
            h.lock.setOnClickListener(v -> {
                if (store.get(S.DUNK_LOCKED) == holder.index) store.set(S.DUNK_LOCKED, -1);
                else {
                    store.lockDunk(holder.index);
                    overlay.toast("已锁定：" + DunkData.title(holder.index));
                }
                overlay.syncAll();
            });
            row = line;
        }
        h.index = actions.get(position);
        bind(h);
        return row;
    }

    private TextView small(LinearLayout line, int widthDp) {
        TextView view = ui.button("", false);
        view.setTextSize(12);
        view.setPadding(0, 0, 0, 0);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ui.dp(widthDp), ui.dp(28));
        lp.setMargins(ui.dp(3), 0, 0, 0);
        line.addView(view, lp);
        return view;
    }

    private void stepWeight(Holder h, int delta) {
        int current = store.dunkWeight(h.index);
        int next = Math.max(0, Math.min(100, current + delta));
        if (current == 1 && delta > 0) next = 5;
        store.setDunkWeight(h.index, next);
        bind(h);
        updateSummary();
    }

    private void bind(Holder h) {
        int index = h.index;
        long available = Native.get(S.ST_DUNK_BASE + index);
        GradientDrawable dot = new GradientDrawable();
        dot.setShape(GradientDrawable.OVAL);
        dot.setColor(available == 1 ? Ui.GOOD : available < 0 ? Ui.BAD : 0xFF55606F);
        h.dot.setBackgroundDrawable(dot);
        h.title.setText(DunkData.title(index));
        boolean selected = store.mixSelected(index);
        h.mix.setText(selected ? "组合✓" : "组合");
        h.mix.setBackgroundDrawable(ui.round(selected ? Ui.ACCENT_SOFT : Ui.BUTTON, 6));
        h.mix.setTextColor(selected ? Ui.ACCENT : Ui.TEXT);
        int weight = store.dunkWeight(index);
        h.weight.setText(String.valueOf(weight));
        h.weight.setTextColor(weight == 0 ? Ui.MUTED : Ui.ACCENT);
        boolean locked = store.get(S.DUNK_LOCKED) == index;
        h.lock.setText(locked ? "已锁" : "锁定");
        h.lock.setBackgroundDrawable(ui.round(locked ? Ui.ACCENT : Ui.BUTTON, 6));
        h.lock.setTextColor(locked ? 0xFF1A1206 : Ui.TEXT);
    }
}
