package org.wa2.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Bundle;
import android.widget.Toast;

/** A native failure remains readable after SDL's game thread has shut down. */
public final class FailureActivity extends Activity {
    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        String message = getIntent().getStringExtra("message");
        String version = "unknown";
        try { version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
        catch (android.content.pm.PackageManager.NameNotFoundException ignored) { }
        final String diagnostic = "WA2 " + version + "\n\n" + (message == null ? "未知错误" : message);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("游戏运行失败")
            .setMessage(diagnostic).setPositiveButton("退出", (value, which) -> finish())
            .setNeutralButton("复制错误", null).setOnCancelListener(value -> finish()).create();
        dialog.setOnShowListener(value -> dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view -> {
            ClipboardManager clipboard = (ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
            if (clipboard != null) {
                clipboard.setPrimaryClip(ClipData.newPlainText("WA2 error", diagnostic));
                Toast.makeText(this, "错误信息已复制", Toast.LENGTH_SHORT).show();
            }
        }));
        dialog.show();
    }
}
