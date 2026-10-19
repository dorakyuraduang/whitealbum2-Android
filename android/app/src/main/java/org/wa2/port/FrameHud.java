package org.wa2.port;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.TextView;
import java.lang.ref.WeakReference;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/** Displays statistics supplied by successful game presents, not UI refreshes. */
public final class FrameHud {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final AtomicBoolean QUEUED = new AtomicBoolean();
    private static final AtomicReference<Sample> LATEST = new AtomicReference<>();
    private static WeakReference<TextView> label = new WeakReference<>(null);
    private static volatile boolean resumed;
    private static volatile boolean enabled;
    private static final class Sample {
        final float fps, milliseconds;
        final long updated = SystemClock.uptimeMillis();
        Sample(float fps, float milliseconds) { this.fps = fps; this.milliseconds = milliseconds; }
    }
    private static final Runnable APPLY = () -> {
        QUEUED.set(false);
        TextView view = label.get();
        if (view == null || !resumed) return;
        Sample sample = LATEST.get();
        if (sample == null || SystemClock.uptimeMillis() - sample.updated > 1500) {
            view.setText("FPS --  |  -- ms");
        } else {
            view.setText(String.format(Locale.ROOT, "FPS %.1f  |  %.2f ms", sample.fps, sample.milliseconds));
        }
    };
    private static final Runnable STALE = new Runnable() {
        @Override public void run() {
            if (!resumed || label.get() == null) return;
            APPLY.run();
            MAIN.postDelayed(this, 1000);
        }
    };

    private FrameHud() {}
    static void install(Activity activity) {
        enabled = LaunchSettings.showFps(activity);
        if (!enabled) return;
        FrameLayout root = activity.findViewById(android.R.id.content);
        TextView view = new TextView(activity);
        int padding = Math.round(6 * activity.getResources().getDisplayMetrics().density);
        view.setText("FPS --  |  -- ms");
        view.setTextColor(Color.WHITE);
        view.setBackgroundColor(0x99000000);
        view.setTypeface(Typeface.MONOSPACE);
        view.setTextSize(13);
        view.setPadding(padding, padding / 2, padding, padding / 2);
        view.setClickable(false);
        view.setFocusable(false);
        view.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.START);
        params.leftMargin = params.topMargin = padding;
        root.addView(view, params);
        view.setOnApplyWindowInsetsListener((target, insets) -> {
            int left = insets.getSystemWindowInsetLeft();
            int top = insets.getSystemWindowInsetTop();
            if (Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                left = Math.max(left, insets.getDisplayCutout().getSafeInsetLeft());
                top = Math.max(top, insets.getDisplayCutout().getSafeInsetTop());
            }
            FrameLayout.LayoutParams position = (FrameLayout.LayoutParams) target.getLayoutParams();
            position.leftMargin = padding + left;
            position.topMargin = padding + top;
            target.setLayoutParams(position);
            return insets;
        });
        view.requestApplyInsets();
        label = new WeakReference<>(view);
    }
    static void setResumed(boolean value) {
        resumed = value;
        LATEST.set(null);
        MAIN.removeCallbacks(STALE);
        TextView view = label.get();
        if (view != null) view.setText("FPS --  |  -- ms");
        if (value) MAIN.postDelayed(STALE, 1000);
    }
    static void detach() {
        enabled = false;
        setResumed(false);
        MAIN.removeCallbacks(APPLY);
        QUEUED.set(false);
        label.clear();
    }
    // Called from the native render thread no more than four times per second.
    public static void report(float fps, float milliseconds) {
        if (!enabled || !resumed || !Float.isFinite(fps) || !Float.isFinite(milliseconds)) return;
        LATEST.set(new Sample(fps, milliseconds));
        if (QUEUED.compareAndSet(false, true)) MAIN.post(APPLY);
    }
}
