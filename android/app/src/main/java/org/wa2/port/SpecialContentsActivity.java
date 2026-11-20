package org.wa2.port;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import org.wa2.launcher.GameFolderSelection;

/** The shared SDL/device adapter, in its own process and resource session. */
public final class SpecialContentsActivity extends Wa2Activity {
    @Override protected void onCreate(Bundle state) {
        Intent request=getIntent();
        Uri tree=null;
        if(request!=null && GameFolderSelection.START_SPECIAL.equals(request.getAction()) &&
                request.getClipData()!=null && request.getClipData().getItemCount()==1)
            tree=request.getClipData().getItemAt(0).getUri();
        GameFolderSelection folders=new GameFolderSelection(this);
        Uri granted=folders.granted(tree);
        // This non-exported activity receives the main process's explicit
        // session URI. SharedPreferences caches are not coherent across app
        // processes; do not substitute a stale saved-folder value here.
        if(granted==null) {
            // SDL must not start with an absent, stale or cross-title folder.
            ResourceDirectory.specialSession(null);
        } else ResourceDirectory.specialSession(granted);
        super.onCreate(state);
    }
    @Override protected String[] getLibraries() {return new String[]{"SDL2","wa2_special"};}
}
