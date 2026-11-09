package org.wa2.port;

import android.app.Activity;
import android.app.AlertDialog;
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
    private static final int PICK_DIRECTORY = 0x5742, PICK_SUBTITLE=0x5743;
    private static final int TEXT = 0xffedf4f3, MUTED = 0xffa4b7b6, ACCENT = 0xff76d6c4;
    private TextView directory, status;
    private Button choose, start, script, font, refreshLanguages, subtitle;
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
        card.addView(text("选择存放原版 PAK 的文件夹，名称不限，并保留 IC 子目录。资源直接读取；存档和游戏配置也保存在所选目录，可放入 PC 版存档。", 13, MUTED), row(8));
        content.addView(text("剧情语言与字体", 18, TEXT), row(24));
        script = new Button(this);script.setAllCaps(false);script.setTextColor(TEXT);script.setBackgroundTintList(ColorStateList.valueOf(0xff30474b));content.addView(script,row(8));
        script.setOnClickListener(view -> chooseLanguage(false));
        font = new Button(this);font.setAllCaps(false);font.setTextColor(TEXT);font.setBackgroundTintList(ColorStateList.valueOf(0xff30474b));content.addView(font,row(6));
        font.setOnClickListener(view -> chooseLanguage(true));
        content.addView(text("默认沿用当前脚本和字库。把兼容的其他语言 PAK 放入所选游戏目录或 IC 后，分别选择脚本与配套字库。字库使用游戏 PAK 格式，不是手机 TTF/OTF 字体；更改后重新启动游戏生效。", 13, MUTED), row(6));
        refreshLanguages = new Button(this);refreshLanguages.setText("刷新资源列表");refreshLanguages.setAllCaps(false);refreshLanguages.setTextColor(TEXT);refreshLanguages.setBackgroundTintList(ColorStateList.valueOf(0xff30474b));content.addView(refreshLanguages,row(6));
        refreshLanguages.setOnClickListener(view -> prepare(ResourceDirectory.selectedTree(this),0,false));
        chineseSubtitles = new Switch(this);chineseSubtitles.setText("正篇补充字幕");chineseSubtitles.setTextColor(TEXT);chineseSubtitles.setTextSize(16);chineseSubtitles.setPadding(0,dp(12),0,dp(12));
        chineseSubtitles.setChecked(LaunchSettings.chineseSubtitles(this));chineseSubtitles.setOnCheckedChangeListener((button,checked) -> {if(!refreshing){status.setText("正在保存启动设置…");LaunchSettings.setChineseSubtitles(this,checked,this::saved);}});
        content.addView(chineseSubtitles,row(8));
        content.addView(text("控制只有声音的正篇补充字幕，不影响剧情正文和语音。默认开启，使用内置中文字幕；也可选择相同格式的外置 sub.yaml。",13,MUTED),row(4));
        subtitle=new Button(this);subtitle.setAllCaps(false);subtitle.setTextColor(TEXT);subtitle.setBackgroundTintList(ColorStateList.valueOf(0xff30474b));content.addView(subtitle,row(8));
        subtitle.setOnClickListener(view -> chooseSubtitle());
        content.addView(text("外置文件需为 UTF-8 的 sub.yaml 格式，包含 soundEffect 和 voice 分组，最多 1 MiB。选中后检查格式、编号、时间轴和文字；字幕设置在下次启动时生效。",13,MUTED),row(4));
        fps = new Switch(this); fps.setText("显示 FPS 和帧耗时"); fps.setTextColor(TEXT); fps.setTextSize(16); fps.setPadding(0, dp(12), 0, dp(12));
        fps.setChecked(LaunchSettings.showFps(this)); fps.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText("正在保存启动设置…");LaunchSettings.setShowFps(this, checked,this::saved);}});
        content.addView(fps, row(16));
        longPress = new Switch(this); longPress.setText("长按快进"); longPress.setTextColor(TEXT); longPress.setTextSize(16); longPress.setPadding(0, dp(12), 0, dp(12));
        longPress.setChecked(LaunchSettings.longPressFastForward(this)); longPress.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText("正在保存启动设置…");LaunchSettings.setLongPressFastForward(this, checked,this::saved);}});
        content.addView(longPress, row(4));
        content.addView(text("单指长按画面快进，松手停止。双指轻点画面可隐藏或显示右上角菜单按钮。", 13, MUTED), row(4));
        testMode = new Switch(this); testMode.setText("测试模式（解锁全部内容）"); testMode.setTextColor(TEXT); testMode.setTextSize(16); testMode.setPadding(0, dp(12), 0, dp(12));
        testMode.setChecked(LaunchSettings.testMode(this)); testMode.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText("正在保存启动设置…");LaunchSettings.setTestMode(this, checked,this::saved);}});
        content.addView(testMode, row(12));
        content.addView(text("解锁全部章节、结局、CG 差分、音乐和回想，并标记影片已观看、文本已读。使用独立测试存档，旧测试进度会自动补齐；关闭后恢复普通存档。切换后从本页重新启动生效。", 13, MUTED), row(4));
        start = new Button(this); start.setText("开始游戏"); start.setAllCaps(false); start.setTextColor(0xff102723);
        start.setBackgroundTintList(ColorStateList.valueOf(ACCENT)); content.addView(start, row(16));
        start.setOnClickListener(view -> prepare(ResourceDirectory.selectedTree(this), 0, true));
        status = text("", 13, MUTED); status.setMinHeight(dp(48)); content.addView(status, row(12));
        Button diagnostics=new Button(this);diagnostics.setText("复制上次运行诊断");diagnostics.setAllCaps(false);diagnostics.setTextColor(TEXT);
        diagnostics.setBackgroundTintList(ColorStateList.valueOf(0xff30474b));content.addView(diagnostics,row(8));
        diagnostics.setOnClickListener(view->{if(!busy)RuntimeDiagnostics.copy(this,status);});
        setContentView(scroll); refresh();Uri remembered=ResourceDirectory.selectedTree(this);if(remembered!=null)prepare(remembered,0,false);
    }
    private void saved(String problem) { runOnUiThread(() -> {if(isFinishing() || isDestroyed() || busy)return;refresh();status.setText(problem!=null?"保存启动设置失败："+problem:LaunchSettings.saving(this)?"正在保存启动设置…":"启动设置已保存到所选游戏目录，重新启动游戏生效。");}); }
    private void refresh() {
        Uri tree = ResourceDirectory.selectedTree(this);
        directory.setText(tree == null ? "尚未选择资源文件夹 · 名称不限" : ResourceDirectory.displayLocation(tree));
        choose.setEnabled(!busy); start.setEnabled(!busy && tree != null && profileReady);
        refreshing=true;fps.setChecked(LaunchSettings.showFps(this));longPress.setChecked(LaunchSettings.longPressFastForward(this));testMode.setChecked(LaunchSettings.testMode(this));chineseSubtitles.setChecked(LaunchSettings.chineseSubtitles(this));refreshing=false;
        for(Switch control:new Switch[]{fps,longPress,testMode,chineseSubtitles})control.setEnabled(!busy && profileReady);
        script.setText("剧情脚本："+(LaunchSettings.script(this).isEmpty()?"当前资源（默认）":LaunchSettings.script(this)));
        font.setText("游戏字库："+(LaunchSettings.font(this).isEmpty()?"当前资源（默认）":LaunchSettings.font(this)));
        script.setEnabled(!busy && profileReady);font.setEnabled(!busy && profileReady);refreshLanguages.setEnabled(!busy && profileReady);
        subtitle.setText("字幕来源："+ExternalSubtitles.display(LaunchSettings.subtitle(this)));subtitle.setEnabled(!busy && profileReady);
        if (!busy) status.setText(tree == null ? "请先选择并授权游戏资源文件夹。" : "启动设置、游戏配置和存档保存在所选游戏目录。");
    }
    private void chooseLanguage(boolean isFont) {
        if(busy || !profileReady)return;
        String[] names=ResourceDirectory.preparedPakNames(this);String selected=isFont?LaunchSettings.font(this):LaunchSettings.script(this);
        java.util.ArrayList<String> values=new java.util.ArrayList<>();values.add("");java.util.Collections.addAll(values,names);
        if(!values.contains(selected))values.add(selected);
        String[] labels=new String[values.size()];labels[0]="当前资源（默认）";
        for(int n=1;n<labels.length;++n)labels[n]=values.get(n)+(java.util.Arrays.asList(names).contains(values.get(n))?"":"（未找到）");
        new AlertDialog.Builder(this).setTitle(isFont?"选择配套字库 PAK":"选择剧情脚本 PAK")
            .setSingleChoiceItems(labels,values.indexOf(selected),(dialog,which) -> {
                String name=values.get(which);dialog.dismiss();status.setText("正在保存语言资源选择…");
                LaunchSettings.setArchive(this,isFont,name,this::saved);refresh();
                if(LaunchSettings.saving(this))status.setText("正在保存语言资源选择…");
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
