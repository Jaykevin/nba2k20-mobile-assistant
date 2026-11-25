package com.local.nba2k20;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/** Small widget kit with a consistent look (custom drawn, independent of the game's theme). */
final class Ui {
    static final int PANEL = 0xF2111720;
    static final int SIDEBAR = 0xFF0B1017;
    static final int CARD = 0xFF19212D;
    static final int CARD_STROKE = 0x14FFFFFF;
    static final int ACCENT = 0xFFFF7A2F;
    static final int ACCENT_SOFT = 0x33FF7A2F;
    static final int TEXT = 0xFFE9EEF5;
    static final int MUTED = 0xFF8D9BB0;
    static final int TRACK = 0xFF333E4E;
    static final int GOOD = 0xFF3DDC84;
    static final int WARN = 0xFFFFC14D;
    static final int BAD = 0xFFFF5A5A;
    static final int BUTTON = 0xFF263142;

    final Context context;
    private final float density;

    Ui(Context context) {
        this.context = context;
        density = context.getResources().getDisplayMetrics().density;
    }

    int dp(float value) { return Math.round(value * density); }

    GradientDrawable round(int color, float radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        return d;
    }

    GradientDrawable outlined(int color, float radiusDp, int stroke) {
        GradientDrawable d = round(color, radiusDp);
        d.setStroke(Math.max(1, dp(1)), stroke);
        return d;
    }

    TextView text(String value, float sp, int color) {
        TextView view = new TextView(context);
        view.setText(value);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setIncludeFontPadding(false);
        return view;
    }

    TextView bold(String value, float sp, int color) {
        TextView view = text(value, sp, color);
        view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return view;
    }

    LinearLayout vertical() {
        LinearLayout layout = new LinearLayout(context);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    LinearLayout horizontal() {
        LinearLayout layout = new LinearLayout(context);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        layout.setGravity(Gravity.CENTER_VERTICAL);
        return layout;
    }

    LinearLayout.LayoutParams fill(int height) { return new LinearLayout.LayoutParams(-1, height); }
    LinearLayout.LayoutParams weight(float w, int height) { return new LinearLayout.LayoutParams(0, height, w); }

    /** Card container with a title. */
    LinearLayout card(LinearLayout parent, String title) {
        LinearLayout card = vertical();
        card.setBackgroundDrawable(outlined(CARD, 12, CARD_STROKE));
        card.setPadding(dp(12), dp(10), dp(12), dp(8));
        if (title != null) {
            TextView heading = bold(title, 13, ACCENT);
            heading.setPadding(0, 0, 0, dp(6));
            card.addView(heading);
        }
        LinearLayout.LayoutParams lp = fill(-2);
        lp.setMargins(0, 0, 0, dp(8));
        parent.addView(card, lp);
        return card;
    }

    TextView button(String label, boolean primary) {
        TextView view = bold(label, 13, primary ? 0xFF1A1206 : TEXT);
        view.setGravity(Gravity.CENTER);
        view.setSingleLine(true);
        view.setEllipsize(TextUtils.TruncateAt.END);
        view.setPadding(dp(10), 0, dp(10), 0);
        view.setBackgroundDrawable(round(primary ? ACCENT : BUTTON, 8));
        view.setClickable(true);
        return view;
    }

    /** Collapsible hint shown when the ⓘ of a row is tapped. */
    TextView hint(String value) {
        TextView view = text(value, 11.5f, MUTED);
        view.setLineSpacing(dp(2), 1f);
        view.setPadding(0, dp(2), 0, dp(6));
        view.setVisibility(View.GONE);
        return view;
    }

    TextView info(final TextView hint) {
        TextView view = text("ⓘ", 15, MUTED);
        view.setGravity(Gravity.CENTER);
        view.setPadding(dp(6), 0, dp(6), 0);
        view.setOnClickListener(v -> hint.setVisibility(hint.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));
        return view;
    }

    // ------------------------------------------------------------------ toggle switch
    interface OnToggle { void changed(boolean on); }

    static final class Toggle extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF rect = new RectF();
        private boolean checked;
        private float position;
        private OnToggle listener;

        Toggle(Context context, boolean checked) {
            super(context);
            this.checked = checked;
            position = checked ? 1f : 0f;
            setClickable(true);
            setOnClickListener(v -> setChecked(!this.checked, true));
        }

        boolean isChecked() { return checked; }
        void setListener(OnToggle l) { listener = l; }

        void setChecked(boolean value, boolean fromUser) {
            if (value == checked) return;
            checked = value;
            ValueAnimator animator = ValueAnimator.ofFloat(position, value ? 1f : 0f);
            animator.setDuration(140);
            animator.addUpdateListener(a -> { position = (Float) a.getAnimatedValue(); invalidate(); });
            animator.start();
            if (fromUser && listener != null) listener.changed(value);
        }

        @Override protected void onDraw(Canvas canvas) {
            float w = getWidth(), h = getHeight();
            float trackH = h * 0.56f, top = (h - trackH) / 2;
            rect.set(0, top, w, top + trackH);
            paint.setColor(blend(TRACK, ACCENT, position));
            canvas.drawRoundRect(rect, trackH / 2, trackH / 2, paint);
            float radius = trackH / 2 - h * 0.06f;
            float cx = trackH / 2 + (w - trackH) * position;
            paint.setColor(0xFFFFFFFF);
            canvas.drawCircle(cx, h / 2, radius, paint);
        }

        private static int blend(int a, int b, float t) {
            int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
            int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
            return 0xFF000000 | ((int) (ar + (br - ar) * t) << 16) | ((int) (ag + (bg - ag) * t) << 8) | (int) (ab + (bb - ab) * t);
        }
    }

