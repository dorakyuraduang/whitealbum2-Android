package org.wa2.port;

/** Title-specific adapter capabilities; game behavior stays in the shared host. */
enum LaunchTarget {
    MAIN("main", "native-launcher.wa2cfg"),
    SPECIAL("special", "native-launcher-special.wa2cfg");
    final String id, file;
    LaunchTarget(String id,String file){this.id=id;this.file=file;}
    String storageKey(String tree){return tree==null?null:id+":"+tree;}
    LaunchProfile defaults(){return this==MAIN?LaunchProfile.defaults():new LaunchProfile(LaunchProfile.FPS|LaunchProfile.LONG_PRESS);}
    void validate(LaunchProfile profile){
        if(this==SPECIAL && (profile.language>LaunchProfile.JAPANESE || profile.enabled(LaunchProfile.TEST_MODE) || profile.enabled(LaunchProfile.CHINESE_SUBTITLES) || !profile.subtitle.isEmpty()))
            throw new IllegalArgumentException("SC supports Chinese/Japanese resources, FPS, frame limit, hold to skip and menu settings");
    }
    String script(LaunchProfile profile){return this==SPECIAL && profile.language==LaunchProfile.CHINESE?"script_cn.pak / script_cn2.pak":profile.scriptLocation();}
    String ui(LaunchProfile profile){return profile.language==LaunchProfile.JAPANESE?"grp.pak":this==SPECIAL?"gcn.pak":"grp.pak";}
    String[] arguments(LaunchProfile profile){
        validate(profile);
        if(this==MAIN)return profile.gameArguments();
        return profile.enabled(LaunchProfile.LIMIT_60_FPS)?new String[]{"--limit-60-fps","--language",profile.languageTag()}:new String[]{"--language",profile.languageTag()};
    }
}
