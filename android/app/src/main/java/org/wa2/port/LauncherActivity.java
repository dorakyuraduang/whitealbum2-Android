package org.wa2.port;

import android.app.Activity;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

/** Configure the external resources before loading SDL or the game library. */
public final class LauncherActivity extends Activity {
    private static final int PICK_DIRECTORY = 0x5742;
    private static final int TEXT = 0xffedf4f3, MUTED = 0xffa4b7b6, ACCENT = 0xff76d6c4;
    private TextView directory, status;
    private Button choose, start;
    private boolean busy, launching;
    private int generation;

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        return view;
    }
    private LinearLayout.LayoutParams row(int top) {
        LinearLayout.LayoutParams result = new LinearLayout.LayoutParams(-1, -2); result.topMargin = dp(top); return result;
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(0xff10191c); getWindow().setNavigationBarColor(0xff10191c);
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.setBackgroundColor(0xff10191c);
        LinearLayout outer = new LinearLayout(this); outer.setOrientation(LinearLayout.VERTICAL); outer.setGravity(Gravity.CENTER);
        outer.setPadding(dp(24), dp(28), dp(24), dp(28)); scroll.addView(outer, new ScrollView.LayoutParams(-1, -1));
        LinearLayout content = new LinearLayout(this); content.setOrientation(LinearLayout.VERTICAL);
        int width = Math.min(getResources().getDisplayMetrics().widthPixels - dp(48), dp(560));
        outer.addView(content, new LinearLayout.LayoutParams(width, -2));
        TextView title = text("WHITE ALBUM2", 30, TEXT); title.setTypeface(Typeface.DEFAULT, Typeface.BOLD); content.addView(title);
        content.addView(text("启动设置", 16, MUTED), row(6));
        LinearLayout card = new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL); card.setPadding(dp(20), dp(20), dp(20), dp(20));
        GradientDrawable background = new GradientDrawable(); background.setColor(0xff1b292d); background.setCornerRadius(dp(16)); card.setBackground(background);
        content.addView(card, row(28));
        card.addView(text("游戏资源", 18, TEXT));
        directory = text("", 14, MUTED); directory.setTextIsSelectable(true); card.addView(directory, row(10));
        choose = new Button(this); choose.setText("选择资源文件夹"); choose.setAllCaps(false); choose.setTextColor(TEXT);
        choose.setBackgroundTintList(ColorStateList.valueOf(0xff30474b)); card.addView(choose, row(14));
        choose.setOnClickListener(view -> pickDirectory());
        card.addView(text("选择存放原版 PAK 的 Wa2Res 文件夹，并保留 IC 子目录。资源直接读取，不复制到应用内。", 13, MUTED), row(8));
        Switch fps = new Switch(this); fps.setText("显示 FPS 和帧耗时"); fps.setTextColor(TEXT); fps.setTextSize(16); fps.setPadding(0, dp(12), 0, dp(12));
        fps.setChecked(LaunchSettings.showFps(this)); fps.setOnCheckedChangeListener((button, checked) -> LaunchSettings.setShowFps(this, checked));
        content.addView(fps, row(16));
        start = new Button(this); start.setText("开始游戏"); start.setAllCaps(false); start.setTextColor(0xff102723);
        start.setBackgroundTintList(ColorStateList.valueOf(ACCENT)); content.addView(start, row(16));
        start.setOnClickListener(view -> prepare(ResourceDirectory.selectedTree(this), 0, true));
        status = text("", 13, MUTED); status.setMinHeight(dp(48)); content.addView(status, row(12));
        setContentView(scroll); refresh();
    }
    private void refresh() {
        Uri tree = ResourceDirectory.selectedTree(this);
        directory.setText(tree == null ? "尚未选择 · 默认位置：内部存储 / Wa2Res" : ResourceDirectory.displayLocation(tree));
        choose.setEnabled(!busy); start.setEnabled(!busy && tree != null);
        if (!busy) status.setText(tree == null ? "请先选择并授权游戏资源文件夹。" : "设置会自动保存。点击开始游戏后读取资源。");
    }
    private void pickDirectory() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        Uri saved = ResourceDirectory.selectedTree(this);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, saved == null ? DocumentsContract.buildDocumentUri(ResourceLocation.AUTHORITY, ResourceLocation.DOCUMENT_ID) : saved);
        try { startActivityForResult(intent, PICK_DIRECTORY); }
        catch (RuntimeException error) { status.setText("无法打开系统文件夹选择器。"); }
    }
    private void prepare(Uri tree, int flags, boolean launch) {
        if (busy || tree == null) return;
        busy = true; final int token = ++generation; refresh(); status.setText("正在检查资源目录…");
        final android.content.Context application = getApplicationContext();
        new Thread(() -> {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
            String problem = null; int count = 0;
            try { count = ResourceDirectory.prepareSelection(application, tree, flags); }
            catch (Exception error) { problem = error.getMessage(); if (problem == null) problem = "无法读取所选资源目录。"; }
            final String failure = problem; final int found = count;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed() || generation != token) return;
                busy = false; refresh();
                if (failure != null) { status.setText(failure + "\n请重新选择可读取的 Wa2Res 文件夹。"); return; }
                status.setText("已找到 " + found + " 个资源包。");
                if (launch) { launching = true; busy = true; refresh(); status.setText("正在启动游戏…"); startActivity(new Intent(this, Wa2Activity.class)); }
            });
        }, "WA2 launcher resources").start();
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        if (request != PICK_DIRECTORY) { super.onActivityResult(request, result, data); return; }
        if (result == RESULT_OK && data != null && data.getData() != null) prepare(data.getData(), data.getFlags(), false);
        else status.setText("已取消选择，保留原来的资源目录。");
    }
    @Override protected void onDestroy() { ++generation; super.onDestroy(); }
    @Override protected void onResume() {
        super.onResume();
        if (launching) { launching = false; busy = false; refresh(); }
    }
}