    /** Row: title (+ optional ⓘ hint) and a switch; tapping the row toggles. */
    Toggle toggleRow(LinearLayout parent, String title, String help, boolean checked, OnToggle listener) {
        LinearLayout row = horizontal();
        row.setMinimumHeight(dp(40));
        TextView label = text(title, 14, TEXT);
        row.addView(label, weight(1, -2));
        TextView hint = help != null ? hint(help) : null;
        if (hint != null) row.addView(info(hint), new LinearLayout.LayoutParams(-2, dp(32)));
        final Toggle toggle = new Toggle(context, checked);
        toggle.setListener(listener);
        row.addView(toggle, new LinearLayout.LayoutParams(dp(44), dp(30)));
        row.setOnClickListener(v -> toggle.setChecked(!toggle.isChecked(), true));
        parent.addView(row, fill(-2));
        if (hint != null) parent.addView(hint, fill(-2));
        return toggle;
    }

    // ------------------------------------------------------------------ slider
    interface Format { String of(int value); }
    interface OnValue { void changed(int value); }

    static final class Slider {
        SeekBar bar;
        TextView value;
        int min, step;
        Format format;
        void set(int v) { bar.setProgress((v - min) / step); value.setText(format.of(v)); }
        int get() { return min + bar.getProgress() * step; }
        void setEnabled(boolean enabled) {
            bar.setEnabled(enabled);
            bar.setAlpha(enabled ? 1f : .35f);
            value.setAlpha(enabled ? 1f : .5f);
        }
    }

    SeekBar seekBar() {
        SeekBar bar = new SeekBar(context);
        GradientDrawable track = round(TRACK, 3);
        GradientDrawable fill = round(ACCENT, 3);
        LayerDrawable layers = new LayerDrawable(new Drawable[]{track, new ClipDrawable(fill, Gravity.LEFT, ClipDrawable.HORIZONTAL)});
        layers.setId(0, android.R.id.background);
        layers.setId(1, android.R.id.progress);
        bar.setProgressDrawable(layers);
        GradientDrawable thumb = new GradientDrawable();
        thumb.setShape(GradientDrawable.OVAL);
        thumb.setColor(0xFFFFFFFF);
        thumb.setStroke(dp(3), ACCENT);
        thumb.setSize(dp(18), dp(18));
        bar.setThumb(thumb);
        bar.setPadding(dp(10), dp(6), dp(10), dp(6));
        bar.setMinimumHeight(dp(6));
        return bar;
    }

    /** Row: title, value chip, −/+ steppers and a slider underneath. */
    Slider sliderRow(LinearLayout parent, String title, String help, int min, int max, int step, int value,
                     Format format, OnValue listener) {
        final Slider s = new Slider();
        s.min = min;
        s.step = step;
        s.format = format;
        LinearLayout head = horizontal();
        head.setMinimumHeight(dp(32));
        head.addView(text(title, 14, TEXT), weight(1, -2));
        TextView hint = help != null ? hint(help) : null;
        if (hint != null) head.addView(info(hint), new LinearLayout.LayoutParams(-2, dp(28)));
        TextView minus = button("−", false);
        TextView plus = button("+", false);
        s.value = bold("", 13, ACCENT);
        s.value.setGravity(Gravity.CENTER);
        s.value.setMinWidth(dp(64));
        head.addView(minus, new LinearLayout.LayoutParams(dp(30), dp(26)));
        head.addView(s.value, new LinearLayout.LayoutParams(-2, dp(26)));
        head.addView(plus, new LinearLayout.LayoutParams(dp(30), dp(26)));
        parent.addView(head, fill(-2));
        s.bar = seekBar();
        s.bar.setMax((max - min) / step);
        parent.addView(s.bar, fill(dp(30)));
        if (hint != null) parent.addView(hint, fill(-2));
        s.set(value);
        s.bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean user) {
                int v = min + progress * step;
                s.value.setText(format.of(v));
                if (user) listener.changed(v);
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { }
        });
        stepper(minus, s, min, max, -step, listener);
        stepper(plus, s, min, max, step, listener);
        return s;
    }

    /** −/+ button: one step per tap; held down it repeats after 0.4 s and speeds up. */
    private void stepper(final View button, final Slider s, final int min, final int max, final int delta,
                         final OnValue listener) {
        final int[] repeats = new int[1];
        final Runnable[] repeat = new Runnable[1];
        repeat[0] = () -> {
            if (!step(s, min, max, delta, listener)) return;
            repeats[0]++;
            button.postDelayed(repeat[0], repeats[0] > 12 ? 30 : 70);
        };
        button.setOnTouchListener((v, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    if (v.getParent() != null) v.getParent().requestDisallowInterceptTouchEvent(true);
                    v.setPressed(true);
                    repeats[0] = 0;
                    step(s, min, max, delta, listener);
                    v.postDelayed(repeat[0], 400);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    v.setPressed(false);
                    v.removeCallbacks(repeat[0]);
                    return true;
                default:
                    return true;
            }
        });
        button.setOnClickListener(v -> step(s, min, max, delta, listener));   // accessibility "click" action
    }

    /** One step; false when disabled or already at the end of the range. */
    private static boolean step(Slider s, int min, int max, int delta, OnValue listener) {
        if (!s.bar.isEnabled()) return false;
        int current = s.get();
        int next = Math.max(min, Math.min(max, current + delta));
        if (next == current) return false;
        s.set(next);
        listener.changed(next);
        return true;
    }

    static String percent(int v) { return v + "%"; }
    static String meters(int cm) { return (cm / 100f) + " 米"; }
}
