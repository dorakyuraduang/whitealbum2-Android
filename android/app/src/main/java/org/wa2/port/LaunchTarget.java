package org.wa2.port;

/** Title-specific adapter capabilities; game behavior stays in the shared host. */
enum LaunchTarget {
    MAIN("main", "native-launcher.wa2cfg"),
    SPECIAL("special", "native-launcher-special.wa2cfg");
    final String id, file;
    LaunchTarget(String id,String file){this.id=id;this.file=file;}
    String storageKey(String tree){return tree==null?null:id+":"+tree;}
    LaunchProfile defaults(){return LaunchProfile.defaults();}
    void validate(LaunchProfile profile){
        if(this==SPECIAL && (profile.language>LaunchProfile.ENGLISH || profile.enabled(LaunchProfile.TEST_MODE) || !profile.subtitle.isEmpty()))
            throw new IllegalArgumentException("SC supports Chinese/Japanese/English resources and automatically selects subtitles by language");
    }
    String script(LaunchProfile profile){return this==SPECIAL && profile.language==LaunchProfile.CHINESE?"script_cn.pak / script_cn2.pak":profile.scriptLocation();}
    String ui(LaunchProfile profile){return this==SPECIAL && profile.language==LaunchProfile.CHINESE?"gcn.pak":"grp.pak";}
    String subtitleFile(LaunchProfile profile){
        if(this!=SPECIAL)throw new IllegalArgumentException("Automatic subtitle files are SC only");
        switch(profile.language){case LaunchProfile.CHINESE:return "subtext.dat";case LaunchProfile.JAPANESE:return "subtext_ja.dat";case LaunchProfile.ENGLISH:return "subtext_en.dat";default:throw new IllegalArgumentException("Unsupported SC subtitle language");}
    }
    boolean normalize(LaunchProfileStore store){return this==SPECIAL && store.setSubtitle("");}
    String[] arguments(LaunchProfile profile){
        validate(profile);
        return profile.gameArguments();
    }
}
