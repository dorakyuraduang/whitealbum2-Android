package org.wa2.port;

import java.nio.file.Files;
import java.nio.file.Path;

public final class ResourceLocationTest {
    public static void main(String[] args) throws Exception {
        int cases = 0;
        for (String id : new String[] {"primary:Wa2Res", "primary:wa2res", "primary:Games/Wa2Res", "1234-5678:Wa2Res", "ABCD-1234:Games/Wa2Res"}) {
            if (!ResourceLocation.matches(ResourceLocation.AUTHORITY, id)) throw new AssertionError("Selected local Wa2Res folder rejected: " + id);
            ++cases;
        }
        for (String id : new String[] { null, "", "primary:",
                "primary:/Wa2Res", "primary:Wa2Res/", "primary:Wa2Res/IC",
                "primary:Wa2Res/../Other", "primary:Wa2ResExtra", "primary:Wa2Res%2FIC",
                "primary:Games//Wa2Res", "primary:Games/../Wa2Res", "home:Wa2Res", "/storage/emulated/0/Wa2Res" }) {
            if (ResourceLocation.matches(ResourceLocation.AUTHORITY, id))
                throw new AssertionError("Different resource directory accepted: " + id);
            ++cases;
        }
        for (String authority : new String[] { null, "", "com.android.providers.downloads.documents",
                "example.provider", "com.android.externalstorage.documents.evil" }) {
            if (ResourceLocation.matches(authority, ResourceLocation.DOCUMENT_ID))
                throw new AssertionError("Different resource provider accepted: " + authority);
            ++cases;
        }
        Files.writeString(Path.of(args[0]), "{\"schema\":\"wa2.android-resource-location.v1\",\"cases\":" + cases
            + ",\"default_primary_storage_relative_path\":\"Wa2Res\",\"selected_local_wa2res_directory_allowed\":true,\"arbitrary_directory_allowed\":false,"
            + "\"android_device_verified\":false,\"passed\":true}\n");
        System.out.println("PASS selectable local resource location: " + cases);
    }
}
