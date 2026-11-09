package org.wa2.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Process;
import android.provider.DocumentsContract;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

/** Configure the external resources before loading SDL or the game library. */
public final class LauncherActivity extends Activity {
    private static final int PICK_DIRECTORY = 0x5742, PICK_SUBTITLE=0x5743;
    private static final int TEXT = 0xffedf4f3, MUTED = 0xffa4b7b6, ACCENT = 0xff76d6c4;
    private TextView directory, status;
    private Button choose, start, language, subtitle;
    private Switch fps, longPress, testMode, chineseSubtitles;
    private boolean busy, launching, refreshing, profileReady;
    private int generation;

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        return view;
    }
    private LinearLayout.LayoutParams row(int top) {
        LinearLayout.LayoutParams result = new LinearLayout.LayoutParams(-1, -2); result.topMargin = dp(top); return result;
    }
    private Button button(String label) {
        Button view=new Button(this);view.setText(label);view.setAllCaps(false);view.setTextSize(14);view.setTextColor(TEXT);
        view.setMinWidth(0);view.setMinimumWidth(0);view.setMinHeight(dp(48));view.setMinimumHeight(dp(48));
        view.setPadding(dp(12),dp(4),dp(12),dp(4));view.setMaxLines(2);view.setEllipsize(TextUtils.TruncateAt.END);
        view.setBackgroundTintList(ColorStateList.valueOf(0xff30474b));return view;
    }
    private Switch toggle(String label) {
        Switch view=new Switch(this);view.setText(label);view.setTextColor(TEXT);view.setTextSize(16);
        view.setMinHeight(dp(48));view.setPadding(0,dp(4),0,dp(4));view.setSwitchPadding(dp(8));return view;
    }
    private void pair(LinearLayout parent,View left,View right,int top) {
        LinearLayout line=new LinearLayout(this);line.setGravity(Gravity.CENTER_VERTICAL);
        line.addView(left,new LinearLayout.LayoutParams(0,-2,1));
        LinearLayout.LayoutParams second=new LinearLayout.LayoutParams(0,-2,1);second.leftMargin=dp(12);line.addView(right,second);
        parent.addView(line,row(top));
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(0xff10191c); getWindow().setNavigationBarColor(0xff10191c);
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.setBackgroundColor(0xff10191c);
        LinearLayout outer = new LinearLayout(this); outer.setOrientation(LinearLayout.VERTICAL); outer.setGravity(Gravity.CENTER);
        outer.setPadding(dp(16), dp(12), dp(16), dp(12)); scroll.addView(outer, new ScrollView.LayoutParams(-1, -1));
        LinearLayout content = new LinearLayout(this); content.setOrientation(LinearLayout.VERTICAL);
        int width = Math.min(getResources().getDisplayMetrics().widthPixels - dp(32), dp(560));
        outer.addView(content, new LinearLayout.LayoutParams(width, -2));
        LinearLayout heading=new LinearLayout(this);heading.setGravity(Gravity.CENTER_VERTICAL);content.addView(heading);
        TextView title = text("WHITE ALBUM2", 26, TEXT); title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);title.setSingleLine();title.setEllipsize(TextUtils.TruncateAt.END);
        heading.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        Button diagnostics=button("复制诊断");diagnostics.setContentDescription("复制上次运行诊断");
        LinearLayout.LayoutParams diagnosticPosition=new LinearLayout.LayoutParams(dp(88),-2);diagnosticPosition.leftMargin=dp(8);heading.addView(diagnostics,diagnosticPosition);
        diagnostics.setOnClickListener(view->{if(!busy)RuntimeDiagnostics.copy(this,status);});
        LinearLayout card = new LinearLayout(this);card.setGravity(Gravity.CENTER_VERTICAL);card.setPadding(dp(12),dp(8),dp(12),dp(8));
        GradientDrawable background = new GradientDrawable(); background.setColor(0xff1b292d); background.setCornerRadius(dp(12)); card.setBackground(background);
        content.addView(card, row(8));
        directory = text("", 14, MUTED);directory.setTextIsSelectable(true);directory.setMaxLines(2);directory.setEllipsize(TextUtils.TruncateAt.END);
        card.addView(directory,new LinearLayout.LayoutParams(0,-2,1));
        choose = button("选择目录");
        LinearLayout.LayoutParams directoryPosition=new LinearLayout.LayoutParams(dp(96),-2);directoryPosition.leftMargin=dp(8);card.addView(choose,directoryPosition);
        choose.setOnClickListener(view -> pickDirectory());
        language = button("");
        language.setOnClickListener(view -> chooseLanguage());
        subtitle=button("");subtitle.setOnClickListener(view -> chooseSubtitle());pair(content,language,subtitle,8);
        chineseSubtitles = toggle("正篇补充字幕");
        chineseSubtitles.setChecked(LaunchSettings.chineseSubtitles(this));chineseSubtitles.setOnCheckedChangeListener((button,checked) -> {if(!refreshing){status.setText("正在保存启动设置…");LaunchSettings.setChineseSubtitles(this,checked,this::saved);}});
        content.addView(chineseSubtitles,row(4));
        fps = toggle("显示 FPS");fps.setContentDescription("显示 FPS 和帧耗时");
        fps.setChecked(LaunchSettings.showFps(this)); fps.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText("正在保存启动设置…");LaunchSettings.setShowFps(this, checked,this::saved);}});
        longPress = toggle("长按快进");
        longPress.setChecked(LaunchSettings.longPressFastForward(this)); longPress.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText("正在保存启动设置…");LaunchSettings.setLongPressFastForward(this, checked,this::saved);}});
        pair(content,fps,longPress,0);
        testMode = toggle("测试模式（解锁全部）");
        testMode.setChecked(LaunchSettings.testMode(this)); testMode.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText("正在保存启动设置…");LaunchSettings.setTestMode(this, checked,this::saved);}});
        content.addView(testMode, row(0));
        start = button("开始游戏");start.setTextSize(16);start.setTextColor(0xff102723);
        start.setBackgroundTintList(ColorStateList.valueOf(ACCENT)); content.addView(start, row(8));
        start.setOnClickListener(view -> prepare(ResourceDirectory.selectedTree(this), 0, true));
        status = text("", 13, MUTED);status.setMinHeight(dp(20));content.addView(status,row(6));
        setContentView(scroll); refresh();Uri remembered=ResourceDirectory.selectedTree(this);if(remembered!=null)prepare(remembered,0,false);
    }
    private void saved(String problem) { runOnUiThread(() -> {if(isFinishing() || isDestroyed() || busy)return;refresh();status.setText(problem!=null?"保存启动设置失败："+problem:LaunchSettings.saving(this)?"正在保存启动设置…":"已保存，下次启动生效。");}); }
    private void refresh() {
        Uri tree = ResourceDirectory.selectedTree(this);
        String location=tree==null?"未选择游戏目录":ResourceDirectory.displayLocation(tree);
        directory.setText(location);directory.setContentDescription("游戏目录："+location);directory.setTooltipText(location);
        choose.setEnabled(!busy); start.setEnabled(!busy && tree != null && profileReady);
        refreshing=true;fps.setChecked(LaunchSettings.showFps(this));longPress.setChecked(LaunchSettings.longPressFastForward(this));testMode.setChecked(LaunchSettings.testMode(this));chineseSubtitles.setChecked(LaunchSettings.chineseSubtitles(this));refreshing=false;
        for(Switch control:new Switch[]{fps,longPress,testMode,chineseSubtitles})control.setEnabled(!busy && profileReady);
        language.setText("语言："+LaunchSettings.languageLabel(this));language.setEnabled(!busy && profileReady);
        String source=LaunchSettings.subtitle(this);subtitle.setText("字幕："+(source.isEmpty()?"内置":"外置"));
        subtitle.setContentDescription("字幕来源："+ExternalSubtitles.display(source));subtitle.setTooltipText(ExternalSubtitles.display(source));subtitle.setEnabled(!busy && profileReady);
        if (!busy) status.setText(tree == null ? "请先选择游戏目录。" : "设置和存档保存在所选目录。");
    }
    private void chooseLanguage() {
        if(busy || !profileReady)return;
        new AlertDialog.Builder(this).setTitle("选择游戏语言")
            .setSingleChoiceItems(new String[]{"简体中文","日本語"},LaunchSettings.language(this),(dialog,which) -> {
                dialog.dismiss();status.setText("正在保存游戏语言…");
                LaunchSettings.setLanguage(this,which,this::saved);refresh();
                if(LaunchSettings.saving(this))status.setText("正在保存游戏语言…");
            }).setNegativeButton("取消",null).show();
    }
    private void pickDirectory() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        Uri saved = ResourceDirectory.selectedTree(this);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, saved == null ? DocumentsContract.buildDocumentUri(ResourceLocation.AUTHORITY, ResourceLocation.DOCUMENT_ID) : saved);
        try { startActivityForResult(intent, PICK_DIRECTORY); }
        catch (RuntimeException error) { status.setText("无法打开系统文件夹选择器。"); }
    }
    private void chooseSubtitle() {
        if(busy || !profileReady)return;
        new AlertDialog.Builder(this).setTitle("字幕来源").setItems(new String[]{"内置字幕（默认）","选择外置 sub.yaml"},(dialog,which) -> {
            if(which==0){LaunchSettings.setSubtitle(this,"",this::saved);refresh();return;}
            Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.setType("*/*");intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,ResourceDirectory.selectedTree(this));
            try{startActivityForResult(intent,PICK_SUBTITLE);}catch(RuntimeException error){status.setText("无法打开系统字幕文件选择器。");}
        }).setNegativeButton("取消",null).show();
    }
    private void validateSubtitle(Uri uri,int flags) {
        if(busy || !profileReady || uri==null)return;
        busy=true;final int token=++generation;refresh();status.setText("正在检查外置字幕格式、时间轴和文字…");
        final android.content.Context application=getApplicationContext();
        new Thread(() -> {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);String problem=null,summary=null;
            try{
                if(!"content".equals(uri.getScheme()))throw new java.io.IOException("请从系统文件选择器选择字幕文件。");
                summary=ExternalSubtitles.validate(application,uri.toString());
                if((flags&Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)==0 || (flags&Intent.FLAG_GRANT_READ_URI_PERMISSION)==0)throw new java.io.IOException("此文件来源不支持持久读取授权，请使用系统文件目录中的副本。");
                application.getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);
            }catch(Exception | LinkageError error){problem=error.getMessage();if(problem==null)problem=error.toString();}
            final String failure=problem,details=summary;
            runOnUiThread(() -> {
                if(isFinishing() || isDestroyed() || generation!=token)return;
                busy=false;refresh();
                if(failure!=null){status.setText("外置字幕未启用，保留原选择：\n"+failure);return;}
                LaunchSettings.setSubtitle(this,uri.toString(),error -> runOnUiThread(() -> {if(isFinishing() || isDestroyed() || busy)return;refresh();status.setText(error==null?"外置字幕校验通过并已保存："+details:"保存字幕设置失败："+error);}));
                if(LaunchSettings.saving(this)){refresh();status.setText("字幕校验通过，正在保存选择…");}
            });
        },"WA2 subtitle validation").start();
    }
    private void prepare(Uri tree, int flags, boolean launch) {
        if (busy || tree == null) return;
        busy = true; profileReady=false; final int token = ++generation; refresh(); status.setText("正在检查资源目录和启动配置…");
        final android.content.Context application = getApplicationContext();
        new Thread(() -> {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
            String problem = null; int count = 0;boolean ready=false;
            try {Uri previous=ResourceDirectory.selectedTree(application);count = ResourceDirectory.prepareSelection(application, tree, flags);LaunchSettings.prepare(application,tree,tree.equals(previous));ready=true;if(launch){ResourceDirectory.validatePreparedLanguages(application,LaunchSettings.script(application),LaunchSettings.font(application));String external=LaunchSettings.subtitle(application);if(LaunchSettings.chineseSubtitles(application) && !external.isEmpty())ExternalSubtitles.validate(application,external);}}
            catch (LinkageError error) {problem="字幕校验模块加载失败："+error.getMessage();}
            catch (Exception error) { problem = error.getMessage(); if (problem == null) problem = "无法读取所选资源目录。"; }
            final String failure = problem; final int found = count;final boolean loaded=ready;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed() || generation != token) return;
                busy = false;profileReady=loaded;refresh();
                if (failure != null) { status.setText(failure + (loaded?"":"\n请重新选择并授权可读写的游戏资源文件夹，名称不限。")); return; }
                status.setText("已找到 " + found + " 个资源包。");
                if (launch) {Intent session=new Intent(this,Wa2Activity.class);LaunchSettings.applySession(this,session);launching = true; busy = true; refresh(); status.setText("正在启动游戏…"); startActivity(session);}
            });
        }, "WA2 launcher resources").start();
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        if(request==PICK_SUBTITLE){if(result==RESULT_OK && data!=null && data.getData()!=null)validateSubtitle(data.getData(),data.getFlags());else status.setText("已取消，保留原来的字幕选择。");return;}
        if (request != PICK_DIRECTORY) { super.onActivityResult(request, result, data); return; }
        if (result == RESULT_OK && data != null && data.getData() != null) prepare(data.getData(), data.getFlags(), false);
        else status.setText("已取消选择，保留原来的资源目录。");
    }
    @Override protected void onDestroy() { ++generation; super.onDestroy(); }
    @Override protected void onResume() {
        super.onResume();
        if (launching) { launching = false; busy = false;refresh();Uri tree=ResourceDirectory.selectedTree(this);if(tree!=null)prepare(tree,0,false); }
    }
}
