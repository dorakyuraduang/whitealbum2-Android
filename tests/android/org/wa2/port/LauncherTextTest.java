package org.wa2.port;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

public final class LauncherTextTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args)throws Exception{
        Locale processLocale=Locale.getDefault();int messages=0,formats=0,rtl=0,resolutions=0;
        check(LauncherText.count()==28 && LauncherText.Key.values().length==42);
        Set<String> names=new HashSet<>(),tags=new HashSet<>(),catalogues=new HashSet<>();
        byte[][] profiles={new LaunchProfile(15,0,"content://provider/sub.yaml").encode(),new LaunchProfile(15,1,"").encode()};
        for(int n=0;n<LauncherText.count();++n){
            LauncherText ui=LauncherText.at(n);check(names.add(ui.name()) && tags.add(ui.tag()) && LauncherText.forTag(ui.tag()).index()==n);
            if(ui.rtl())++rtl;StringBuilder catalogue=new StringBuilder();
            for(LauncherText.Key key:LauncherText.Key.values()){
                String template=ui.text(key,key==LauncherText.Key.READY?17:"<sample/sub.yaml>");
                String sig=LauncherText.signature(LauncherText.at(0).text(key,key==LauncherText.Key.READY?17:"sample"));
                check(!template.isEmpty() && sig.isEmpty() && new String(template.getBytes(StandardCharsets.UTF_8),StandardCharsets.UTF_8).equals(template));
                if(Arrays.asList(LauncherText.Key.FOLDER_FMT,LauncherText.Key.GAME_FMT,LauncherText.Key.SUBTITLE_FMT,LauncherText.Key.SAVE_ERROR,LauncherText.Key.SUBTITLE_OK,LauncherText.Key.SUBTITLE_REJECTED,LauncherText.Key.OPERATION_FAILED,LauncherText.Key.LOG_FAILED).contains(key)){
                    check(template.contains("<sample/sub.yaml>"));++formats;
                }else if(key==LauncherText.Key.READY){check(template.contains(String.format(Locale.forLanguageTag(ui.tag()),"%d",17)));++formats;}
                catalogue.append(template).append('\n');++messages;
            }
            check(catalogues.add(catalogue.toString()));
            for(byte[] data:profiles){LaunchProfile profile=LaunchProfile.decode(data);check(Arrays.equals(profile.encode(),data));check(Arrays.asList(profile.gameArguments()).contains(profile.language==0?"zh-CN":"ja-JP"));}
            check(Locale.getDefault().equals(processLocale));
        }
        String[][] aliases={{null,"zh-CN"},{"","zh-CN"},{"unknown","zh-CN"},{"zh-Hant-HK","zh-TW"},{"zh-HK","zh-TW"},{"zh-TW","zh-TW"},{"zh-SG","zh-CN"},{"zh-Hans-CN","zh-CN"},{"en-US","en"},{"pt-PT","pt-BR"},{"iw-IL","he"},{"tl-PH","fil"},{"ja-JP","ja"},{"ar-EG","ar"},{"fr-CA","fr"}};
        for(String[] alias:aliases){check(LauncherText.forTag(alias[0]).tag().equals(alias[1]));++resolutions;}
        for(int invalid:new int[]{-1,LauncherText.count()}){boolean caught=false;try{LauncherText.at(invalid);}catch(IllegalArgumentException expected){caught=true;}check(caught);}
        String[] labels=LauncherText.names();labels[0]="mutated";check(LauncherText.names()[0].equals("简体中文"));check(rtl==4 && messages==1176 && formats==252 && Locale.getDefault().equals(processLocale));
        Files.write(Paths.get(args[0]),("{\"schema\":\"wa2.launcher-text.v1\",\"languages\":28,\"translated_messages\":"+messages+",\"formatted_message_cases\":"+formats+",\"rtl_languages\":"+rtl+",\"locale_resolution_cases\":"+resolutions+",\"default_chinese\":true,\"process_locale_unchanged\":true,\"game_profiles_unchanged\":true,\"source_files_opened\":false,\"android_device_verified\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n").getBytes(StandardCharsets.UTF_8));
    }
}
