package org.wa2.port;

/** Default location plus user-selected Wa2Res folders on local storage. */
final class ResourceLocation {
    static final String DIRECTORY = "Wa2Res";
    static final String AUTHORITY = "com.android.externalstorage.documents";
    static final String DOCUMENT_ID = "primary:" + DIRECTORY;

    private ResourceLocation() { }

    static boolean matches(String authority, String documentId) {
        if (!AUTHORITY.equals(authority) || documentId == null) return false;
        int colon = documentId.indexOf(':');
        if (colon < 1) return false;
        String volume = documentId.substring(0, colon), path = documentId.substring(colon + 1);
        if (!"primary".equals(volume) && !volume.matches("[0-9a-fA-F]{4}-[0-9a-fA-F]{4}")) return false;
        String[] components = path.split("/", -1);
        for (String component : components)
            if (component.isEmpty() || component.equals(".") || component.equals("..") || component.indexOf('\\') >= 0) return false;
        return components.length > 0 && DIRECTORY.equalsIgnoreCase(components[components.length - 1]);
    }
}
