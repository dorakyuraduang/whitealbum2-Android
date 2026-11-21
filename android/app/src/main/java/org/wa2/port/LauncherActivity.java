package org.wa2.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.Process;
import android.provider.DocumentsContract;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import java.util.List;
import org.wa2.launcher.GameFolderSelection;
import static org.wa2.port.LauncherText.Key.*;

/** Configure the external resources before loading SDL or the game library. */
public final class LauncherActivity extends Activity {
    private static final int PICK_DIRECTORY = 0x5742, PICK_SUBTITLE=0x5743, PICK_SPECIAL=0x5744;
    private static final int TEXT = 0xffedf4f3, MUTED = 0xffa4b7b6, ACCENT = 0xff76d6c4;
    private TextView directory, status, languagePaths;
    private Button choose, start, language, subtitle, uiLanguage, diagnostics;
    private Button mainConfig, specialConfig;
    private LaunchTarget selectedTarget=LaunchTarget.MAIN;
    private GameFolderSelection gameFolders;
    private ScrollView root;
    private LauncherText ui;
    private Switch fps, limit60Fps, longPress, testMode, chineseSubtitles, showMenu;
    private boolean busy, launching, refreshing, profileReady;
    private int generation;

    LaunchTarget configurationTarget(){return selectedTarget;}
    Uri configurationTree(){return selectedTarget==LaunchTarget.MAIN?ResourceDirectory.selectedTree(this):gameFolders.saved(GameFolderSelection.SPECIAL);}
    private void selectConfiguration(LaunchTarget target){
        if(busy || target==selectedTarget)return;
        if(LaunchSettings.saving(this)){status.setText(t(SAVING));return;}
        selectedTarget=target;profileReady=false;++generation;
        getSharedPreferences("launcher-view",MODE_PRIVATE).edit().putString("configuration",target.id).apply();
        refresh();Uri tree=configurationTree();if(tree!=null)prepare(tree,0,false);
    }

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
        view.setPadding(dp(12),dp(4),dp(12),dp(4));view.setSingleLine(false);view.setEllipsize(null);
        view.setBackgroundTintList(ColorStateList.valueOf(0xff30474b));return view;
    }
    private Switch toggle(String label) {
        Switch view=new Switch(this);view.setText(label);view.setTextColor(TEXT);view.setTextSize(16);
        view.setMinHeight(dp(48));view.setPadding(0,dp(4),0,dp(4));view.setSwitchPadding(dp(8));return view;
    }
    private void fitContent(LinearLayout content){
        if(root.getWidth()==0)return;
        int available=Math.max(0,root.getWidth()-root.getPaddingLeft()-root.getPaddingRight()-dp(32));
        int width=Math.min(available,dp(560));
        android.view.ViewGroup.LayoutParams position=content.getLayoutParams();
        if(position.width!=width){position.width=width;content.setLayoutParams(position);}
    }
    private void installSafeArea(LinearLayout content){
        // Own all four inset edges so the decor and this root cannot both pad
        // the same status/navigation bar on older Android versions.
        if(Build.VERSION.SDK_INT>=30)getWindow().setDecorFitsSystemWindows(false);
        else{
            View decor=getWindow().getDecorView();
            decor.setSystemUiVisibility(decor.getSystemUiVisibility()|View.SYSTEM_UI_FLAG_LAYOUT_STABLE|View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN|View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        if(Build.VERSION.SDK_INT>=28){
            WindowManager.LayoutParams attributes=getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attributes);
        }
        root.setClipToPadding(true);
        root.setOnApplyWindowInsetsListener((view,insets) -> {
            int left,top,right,bottom;
            if(Build.VERSION.SDK_INT>=30){
                android.graphics.Insets safe=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());
                left=safe.left;top=safe.top;right=safe.right;bottom=safe.bottom;
            }else{
                left=insets.getSystemWindowInsetLeft();top=insets.getSystemWindowInsetTop();right=insets.getSystemWindowInsetRight();bottom=insets.getSystemWindowInsetBottom();
                if(Build.VERSION.SDK_INT>=28 && insets.getDisplayCutout()!=null){
                    android.view.DisplayCutout cutout=insets.getDisplayCutout();
                    left=Math.max(left,cutout.getSafeInsetLeft());top=Math.max(top,cutout.getSafeInsetTop());
                    right=Math.max(right,cutout.getSafeInsetRight());bottom=Math.max(bottom,cutout.getSafeInsetBottom());
                }
            }
            // Absolute edges keep a physical left/right cutout on the same
            // side when the interface language changes to RTL.
            if(root.getPaddingLeft()!=left || root.getPaddingTop()!=top || root.getPaddingRight()!=right || root.getPaddingBottom()!=bottom)root.setPadding(left,top,right,bottom);
            fitContent(content);return insets;
        });
        root.addOnLayoutChangeListener((view,left,top,right,bottom,oldLeft,oldTop,oldRight,oldBottom) -> fitContent(content));
        root.addOnAttachStateChangeListener(new View.OnAttachStateChangeListener(){
            @Override public void onViewAttachedToWindow(View view){view.requestApplyInsets();}
            @Override public void onViewDetachedFromWindow(View view){}
        });
        root.requestApplyInsets();
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ui=LauncherLocale.load(this);
        gameFolders = new GameFolderSelection(this);
        selectedTarget="special".equals(getSharedPreferences("launcher-view",MODE_PRIVATE).getString("configuration","main"))?LaunchTarget.SPECIAL:LaunchTarget.MAIN;
        getWindow().setStatusBarColor(0xff10191c); getWindow().setNavigationBarColor(0xff10191c);
        root = new ScrollView(this);root.setFillViewport(true);root.setBackgroundColor(0xff10191c);
        LinearLayout outer = new LinearLayout(this);outer.setOrientation(LinearLayout.VERTICAL);outer.setGravity(Gravity.TOP|Gravity.CENTER_HORIZONTAL);
        outer.setPadding(dp(16),dp(4),dp(16),dp(8));root.addView(outer,new ScrollView.LayoutParams(-1,-1));
        LinearLayout content = new LinearLayout(this); content.setOrientation(LinearLayout.VERTICAL);
        outer.addView(content,new LinearLayout.LayoutParams(-1,-2));
        TextView title = text("WHITE ALBUM2",22,TEXT);title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);title.setSingleLine();title.setEllipsize(TextUtils.TruncateAt.END);title.setTextDirection(View.TEXT_DIRECTION_LTR);
        content.addView(title);
        uiLanguage=button(t(UI_LANGUAGE));uiLanguage.setOnClickListener(view -> chooseInterfaceLanguage());content.addView(uiLanguage,row(4));
        LinearLayout configurations=new LinearLayout(this);configurations.setBaselineAligned(false);content.addView(configurations,row(4));
        mainConfig=button(t(MAIN_CONFIG));mainConfig.setOnClickListener(view -> selectConfiguration(LaunchTarget.MAIN));configurations.addView(mainConfig,new LinearLayout.LayoutParams(0,-2,1));
        specialConfig=button("SC");specialConfig.setOnClickListener(view -> selectConfiguration(LaunchTarget.SPECIAL));
        LinearLayout.LayoutParams scPosition=new LinearLayout.LayoutParams(0,-2,1);scPosition.setMarginStart(dp(4));configurations.addView(specialConfig,scPosition);
        LinearLayout card = new LinearLayout(this);card.setOrientation(LinearLayout.VERTICAL);card.setPadding(dp(12),dp(8),dp(12),dp(8));
        GradientDrawable background = new GradientDrawable(); background.setColor(0xff1b292d); background.setCornerRadius(dp(12)); card.setBackground(background);
        content.addView(card, row(8));
        directory = text("", 14, MUTED);directory.setTextIsSelectable(true);directory.setMaxLines(2);directory.setEllipsize(TextUtils.TruncateAt.END);
        card.addView(directory,row(0));
        choose = button(t(PICK_FOLDER));
        card.addView(choose,row(4));
        choose.setOnClickListener(view -> pickDirectory());
        language = button("");
        language.setOnClickListener(view -> chooseLanguage());content.addView(language,row(8));
        languagePaths=text("",13,MUTED);languagePaths.setTextIsSelectable(true);content.addView(languagePaths,row(4));
        subtitle=button("");subtitle.setOnClickListener(view -> chooseSubtitle());content.addView(subtitle,row(4));
        chineseSubtitles = toggle(t(CAPTIONS));
        chineseSubtitles.setChecked(LaunchSettings.chineseSubtitles(this));chineseSubtitles.setOnCheckedChangeListener((button,checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setChineseSubtitles(this,checked,this::saved);}});
        content.addView(chineseSubtitles,row(4));
        fps = toggle(t(FPS));
        fps.setChecked(LaunchSettings.showFps(this)); fps.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setShowFps(this, checked,this::saved);}});
        content.addView(fps,row(0));
        limit60Fps=toggle(t(LIMIT_60_FPS));
        limit60Fps.setOnCheckedChangeListener((button,checked) -> {
            if(!refreshing){status.setText(t(SAVING));LaunchSettings.setLimit60Fps(this,checked,this::saved);}
        });
        content.addView(limit60Fps,row(0));
        longPress = toggle(t(LONG_PRESS));
        longPress.setChecked(LaunchSettings.longPressFastForward(this)); longPress.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setLongPressFastForward(this, checked,this::saved);}});
        content.addView(longPress,row(0));
        showMenu=toggle(t(SHOW_MENU));showMenu.setChecked(LaunchSettings.showMenu(this));
        showMenu.setOnCheckedChangeListener((button,checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setShowMenu(this,checked,this::saved);}});
        content.addView(showMenu,row(0));
        testMode = toggle(t(TEST_MODE));
        testMode.setChecked(LaunchSettings.testMode(this)); testMode.setOnCheckedChangeListener((button, checked) -> {if(!refreshing){status.setText(t(SAVING));LaunchSettings.setTestMode(this, checked,this::saved);}});
        content.addView(testMode, row(0));
        LinearLayout launchButtons = new LinearLayout(this);
        launchButtons.setBaselineAligned(false);
        content.addView(launchButtons, row(8));
        start = button(t(START));start.setTextSize(16);start.setTextColor(0xff102723);
        start.setBackgroundTintList(ColorStateList.valueOf(ACCENT));
        launchButtons.addView(start, new LinearLayout.LayoutParams(0, -2, 1));
        start.setOnClickListener(view -> prepare(configurationTree(), 0, true));
        diagnostics=button(t(COPY_LOG));content.addView(diagnostics,row(4));
        diagnostics.setOnClickListener(view->{if(!busy)RuntimeDiagnostics.copy(this,status);});
        status = text("", 13, MUTED);status.setMinHeight(dp(20));content.addView(status,row(6));
        installSafeArea(content);setContentView(root);refresh();Uri remembered=configurationTree();if(remembered!=null)prepare(remembered,0,false);
    }
    private void saved(String problem) {runOnUiThread(() -> {if(isFinishing() || isDestroyed() || busy)return;refresh();status.setText(problem!=null?t(SAVE_ERROR,problem):LaunchSettings.saving(this)?t(SAVING):t(SAVED));});}
    private String folderLocation(Uri tree){
        try{String path=DocumentsContract.getTreeDocumentId(tree);if(path.startsWith("primary:"))path=path.substring(8);return path.isEmpty()?"/":path;}
        catch(RuntimeException error){return tree.toString();}
    }
    private void refresh() {
        Uri tree = configurationTree();
        root.setLayoutDirection(ui.rtl()?View.LAYOUT_DIRECTION_RTL:View.LAYOUT_DIRECTION_LTR);
        String location=tree==null?t(FOLDER_NONE):folderLocation(tree);
        directory.setText(location);directory.setContentDescription(t(FOLDER_FMT,location));directory.setTooltipText(location);
        uiLanguage.setText(t(UI_LANGUAGE)+" · "+ui.name());uiLanguage.setContentDescription(t(UI_LANGUAGE));uiLanguage.setTooltipText(t(UI_LANGUAGE)+" · "+ui.name());uiLanguage.setEnabled(!busy);
        diagnostics.setText(t(COPY_LOG));diagnostics.setContentDescription(t(COPY_LOG));diagnostics.setEnabled(!busy);
        choose.setText(t(PICK_FOLDER));start.setText(t(START));
        fps.setText(t(FPS));fps.setContentDescription(t(FPS_DETAIL));limit60Fps.setText(t(LIMIT_60_FPS));longPress.setText(t(LONG_PRESS));testMode.setText(t(TEST_MODE));chineseSubtitles.setText(t(CAPTIONS));showMenu.setText(t(SHOW_MENU));
        choose.setEnabled(!busy); start.setEnabled(!busy && tree != null && profileReady);
        mainConfig.setText(t(MAIN_CONFIG));
        for(Button control:new Button[]{mainConfig,specialConfig}){
            boolean selected=control==(selectedTarget==LaunchTarget.MAIN?mainConfig:specialConfig);
            control.setSelected(selected);control.setEnabled(!busy);control.setTextColor(selected?0xff102723:TEXT);
            control.setBackgroundTintList(ColorStateList.valueOf(selected?ACCENT:0xff30474b));
        }
        refreshing=true;fps.setChecked(LaunchSettings.showFps(this));limit60Fps.setChecked(LaunchSettings.limit60Fps(this));longPress.setChecked(LaunchSettings.longPressFastForward(this));testMode.setChecked(LaunchSettings.testMode(this));chineseSubtitles.setChecked(LaunchSettings.chineseSubtitles(this));showMenu.setChecked(LaunchSettings.showMenu(this));refreshing=false;
        for(Switch control:new Switch[]{fps,limit60Fps,longPress,testMode,chineseSubtitles,showMenu})control.setEnabled(!busy && profileReady);
        for(View control:new View[]{testMode,chineseSubtitles,subtitle})control.setVisibility(selectedTarget==LaunchTarget.MAIN?View.VISIBLE:View.GONE);
        language.setText(t(PICK_GAME)+" · "+LaunchSettings.languageLabel(this));language.setEnabled(!busy && profileReady);
        languagePaths.setText(t(UI_PATH,"./"+LaunchSettings.uiArchive(this))+"\n"+t(SCRIPT_PATH,"./"+LaunchSettings.script(this))+"\n"+t(FONT_PATH,"./"+LaunchSettings.font(this)));
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
            .setSingleChoiceItems(selectedTarget==LaunchTarget.SPECIAL?new String[]{"简体中文","日本語"}:new String[]{"简体中文","日本語","English","Українська"},LaunchSettings.language(this),(dialog,which) -> {
                dialog.dismiss();status.setText(t(SAVING));
                LaunchSettings.setLanguage(this,which,this::saved);refresh();
                if(LaunchSettings.saving(this))status.setText(t(SAVING));
            }).setNegativeButton(t(CANCEL),null).create());
    }
    private void pickDirectory() {
        if(selectedTarget==LaunchTarget.SPECIAL){pickSpecialFolder();return;}
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        Uri saved = configurationTree();
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, saved == null ? DocumentsContract.buildDocumentUri(ResourceLocation.AUTHORITY, ResourceLocation.DOCUMENT_ID) : saved);
        try { startActivityForResult(intent, PICK_DIRECTORY); }
        catch (RuntimeException error) {status.setText(t(PICKER_FAILED));}
    }

    private Intent specialRuntime() {
        Intent request = new Intent(GameFolderSelection.START_SPECIAL)
            .addCategory(Intent.CATEGORY_DEFAULT).setPackage(getPackageName());
        List<ResolveInfo> matches = getPackageManager().queryIntentActivities(request, PackageManager.MATCH_DEFAULT_ONLY);
        if (matches.size() != 1 || matches.get(0).activityInfo == null) return null;
        android.content.pm.ActivityInfo runtime = matches.get(0).activityInfo;
        return request.setComponent(new ComponentName(runtime.packageName, runtime.name));
    }

    private String missingSpecialRuntime() {
        switch (ui.tag()) {
            case "zh-CN": return "此版本尚未包含 Special Contents 运行模块。";
            case "zh-TW": return "此版本尚未包含 Special Contents 執行模組。";
            default: return "The Special Contents runtime is not included in this build.";
        }
    }

    private void pickSpecialFolder() {
        try {
            startActivityForResult(gameFolders.picker(GameFolderSelection.SPECIAL), PICK_SPECIAL);
        } catch (ActivityNotFoundException | SecurityException error) {
            Toast.makeText(this, t(PICKER_FAILED), Toast.LENGTH_LONG).show();
        }
    }

    private void startSpecial(Intent runtime, Uri folder) {
        if (runtime == null || gameFolders.granted(folder) == null) return;
        runtime.setClipData(ClipData.newRawUri("Special Contents", folder));
        LaunchSettings.applySession(this,runtime);
        try {
            launching=true;busy=true;refresh();status.setText(t(STARTING));
            startActivity(runtime);
        } catch (ActivityNotFoundException | SecurityException error) {
            launching=false;busy=false;refresh();
            Toast.makeText(this, t(OPERATION_FAILED, missingSpecialRuntime()), Toast.LENGTH_LONG).show();
        }
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
        final LaunchTarget target=selectedTarget;
        new Thread(() -> {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND);
            String problem = null; int count = 0;boolean ready=false;
            try {
                Uri previous=ResourceDirectory.selectedTree(application);
                count=target==LaunchTarget.SPECIAL?ResourceDirectory.inspectSpecialSelection(application,tree):ResourceDirectory.prepareSelection(application,tree,flags);
                LaunchSettings.prepare(application,tree,tree.equals(previous),target);ready=true;
                if(launch && target==LaunchTarget.MAIN){ResourceDirectory.validatePreparedSelection(application);String external=LaunchSettings.subtitle(application);if(LaunchSettings.chineseSubtitles(application) && !external.isEmpty())ExternalSubtitles.validate(application,external);}
            }
            catch (LinkageError error) {problem="Subtitle validation module: "+error.getMessage();}
            catch (Exception error) {problem=error.getMessage();if(problem==null)problem=error.toString();}
            final String failure = problem; final int found = count;final boolean loaded=ready;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed() || generation != token) return;
                busy = false;profileReady=loaded;refresh();
                if(failure!=null){status.setText(t(OPERATION_FAILED,failure)+(loaded?"":"\n"+t(NEED_REAUTHORIZE)));return;}
                status.setText(t(READY,found));
                if(launch && target==LaunchTarget.SPECIAL){Intent runtime=specialRuntime();if(runtime==null){status.setText(missingSpecialRuntime());return;}startSpecial(runtime,tree);}
                else if(launch){Intent session=new Intent(this,Wa2Activity.class);LaunchSettings.applySession(this,session);launching=true;busy=true;refresh();status.setText(t(STARTING));startActivity(session);}
            });
        }, "WA2 launcher resources").start();
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        if(request==PICK_SUBTITLE){if(result==RESULT_OK && data!=null && data.getData()!=null)validateSubtitle(data.getData(),data.getFlags());else status.setText(t(CANCELLED_SUBTITLE));return;}
        if (request == PICK_SPECIAL) {
            if (result == RESULT_OK && data != null) {
                GameFolderSelection.Result saved = gameFolders.remember(GameFolderSelection.SPECIAL, data,
                    tree -> tree.equals(ResourceDirectory.selectedTree(this)));
                if (saved != GameFolderSelection.Result.SAVED) {
                    String message = saved == GameFolderSelection.Result.ACCESS_REQUIRED ? t(NEED_REAUTHORIZE)
                        : t(SAVE_ERROR, t(PICK_FOLDER));
                    Toast.makeText(this, message, Toast.LENGTH_LONG).show();
                }
            }
            if (!isFinishing()){profileReady=false;refresh();Uri tree=configurationTree();if(tree!=null)prepare(tree,0,false);}
            return;
        }
        if (request != PICK_DIRECTORY) { super.onActivityResult(request, result, data); return; }
        if (result == RESULT_OK && data != null && data.getData() != null) prepare(data.getData(), data.getFlags(), false);
        else status.setText(t(CANCELLED_FOLDER));
    }
    @Override protected void onDestroy() { ++generation; super.onDestroy(); }
    @Override protected void onResume() {
        super.onResume();
        if (launching) { launching = false; busy = false;refresh();Uri tree=configurationTree();if(tree!=null)prepare(tree,0,false); }
    }
}
