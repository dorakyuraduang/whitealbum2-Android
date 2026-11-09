package org.wa2.port;

import android.content.Context;

/** Available before folder authorization; independent of game/save settings. */
final class LauncherLocale {
    private static final String PREFERENCES="launcher-ui", LANGUAGE="language";
    static LauncherText load(Context context){return LauncherText.forTag(context.getSharedPreferences(PREFERENCES,Context.MODE_PRIVATE).getString(LANGUAGE,"zh-CN"));}
    static void save(Context context,LauncherText language){context.getSharedPreferences(PREFERENCES,Context.MODE_PRIVATE).edit().putString(LANGUAGE,language.tag()).apply();}
    private LauncherLocale(){}
}
