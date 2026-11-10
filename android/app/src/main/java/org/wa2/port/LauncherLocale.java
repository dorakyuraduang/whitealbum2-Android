package org.wa2.port;

import android.content.Context;
import android.os.LocaleList;

/** Available before folder authorization; independent of game/save settings. */
final class LauncherLocale {
    private static final String PREFERENCES="launcher-ui", LANGUAGE="language";
    static LauncherText load(Context context){
        String saved=context.getSharedPreferences(PREFERENCES,Context.MODE_PRIVATE).getString(LANGUAGE,null);
        LocaleList locales=context.getResources().getConfiguration().getLocales();
        return LauncherText.resolve(saved,locales.isEmpty()?null:locales.get(0).toLanguageTag());
    }
    static void save(Context context,LauncherText language){context.getSharedPreferences(PREFERENCES,Context.MODE_PRIVATE).edit().putString(LANGUAGE,language.tag()).apply();}
    private LauncherLocale(){}
}
