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
import static org.wa2.port.LauncherText.Key.*;

/** Configure the external resources before loading SDL or the game library. */
public final class LauncherActivity extends Activity {
    private static final int PICK_DIRECTORY = 0x5742, PICK_SUBTITLE=0x5743;
    private static final int TEXT = 0xffedf4f3, MUTED = 0xffa4b7b6, ACCENT = 0xff76d6c4;
    private TextView directory, status;
    private Button choose, start, language, subtitle, uiLanguage, diagnostics;
    private ScrollView root;
    private LauncherText ui;
    private Switch fps, longPress, testMode, chineseSubtitles;
    private boolean busy, launching, refreshing, profileReady;
    private int generation;

    private String t(LauncherText.Key key,Object... args){return ui.text(key,args);}
    private void showDialog(AlertDialog dialog){dialog.show();dialog.getWindow().getDecorView().setLayoutDirection(ui.rtl()?View.LAYOUT_DIRECTION_RTL:View.LAYOUT_DIRECTION_LTR);}
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
        LinearLayout.LayoutParams second=new LinearLayout.LayoutParams(0,-2,1);second.setMarginStart(dp(12));line.addView(right,second);
        parent.addView(line,row(top));
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ui=LauncherLocale.load(this);
        getWindow().setStatusBarColor(0xff10191c); getWindow().setNavigationBarColor(0xff10191c);
        root = new ScrollView(this);root.setFillViewport(true);root.setBackgroundColor(0xff10191c);
        LinearLayout outer = new LinearLayout(this);outer.setOrientation(LinearLayout.VERTICAL);outer.setGravity(Gravity.TOP|Gravity.CENTER_HORIZONTAL);
        outer.setPadding(dp(16),dp(4),dp(16),dp(8));root.addView(outer,new ScrollView.LayoutParams(-1,-1));
        LinearLayout content = new LinearLayout(this); content.setOrientation(LinearLayout.VERTICAL);
        int width = Math.min(getResources().getDisplayMetrics().widthPixels - dp(32), dp(560));
        outer.addView(content, new LinearLayout.LayoutParams(width, -2));
        LinearLayout heading=new LinearLayout(this);heading.setGravity(Gravity.CENTER_VERTICAL);content.addView(heading);
        TextView title = text("WHITE ALBUM2",22,TEXT);title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);title.setSingleLine();title.setEllipsize(TextUtils.TruncateAt.END);title.setTextDirection(View.TEXT_DIRECTION_LTR);
        heading.addView(title,new LinearLayout.LayoutParams(0,-2,1));
        uiLanguage=button("🌐");uiLanguage.setTextSize(22);uiLanguage.setOnClickListener(view -> chooseInterfaceLanguage());
        LinearLayout.LayoutParams localePosition=new LinearLayout.LayoutParams(dp(48),-2);localePosition.setMarginStart(dp(4));heading.addView(uiLanguage,localePosition);
        diagnostics=button(t(LOGS));
        LinearLayout.LayoutParams diagnosticPosition=new LinearLayout.LayoutParams(dp(64),-2);diagnosticPosition.setMarginStart(dp(4));heading.addView(diagnostics,diagnosticPosition);
        diagnostics.setOnClickListener(view->{if(!busy)RuntimeDiagnostics.copy(this,status);});
        LinearLayout card = new LinearLayout(this);card.setGravity(Gravity.CENTER_VERTICAL);card.setPadding(dp(12),dp(8),dp(12),dp(8));
        GradientDrawable background = new GradientDrawable(); background.setColor(0xff1b292d); background.setCornerRadius(dp(12)); card.setBackground(background);
        content.addView(card, row(8));
        directory = text("", 14, MUTED);directory.setTextIsSelectable(true);directory.setMaxLines(2);directory.setEllipsize(TextUtils.TruncateAt.END);
        card.addView(directory,new LinearLayout.LayoutParams(0,-2,1));
        choose = button(t(PICK_FOLDER));
        LinearLayout.LayoutParams directoryPosition=new LinearLayout.LayoutParams(dp(96),-2);directoryPosition.setMarginStart(dp(8));card.addView(choose,directoryPosition);
        choose.setOnClickListener(view -> pickDirectory());
        language = button("");
        language.setOnClickListener(view -> chooseLanguage());
        subtitle=button("");subtitle.setOnClickListener(view -> chooseSubtitle());pair(content,language,subtitle,8);
        chineseSubtitles = toggle(t(CAPTIONS));
        chineseSubtitles.setChecked(LaunchSettings.chineseSubtitles(this));chineseSubtitles.setOnCheckedChangeListener((button,checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setChineseSubtitles(this,checked,this::saved);}});
        content.addView(chineseSubtitles,row(4));
        fps = toggle(t(FPS));
        fps.setChecked(LaunchSettings.showFps(this)); fps.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setShowFps(this, checked,this::saved);}});
        longPress = toggle(t(LONG_PRESS));
        longPress.setChecked(LaunchSettings.longPressFastForward(this)); longPress.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setLongPressFastForward(this, checked,this::saved);}});
        pair(content,fps,longPress,0);
        testMode = toggle(t(TEST_MODE));
        testMode.setChecked(LaunchSettings.testMode(this)); testMode.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setTestMode(this, checked,this::saved);}});
        content.addView(testMode, row(0));
        start = button(t(START));start.setTextSize(16);start.setTextColor(0xff102723);
        start.setBackgroundTintList(ColorStateList.valueOf(ACCENT)); content.addView(start, row(8));
        start.setOnClickListener(view -> prepare(ResourceDirectory.selectedTree(this), 0, true));
        status = text("", 13, MUTED);status.setMinHeight(dp(20));content.addView(status,row(6));
        setContentView(root);refresh();Uri remembered=ResourceDirectory.selectedTree(this);if(remembered!=null)prepare(remembered,0,false);
    }
    private void saved(String problem) {runOnUiThread(() -> {if(isFinishing() || isDestroyed() || busy)return;refresh();status.setText(problem!=null?t(SAVE_ERROR,problem):LaunchSettings.saving(this)?t(SAVING):t(SAVED));});}
    private String folderLocation(Uri tree){
        try{String path=DocumentsContract.getTreeDocumentId(tree);if(path.startsWith("primary:"))path=path.substring(8);return path.isEmpty()?"/":path;}
        catch(RuntimeException error){return tree.toString();}
    }
    private void refresh() {
        Uri tree = ResourceDirectory.selectedTree(this);
        root.setLayoutDirection(ui.rtl()?View.LAYOUT_DIRECTION_RTL:View.LAYOUT_DIRECTION_LTR);
        String location=tree==null?t(FOLDER_NONE):folderLocation(tree);
        directory.setText(location);directory.setContentDescription(t(FOLDER_FMT,location));directory.setTooltipText(location);
        uiLanguage.setContentDescription(t(UI_LANGUAGE));uiLanguage.setTooltipText(t(UI_LANGUAGE)+" · "+ui.name());uiLanguage.setEnabled(!busy);
        diagnostics.setText(t(LOGS));diagnostics.setContentDescription(t(COPY_LOG));diagnostics.setEnabled(!busy);
        choose.setText(t(PICK_FOLDER));start.setText(t(START));
        fps.setText(t(FPS));fps.setContentDescription(t(FPS_DETAIL));longPress.setText(t(LONG_PRESS));testMode.setText(t(TEST_MODE));chineseSubtitles.setText(t(CAPTIONS));
        choose.setEnabled(!busy); start.setEnabled(!busy && tree != null && profileReady);
        refreshing=true;fps.setChecked(LaunchSettings.showFps(this));longPress.setChecked(LaunchSettings.longPressFastForward(this));testMode.setChecked(LaunchSettings.testMode(this));chineseSubtitles.setChecked(LaunchSettings.chineseSubtitles(this));refreshing=false;
        for(Switch control:new Switch[]{fps,longPress,testMode,chineseSubtitles})control.setEnabled(!busy && profileReady);
        language.setText(t(GAME_FMT,LaunchSettings.languageLabel(this)));language.setEnabled(!busy && profileReady);
        String source=LaunchSettings.subtitle(this);subtitle.setText(t(SUBTITLE_FMT,t(source.isEmpty()?BUILT_IN:EXTERNAL)));
        String sourceName=source.isEmpty()?t(BUILT_IN):source;subtitle.setContentDescription(t(SUBTITLE_FMT,sourceName));subtitle.setTooltipText(sourceName);subtitle.setEnabled(!busy && profileReady);
        if(!busy)status.setText(t(tree==null?NEED_FOLDER:LOCATION_HINT));
    }
    private void chooseInterfaceLanguage(){
        if(busy)return;
        showDialog(new AlertDialog.Builder(this).setTitle(t(UI_LANGUAGE)).setSingleChoiceItems(LauncherText.names(),ui.index(),(dialog,which) -> {
            dialog.dismiss();ui=LauncherText.at(which);LauncherLocale.save(this,ui);refresh();
        }).setNegativeButton(t(CANCEL),null).create());
    }
    private void chooseLanguage() {
        if(busy || !profileReady)return;
        showDialog(new AlertDialog.Builder(this).setTitle(t(PICK_GAME))
            .setSingleChoiceItems(new String[]{"简体中文","日本語"},LaunchSettings.language(this),(dialog,which) -> {
                dialog.dismiss();status.setText(t(SAVING));
                LaunchSettings.setLanguage(this,which,this::saved);refresh();
                if(LaunchSettings.saving(this))status.setText(t(SAVING));
            }).setNegativeButton(t(CANCEL),null).create());
    }
    private void pickDirectory() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        Uri saved = ResourceDirectory.selectedTree(this);
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, saved == null ? DocumentsContract.buildDocumentUri(ResourceLocation.AUTHORITY, ResourceLocation.DOCUMENT_ID) : saved);
        try { startActivityForResult(intent, PICK_DIRECTORY); }
        catch (RuntimeException error) {status.setText(t(PICKER_FAILED));}
    }
    private void chooseSubtitle() {
        if(busy || !profileReady)return;
        showDialog(new AlertDialog.Builder(this).setTitle(t(SUBTITLE_SOURCE)).setItems(new String[]{t(BUILT_IN),t(CHOOSE_SUBTITLE)},(dialog,which) -> {
            if(which==0){LaunchSettings.setSubtitle(this,"",this::saved);refresh();return;}
            Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT);intent.setType("*/*");intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION|Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,ResourceDirectory.selectedTree(this));
            try{startActivityForResult(intent,PICK_SUBTITLE);}catch(RuntimeException error){status.setText(t(PICKER_FAILED));}
        }).setNegativeButton(t(CANCEL),null).create());
    }
    private void validateSubtitle(Uri uri,int flags) {
        if(busy || !profileReady || uri==null)return;
        busy=true;final int token=++generation;refresh();status.setText(t(CHECKING_SUBTITLE));
        final android.content.Context application=getApplicationContext();
        new Thread(() -> {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);String problem=null,summary=null;
            try{
                if(!"content".equals(uri.getScheme()))throw new java.io.IOException(t(REQUIRE_LOCAL_SUBTITLE));
                summary=ExternalSubtitles.validate(application,uri.toString());
                if((flags&Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)==0 || (flags&Intent.FLAG_GRANT_READ_URI_PERMISSION)==0)throw new java.io.IOException(t(REQUIRE_LOCAL_SUBTITLE));
                application.getContentResolver().takePersistableUriPermission(uri,Intent.FLAG_GRANT_READ_URI_PERMISSION);
            }catch(Exception | LinkageError error){problem=error.getMessage();if(problem==null)problem=error.toString();}
            final String failure=problem,details=summary;
            runOnUiThread(() -> {
                if(isFinishing() || isDestroyed() || generation!=token)return;
                busy=false;refresh();
                if(failure!=null){status.setText(t(SUBTITLE_REJECTED,failure));return;}
                LaunchSettings.setSubtitle(this,uri.toString(),error -> runOnUiThread(() -> {if(isFinishing() || isDestroyed() || busy)return;refresh();status.setText(error==null?t(SUBTITLE_OK,details):t(SAVE_ERROR,error));}));
                if(LaunchSettings.saving(this)){refresh();status.setText(t(SAVING));}
            });
        },"WA2 subtitle validation").start();
    }
    private void prepare(Uri tree, int flags, boolean launch) {
        if (busy || tree == null) return;
        busy = true; profileReady=false; final int token = ++generation; refresh();status.setText(t(CHECKING_RESOURCES));
        final android.content.Context application = getApplicationContext();
        new Thread(() -> {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
            String problem = null; int count = 0;boolean ready=false;
            try {Uri previous=ResourceDirectory.selectedTree(application);count = ResourceDirectory.prepareSelection(application, tree, flags);LaunchSettings.prepare(application,tree,tree.equals(previous));ready=true;if(launch){ResourceDirectory.validatePreparedLanguages(application,LaunchSettings.script(application),LaunchSettings.font(application));String external=LaunchSettings.subtitle(application);if(LaunchSettings.chineseSubtitles(application) && !external.isEmpty())ExternalSubtitles.validate(application,external);}}
            catch (LinkageError error) {problem="Subtitle validation module: "+error.getMessage();}
            catch (Exception error) {problem=error.getMessage();if(problem==null)problem=error.toString();}
            final String failure = problem; final int found = count;final boolean loaded=ready;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed() || generation != token) return;
                busy = false;profileReady=loaded;refresh();
                if(failure!=null){status.setText(t(OPERATION_FAILED,failure)+(loaded?"":"\n"+t(NEED_REAUTHORIZE)));return;}
                status.setText(t(READY,found));
                if(launch){Intent session=new Intent(this,Wa2Activity.class);LaunchSettings.applySession(this,session);launching=true;busy=true;refresh();status.setText(t(STARTING));startActivity(session);}
            });
        }, "WA2 launcher resources").start();
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        if(request==PICK_SUBTITLE){if(result==RESULT_OK && data!=null && data.getData()!=null)validateSubtitle(data.getData(),data.getFlags());else status.setText(t(CANCELLED_SUBTITLE));return;}
        if (request != PICK_DIRECTORY) { super.onActivityResult(request, result, data); return; }
        if (result == RESULT_OK && data != null && data.getData() != null) prepare(data.getData(), data.getFlags(), false);
        else status.setText(t(CANCELLED_FOLDER));
    }
    @Override protected void onDestroy() { ++generation; super.onDestroy(); }
    @Override protected void onResume() {
        super.onResume();
        if (launching) { launching = false; busy = false;refresh();Uri tree=ResourceDirectory.selectedTree(this);if(tree!=null)prepare(tree,0,false); }
    }
}
