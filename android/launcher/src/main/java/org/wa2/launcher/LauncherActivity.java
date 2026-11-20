package org.wa2.launcher;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.UriPermission;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.EnumMap;

public final class LauncherActivity extends Activity {
    private static final String MAIN_APP = "org.wa2.port";
    private static final int ACCESS = Intent.FLAG_GRANT_READ_URI_PERMISSION
        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
    private static final int TREE_ACCESS = ACCESS | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION;

    private enum Edition {
        MAIN("main", "org.wa2.launcher.action.RUN_MAIN", 11, R.string.main_game,
            R.string.main_runtime_missing),
        SPECIAL("special", "org.wa2.launcher.action.RUN_SPECIAL_CONTENTS", 12,
            R.string.special_contents, R.string.special_runtime_missing);

        final String key, action;
        final int requestCode, title, missingRuntime;

        Edition(String key, String action, int requestCode, int title, int missingRuntime) {
            this.key = key;
            this.action = action;
            this.requestCode = requestCode;
            this.title = title;
            this.missingRuntime = missingRuntime;
        }
    }

    private static final class Card {
        TextView status, folder;
        Button pick, open;
    }

    private final EnumMap<Edition, Card> cards = new EnumMap<>(Edition.class);
    private SharedPreferences folders;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        folders = getSharedPreferences("game-folders", MODE_PRIVATE);
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(getColor(R.color.page));
        scroll.setFillViewport(true);
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            scroll.setOnApplyWindowInsetsListener((view, windowInsets) -> {
                Insets safe = windowInsets.getInsets(WindowInsets.Type.systemBars()
                    | WindowInsets.Type.displayCutout());
                view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
                return windowInsets;
            });
        }
        FrameLayout page = new FrameLayout(this);
        scroll.addView(page);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(20), dp(18), dp(20), dp(16));
        int width = getResources().getConfiguration().screenWidthDp >= 600
            ? dp(560) : FrameLayout.LayoutParams.MATCH_PARENT;
        page.addView(content, new FrameLayout.LayoutParams(width,
            FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        TextView heading = text(26, R.color.primary_text);
        heading.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        if (Build.VERSION.SDK_INT >= 28) heading.setAccessibilityHeading(true);
        heading.setText(R.string.app_name);
        content.addView(heading);
        for (Edition edition : Edition.values()) {
            addCard(content, edition);
        }
        setContentView(scroll);
    }

    @Override protected void onResume() {
        super.onResume();
        refresh();
    }

    private void addCard(LinearLayout content, Edition edition) {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(16), dp(12), dp(16), dp(10));
        GradientDrawable background = new GradientDrawable();
        background.setColor(getColor(R.color.surface));
        background.setCornerRadius(dp(14));
        background.setStroke(dp(1), getColor(R.color.outline));
        panel.setBackground(background);
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        layout.topMargin = dp(12);
        content.addView(panel, layout);

        TextView title = text(20, R.color.primary_text);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        if (Build.VERSION.SDK_INT >= 28) title.setAccessibilityHeading(true);
        title.setText(edition.title);
        panel.addView(title);

        Card card = new Card();
        card.status = text(14, R.color.secondary_text);
        card.status.setPadding(0, dp(4), 0, 0);
        card.folder = text(14, R.color.secondary_text);
        panel.addView(card.status);
        panel.addView(card.folder);
        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.END);
        panel.addView(actions);
        card.pick = button(R.string.pick_directory);
        card.pick.setContentDescription(getString(R.string.pick_directory_for, getString(edition.title)));
        card.pick.setOnClickListener(view -> pick(edition));
        card.open = button(R.string.start);
        card.open.setContentDescription(getString(R.string.start_edition, getString(edition.title)));
        card.open.setOnClickListener(view -> open(edition));
        actions.addView(card.pick);
        actions.addView(card.open);
        cards.put(edition, card);
    }

    private TextView text(int size, int color) {
        TextView text = new TextView(this);
        text.setTextSize(size);
        text.setTextColor(getColor(color));
        return text;
    }

    private Button button(int label) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        return button;
    }

    private int dp(int amount) {
        return Math.round(amount * getResources().getDisplayMetrics().density);
    }

    private void refresh() {
        for (Edition edition : Edition.values()) {
            Card card = cards.get(edition);
            boolean canRunWithFolder = hasRuntime(runtimeRequest(edition));
            boolean legacy = !canRunWithFolder && edition == Edition.MAIN && legacyMain() != null;
            Uri saved = savedFolder(edition);
            Uri granted = grantedFolder(saved);
            card.status.setVisibility(canRunWithFolder || legacy ? View.GONE : View.VISIBLE);
            card.status.setText(edition.missingRuntime);
            card.pick.setVisibility(legacy ? View.GONE : View.VISIBLE);
            if (legacy) {
                card.folder.setText(R.string.main_uses_own_folder);
            } else if (granted != null) {
                card.folder.setText(getString(R.string.folder_selected, folderName(granted)));
            } else {
                card.folder.setText(saved == null ? R.string.folder_required : R.string.folder_access_expired);
            }
            card.open.setEnabled(legacy || (canRunWithFolder && granted != null));
        }
    }

    private Intent legacyMain() {
        return getPackageManager().getLaunchIntentForPackage(MAIN_APP);
    }

    private Intent runtimeRequest(Edition edition) {
        return new Intent(edition.action).addCategory(Intent.CATEGORY_DEFAULT);
    }

    private boolean hasRuntime(Intent request) {
        for (ResolveInfo match : getPackageManager().queryIntentActivities(request,
            PackageManager.MATCH_DEFAULT_ONLY)) {
            ActivityInfo activity = match.activityInfo;
            if (activity != null && activity.exported && (activity.permission == null
                || checkSelfPermission(activity.permission) == PackageManager.PERMISSION_GRANTED)) {
                return true;
            }
        }
        return false;
    }

    private Uri savedFolder(Edition edition) {
        String value = folders.getString(edition.key, null);
        return value == null ? null : Uri.parse(value);
    }

    private static boolean isTree(Uri uri) {
        return uri != null && ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())
            && uri.getAuthority() != null && uri.getPathSegments().size() == 2
            && DocumentsContract.isTreeUri(uri) && !DocumentsContract.getTreeDocumentId(uri).isEmpty();
    }

    private Uri grantedFolder(Uri uri) {
        if (!isTree(uri)) return null;
        for (UriPermission permission : getContentResolver().getPersistedUriPermissions()) {
            if (uri.equals(permission.getUri()) && permission.isReadPermission()
                && permission.isWritePermission()) return uri;
        }
        return null;
    }

    private static String folderName(Uri uri) {
        String id = DocumentsContract.getTreeDocumentId(uri);
        int slash = id.lastIndexOf('/');
        int colon = id.indexOf(':');
        int divider = Math.max(slash, colon);
        return divider >= 0 && divider + 1 < id.length() ? id.substring(divider + 1) : id;
    }

    private void pick(Edition edition) {
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
            .addFlags(TREE_ACCESS | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(picker, edition.requestCode);
        } catch (ActivityNotFoundException | SecurityException error) {
            message(R.string.folder_picker_unavailable);
        }
    }

    @Override protected void onActivityResult(int requestCode, int result, Intent data) {
        super.onActivityResult(requestCode, result, data);
        if (result != RESULT_OK || data == null) return;
        for (Edition edition : Edition.values()) {
            if (edition.requestCode == requestCode) {
                remember(edition, data);
                refresh();
                return;
            }
        }
    }

    private void remember(Edition edition, Intent data) {
        Uri tree = data.getData();
        int needed = ACCESS | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION;
        if (!isTree(tree) || (data.getFlags() & needed) != needed) {
            message(R.string.folder_needs_access);
            return;
        }
        Uri previous = savedFolder(edition);
        try {
            getContentResolver().takePersistableUriPermission(tree, ACCESS);
        } catch (SecurityException error) {
            message(R.string.folder_needs_access);
            return;
        }
        if (folders.edit().putString(edition.key, tree.toString()).commit()) {
            forgetGrantIfUnused(previous);
        } else {
            // SharedPreferences updates its in-memory view before committing to disk.
            SharedPreferences.Editor rollback = folders.edit();
            if (previous == null) rollback.remove(edition.key);
            else rollback.putString(edition.key, previous.toString());
            rollback.apply();
            forgetGrantIfUnused(tree);
            message(R.string.cannot_save_folder);
        }
    }

    private void forgetGrantIfUnused(Uri tree) {
        if (tree == null) return;
        for (Edition edition : Edition.values()) {
            if (tree.equals(savedFolder(edition))) return;
        }
        try {
            getContentResolver().releasePersistableUriPermission(tree, ACCESS);
        } catch (SecurityException ignored) {
            // The provider may already have revoked this permission.
        }
    }

    private void open(Edition edition) {
        Intent target = runtimeRequest(edition);
        try {
            if (hasRuntime(target)) {
                Uri tree = grantedFolder(savedFolder(edition));
                if (tree == null) {
                    refresh();
                    return;
                }
                target.setClipData(ClipData.newRawUri(getString(edition.title), tree));
                target.addFlags(TREE_ACCESS);
                startActivity(Intent.createChooser(target,
                    getString(R.string.select_runtime, getString(edition.title))));
                return;
            }
            Intent installed = edition == Edition.MAIN ? legacyMain() : null;
            if (installed == null) {
                refresh();
                return;
            }
            startActivity(installed);
        } catch (ActivityNotFoundException | SecurityException error) {
            refresh();
            message(R.string.cannot_open_runtime);
        }
    }

    private void message(int label) {
        Toast.makeText(this, label, Toast.LENGTH_LONG).show();
    }
}
