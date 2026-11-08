package org.wa2.port;

import java.nio.file.Files;
import java.nio.file.Path;

public final class ResourceLocationTest {
    public static void main(String[] args) throws Exception {
        int cases = 0;
        for (String id : new String[] {"primary:Wa2Res", "primary:WA2", "primary:Games/White Album 2", "primary:游戏/白色相簿2", "1234-5678:Visual Novels", "ABCD-1234:Games/IC", "home:WhiteAlbum2", "primary:"}) {
            if (!ResourceLocation.matches(ResourceLocation.AUTHORITY, id)) throw new AssertionError("Selected folder rejected: " + id);
            ++cases;
        }
        for (String id : new String[] { null, "", "primary:Game\0Folder" }) {
            if (ResourceLocation.matches(ResourceLocation.AUTHORITY, id))
                throw new AssertionError("Malformed resource document ID accepted");
            ++cases;
        }
        for (String authority : new String[] {"com.android.providers.downloads.documents", "example.provider"}) {
            if (!ResourceLocation.matches(authority,"opaque-folder-id-42"))throw new AssertionError("Authorized document provider rejected");++cases;
        }
        for (String authority : new String[] { null, "", "invalid/provider", "bad\\provider", "provider\0name" }) {
            if (ResourceLocation.matches(authority, ResourceLocation.DOCUMENT_ID))
                throw new AssertionError("Malformed resource provider accepted");
            ++cases;
        }
        Files.writeString(Path.of(args[0]), "{\"schema\":\"wa2.android-resource-location.v1\",\"cases\":" + cases
            + ",\"folder_name_unrestricted\":true,\"opaque_document_ids_allowed\":true,\"arbitrary_directory_allowed\":true,"
            + "\"android_device_verified\":false,\"passed\":true}\n");
        System.out.println("PASS selectable local resource location: " + cases);
    }
}
