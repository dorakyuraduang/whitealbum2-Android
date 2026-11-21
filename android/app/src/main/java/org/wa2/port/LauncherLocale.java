package org.wa2.port;

import android.content.Context;
import android.content.Intent;
import android.os.LocaleList;

/** Available before folder authorization; independent of game/save settings. */
final class LauncherLocale {
    private static final String PREFERENCES="launcher-ui", LANGUAGE="language";
    private static final String SESSION_LANGUAGE="wa2.launch-ui-language";
    static LauncherText load(Context context){
        if(context instanceof Wa2Activity){
            Intent session=((Wa2Activity)context).getIntent();
            String tag=session==null?null:session.getStringExtra(SESSION_LANGUAGE);
            if(tag!=null)return LauncherText.forTag(tag);
        }
        String saved=context.getSharedPreferences(PREFERENCES,Context.MODE_PRIVATE).getString(LANGUAGE,null);
        LocaleList locales=context.getResources().getConfiguration().getLocales();
        return LauncherText.resolve(saved,locales.isEmpty()?null:locales.get(0).toLanguageTag());
    }
    static void save(Context context,LauncherText language){context.getSharedPreferences(PREFERENCES,Context.MODE_PRIVATE).edit().putString(LANGUAGE,language.tag()).apply();}
    // SC runs in a separate process whose SharedPreferences cache can be stale.
    static void attachToSession(Context launcher,Intent intent){intent.putExtra(SESSION_LANGUAGE,load(launcher).tag());}
    private LauncherLocale(){}
}
