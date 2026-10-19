package org.wa2.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.SparseArray;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.RadioButton;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.util.ArrayList;

/** Device widgets for the shared source window model; no game script logic. */
public final class NativeWidgets {
    private static Activity activity;
    private static final SparseArray<Window> windows = new SparseArray<>();
    private static final SparseArray<View> controls = new SparseArray<>();
    private static Button menuButton;
    private static JSONArray currentMenu = new JSONArray();
    private static int menuOwner;
    private interface UiAction { void run() throws Exception; }
    private static native void nativeEvent(int kind, int window, int message, int wp,
                                          int lp, int control, int value, String text);
    private static void failure(Throwable error) {
        nativeEvent(-1, 0, 0, 0, 0, 0, 0, "Native widgets: " + error.toString());
    }
    private static void ui(UiAction action) {
        final Activity owner = activity;
        if (owner == null) { failure(new IllegalStateException("Activity unavailable")); return; }
        owner.runOnUiThread(() -> { try { action.run(); } catch (Throwable error) { failure(error); } });
    }
    public static void attach(Activity owner) { activity = owner; }
    public static void detach() {
        for (int i = 0; i < windows.size(); ++i) windows.valueAt(i).dialog.dismiss();
        windows.clear(); controls.clear(); menuButton = null; activity = null;
    }
    private static int integer(JSONObject value, String key) throws JSONException {
        return (int)value.getLong(key);
    }
    private static String label(String text) { return text.replace("&&", "\u0000").replace("&", "").replace("\u0000", "&"); }
    private static final class Window {
        final int token; final Dialog dialog; final SparseArray<JSONObject> definitions = new SparseArray<>();
        final SparseArray<View> views = new SparseArray<>(); boolean updating;
        Window(int token, Dialog dialog) { this.token = token; this.dialog = dialog; }
    }
    private static void command(Window window, JSONObject definition, int notification) {
        try { final int id=integer(definition,"id"), token=integer(definition,"token");
            nativeEvent(0,window.token,0x111,(id&65535)|(notification<<16),token,0,0,null);
        } catch (Throwable error) { failure(error); }
    }
    private static View createControl(Window owner, JSONObject definition) throws Exception {
        final String type=definition.getString("type"); final int id=integer(definition,"id"),token=integer(definition,"token"),style=integer(definition,"style");
        View result;
        if ("#128".equals(type)) {
            final int kind=style&15;
            if(kind==2 || kind==3 || kind==5 || kind==6 || kind==4 || kind==9) {
                CompoundButton button=(kind==4 || kind==9)?new RadioButton(activity):new CheckBox(activity);
                button.setOnClickListener(view -> { if(owner.updating)return;
                    nativeEvent(4,owner.token,0,0,0,id,button.isChecked()?1:0,null);command(owner,definition,0); });
                result=button;
            } else if(kind==7) {
                TextView group=new TextView(activity);group.setGravity(Gravity.TOP|Gravity.START);group.setPadding(8,0,8,0);
                GradientDrawable border=new GradientDrawable();border.setColor(Color.TRANSPARENT);border.setStroke(1,0xff888888);group.setBackground(border);result=group;
            } else if(kind==0 || kind==1 || kind==11) {
                Button button=new Button(activity);button.setOnClickListener(view -> { if(!owner.updating)command(owner,definition,0); });result=button;
            } else throw new IllegalArgumentException("Unsupported source button style " + kind);
        } else if("#129".equals(type)) {
            EditText edit=new EditText(activity);edit.setSingleLine((style&4)==0);
            edit.setInputType((style&0x20)!=0?InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_PASSWORD:InputType.TYPE_CLASS_TEXT);
            edit.addTextChangedListener(new TextWatcher() {
                public void beforeTextChanged(CharSequence s,int start,int count,int after) { }
                public void onTextChanged(CharSequence s,int start,int before,int count) {
                    if(owner.updating)return;nativeEvent(1,owner.token,0,0,0,id,0,s.toString());command(owner,definition,0x300);
                }
                public void afterTextChanged(Editable value) { }
            });result=edit;
        } else if("#130".equals(type)) {
            if((style&31)>2 && (style&31)!=11 && (style&31)!=12 && (style&31)!=13 && (style&31)!=16 && (style&31)!=17 && (style&31)!=18)
                throw new IllegalArgumentException("Unsupported source static style " + (style&31));
            TextView text=new TextView(activity);text.setGravity((style&3)==1?Gravity.CENTER:(style&3)==2?Gravity.END:Gravity.START);result=text;
        } else if("#131".equals(type)) {
            ListView list=new ListView(activity);list.setChoiceMode(ListView.CHOICE_MODE_SINGLE);
            list.setOnItemClickListener((parent,view,position,row) -> { if(owner.updating)return;
                nativeEvent(2,owner.token,0,0,0,id,position,null);command(owner,definition,1); });result=list;
        } else if("msctls_trackbar32".equalsIgnoreCase(type)) {
            SeekBar slider=new SeekBar(activity);
            slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                private void changed(SeekBar bar,int code) {
                    if(owner.updating)return;try { int value=bar.getProgress()+owner.definitions.get(token).getInt("minimum");
                        nativeEvent(3,owner.token,0,0,0,id,value,null);
                        nativeEvent(0,owner.token,(style&2)!=0?0x115:0x114,code|((value&65535)<<16),token,0,0,null);
                    } catch(Throwable error) { failure(error); }
                }
                public void onProgressChanged(SeekBar bar,int progress,boolean fromUser) { if(fromUser)changed(bar,5); }
                public void onStartTrackingTouch(SeekBar bar) { }
                public void onStopTrackingTouch(SeekBar bar) { changed(bar,8); }
            });result=slider;
        } else throw new IllegalArgumentException("Unsupported source control class " + type);
        result.setId(View.generateViewId());owner.views.put(token,result);owner.definitions.put(token,definition);controls.put(token,result);
        if(result instanceof TextView) { ((TextView)result).setTextSize(12f);((TextView)result).setPadding(3,0,3,0); }
        return result;
    }
    public static void create(String json) { ui(() -> {
        JSONObject definition=new JSONObject(json);int id=integer(definition,"token");if(windows.get(id)!=null)throw new IllegalArgumentException("Duplicate dialog token");
        Dialog dialog=new Dialog(activity);dialog.setTitle(definition.getString("title"));dialog.setCanceledOnTouchOutside(false);
        Window owner=new Window(id,dialog);windows.put(id,owner);FrameLayout content=new FrameLayout(activity);
        float density=activity.getResources().getDisplayMetrics().density;
        float scale=Math.min(2f*density,activity.getResources().getDisplayMetrics().widthPixels*.90f/Math.max(1,definition.getInt("width")));
        content.setLayoutParams(new ViewGroup.LayoutParams(Math.max(1,(int)(definition.getInt("width")*scale)),Math.max(1,(int)(definition.getInt("height")*scale))));
        JSONArray children=definition.getJSONArray("controls");
        // Group frames are drawn first, preserving source control z-order for
        // labels, buttons and sliders contained by those frames.
        for(int pass=0;pass<2;++pass)for(int i=0;i<children.length();++i) {
            JSONObject child=children.getJSONObject(i);boolean group="#128".equals(child.getString("type"))&&(integer(child,"style")&15)==7;if(group!=(pass==0))continue;
            View view=createControl(owner,child);JSONArray b=child.getJSONArray("bounds");FrameLayout.LayoutParams bounds=new FrameLayout.LayoutParams(Math.max(1,(int)(b.getInt(2)*scale)),Math.max(1,(int)(b.getInt(3)*scale)));
            bounds.leftMargin=(int)(b.getInt(0)*scale);bounds.topMargin=(int)(b.getInt(1)*scale);content.addView(view,bounds);
        }
        ScrollView scroll=new ScrollView(activity);scroll.addView(content);dialog.setContentView(scroll);
        dialog.setOnCancelListener(ignored -> nativeEvent(0,id,0x10,0,0,0,0,null));
        updateNow(owner,definition);if(definition.getBoolean("visible"))dialog.show();
    }); }
    private static void updateNow(Window owner,JSONObject source) throws Exception {
        owner.updating=true;
        try { JSONArray children=source.getJSONArray("controls");
            for(int i=0;i<children.length();++i) { JSONObject c=children.getJSONObject(i);int token=integer(c,"token");View view=owner.views.get(token);if(view==null)throw new IllegalArgumentException("Unknown control token");owner.definitions.put(token,c);
                view.setEnabled(c.getBoolean("enabled"));view.setVisibility(c.getBoolean("visible")?View.VISIBLE:View.INVISIBLE);
                if(view instanceof TextView) { String text=view instanceof EditText?c.getString("text"):label(c.getString("text"));TextView textView=(TextView)view;if(!textView.getText().toString().equals(text))textView.setText(text); }
                if(view instanceof CompoundButton)((CompoundButton)view).setChecked(c.getInt("check")!=0);
                if(view instanceof SeekBar) { SeekBar slider=(SeekBar)view;int minimum=c.getInt("minimum"),maximum=c.getInt("maximum");if((long)maximum-minimum>Integer.MAX_VALUE || maximum<minimum)throw new IllegalArgumentException("Slider range");slider.setMax(maximum-minimum);slider.setProgress(c.getInt("position")-minimum); }
                if(view instanceof ListView) { JSONArray values=c.getJSONArray("items");ArrayList<String> labels=new ArrayList<>();for(int j=0;j<values.length();++j)labels.add(values.getString(j));ListView list=(ListView)view;list.setAdapter(new ArrayAdapter<>(activity,android.R.layout.simple_list_item_single_choice,labels));int selected=c.getInt("selection");if(selected>=0)list.setItemChecked(selected,true); }
            }
        } finally { owner.updating=false; }
    }
    public static void update(String json) { ui(() -> { JSONObject source=new JSONObject(json);Window owner=windows.get(integer(source,"token"));if(owner==null)throw new IllegalArgumentException("Unknown dialog");updateNow(owner,source); }); }
    public static void destroy(int token) { ui(() -> { Window owner=windows.get(token);if(owner==null)return;for(int i=0;i<owner.views.size();++i)controls.remove(owner.views.keyAt(i));owner.dialog.dismiss();windows.remove(token); }); }
    public static void show(int token,boolean visible) { ui(() -> { Window owner=windows.get(token);if(owner==null)throw new IllegalArgumentException("Unknown dialog");if(visible)owner.dialog.show();else owner.dialog.hide(); }); }
    public static void title(int token,String text) { ui(() -> { Window owner=windows.get(token);if(owner==null)throw new IllegalArgumentException("Unknown dialog");owner.dialog.setTitle(text); }); }
    public static void focus(int token) { ui(() -> { View view=controls.get(token);if(view!=null)view.requestFocus();else if(windows.get(token)!=null)windows.get(token).dialog.getWindow().getDecorView().requestFocus();else throw new IllegalArgumentException("Unknown focus target"); }); }
    private static void openMenu(JSONArray items) throws Exception {
        ArrayList<JSONObject> entries=new ArrayList<>();ArrayList<String> labels=new ArrayList<>();
        for(int i=0;i<items.length();++i) { JSONObject item=items.getJSONObject(i);if((item.getInt("type")&0x800)!=0)continue;entries.add(item);labels.add(((item.getInt("state")&8)!=0?"✓ ":"")+label(item.getString("text"))); }
        ListView list=new ListView(activity);list.setAdapter(new ArrayAdapter<String>(activity,android.R.layout.simple_list_item_1,labels){
            @Override public boolean isEnabled(int position) { return (entries.get(position).optInt("state")&3)==0; }
        });
        AlertDialog menu=new AlertDialog.Builder(activity).setView(list).create();
        list.setOnItemClickListener((parent,view,position,row) -> { try { JSONObject item=entries.get(position);if((item.getInt("state")&3)!=0)return;menu.dismiss();JSONArray children=item.getJSONArray("children");if(children.length()>0)openMenu(children);else nativeEvent(0,menuOwner,0x111,integer(item,"id"),0,0,0,null); } catch(Throwable error) { failure(error); } });menu.show();
    }
    public static void menu(int owner,String json) { ui(() -> {
        menuOwner=owner;currentMenu=new JSONArray(json);
        if(menuButton==null) { menuButton=new Button(activity);menuButton.setText("☰");menuButton.setContentDescription("游戏功能菜单");menuButton.setAlpha(.65f);
            menuButton.setPadding(0,0,0,0);menuButton.setMinWidth(0);menuButton.setMinHeight(0);menuButton.setGravity(Gravity.CENTER);
            int size=Math.round(48f*activity.getResources().getDisplayMetrics().density);
            FrameLayout.LayoutParams bounds=new FrameLayout.LayoutParams(size,size,Gravity.TOP|Gravity.END);activity.addContentView(menuButton,bounds);
            menuButton.setOnClickListener(view -> { try { openMenu(currentMenu); } catch(Throwable error) { failure(error); } }); }
        menuButton.setVisibility(currentMenu.length()==0?View.GONE:View.VISIBLE);
    }); }
    private NativeWidgets() { }
}
