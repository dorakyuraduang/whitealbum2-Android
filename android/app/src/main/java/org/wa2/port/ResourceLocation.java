package org.wa2.port;

/** SAF document IDs are provider-owned; the selected folder has no fixed name. */
final class ResourceLocation {
    static final String AUTHORITY = "com.android.externalstorage.documents";
    static final String DOCUMENT_ID = "primary:";

    private ResourceLocation() { }

    static boolean matches(String authority, String documentId) {
        return authority != null && !authority.isEmpty() && authority.indexOf('\0') < 0 &&
            authority.indexOf('/') < 0 && authority.indexOf('\\') < 0 &&
            documentId != null && !documentId.isEmpty() && documentId.indexOf('\0') < 0;
    }
}
