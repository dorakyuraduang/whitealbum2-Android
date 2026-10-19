"""Losslessly export the supplied ICO image for the Android adapter (no resizing)."""
import struct
import hashlib
from pathlib import Path
import re
import zipfile
import zlib
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ANDROID_ICON = ROOT / 'android/app/src/main/res/drawable-nodpi/ic_launcher_art.png'


def frames(data):
    if len(data) < 6 or struct.unpack_from('<HH', data) != (0, 1):
        raise ValueError('icon.ico is not an ICO file')
    count = struct.unpack_from('<H', data, 4)[0]
    if not 1 <= count <= 256 or len(data) < 6 + count * 16:
        raise ValueError('Invalid ICO image directory')
    result = []
    for n in range(count):
        width, height, _, _, _, bits, size, offset = struct.unpack_from('<BBBBHHII', data, 6 + n * 16)
        if offset < 6 + count * 16 or size > len(data) - offset:
            raise ValueError('ICO image exceeds file bounds')
        result.append((width or 256, height or 256, bits, data[offset:offset + size]))
    return result


def png_image(width, height, payload):
    if payload.startswith(b'\x89PNG\r\n\x1a\n'):
        if len(payload) < 33 or struct.unpack_from('>II', payload, 16) != (width, height):
            raise ValueError('ICO PNG dimensions disagree')
        return payload
    if len(payload) < 40:
        raise ValueError('Unsupported ICO bitmap header')
    header, w, doubled_height, planes, bits, compression = struct.unpack_from('<IiiHHI', payload)
    if header < 40 or header > len(payload) or w != width or abs(doubled_height) != height * 2 or planes != 1 or bits != 32 or compression != 0:
        raise ValueError('Use a PNG or uncompressed 32-bit image in icon.ico')
    pixels = payload[header:header + width * height * 4]
    if len(pixels) != width * height * 4:
        raise ValueError('ICO bitmap is truncated')
    alpha_present = any(pixels[n] for n in range(3, len(pixels), 4))
    mask_stride = (width + 31) // 32 * 4
    mask = payload[header + len(pixels):header + len(pixels) + mask_stride * height]
    if not alpha_present and len(mask) != mask_stride * height:
        raise ValueError('ICO has neither an alpha channel nor a complete transparency mask')
    rows = bytearray()
    for y in range(height):
        source_y = height - 1 - y if doubled_height > 0 else y
        rows.append(0)  # PNG filter None; the RGBA samples are unchanged.
        for x in range(width):
            b, g, red, alpha = pixels[(source_y * width + x) * 4:(source_y * width + x + 1) * 4]
            if not alpha_present:
                alpha = 0 if mask[source_y * mask_stride + x // 8] & (0x80 >> (x % 8)) else 255
            rows.extend((red, g, b, alpha))
    def chunk(kind, content):
        return struct.pack('>I', len(content)) + kind + content + struct.pack('>I', zlib.crc32(kind + content))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')


def prepare():
    images = frames((ROOT / 'icon.ico').read_bytes())
    width, height, _, payload = max(images, key=lambda item: (item[0] * item[1], item[2]))
    result = png_image(width, height, payload)
    ANDROID_ICON.parent.mkdir(parents=True, exist_ok=True)
    if not ANDROID_ICON.is_file() or ANDROID_ICON.read_bytes() != result:
        ANDROID_ICON.write_bytes(result)
    return dict(width=width, height=height, frames=len(images), resized=False)


def verify_windows(path):
    """Inspect only the built host PE, never the source game executable."""
    data = Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    optional = pe + 24
    if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Icon check requires the built x64 host PE')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    section_start = optional + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, section_start + n * 40 + 8) for n in range(count)]
    def offset(rva, length=1):
        for virtual_size, base, size, raw in sections:
            if base <= rva and rva - base + length <= size and raw + rva - base + length <= len(data):
                return raw + rva - base
        raise ValueError('Host resource RVA exceeds its file section')
    resource_rva = struct.unpack_from('<I', data, optional + 112 + 2 * 8)[0]
    if not resource_rva:
        raise ValueError('The built host has no Windows icon resources')
    root = offset(resource_rva)
    resources = {}
    def visit(relative, keys):
        at = root + relative
        named, ids = struct.unpack_from('<HH', data, at + 12)
        for n in range(named + ids):
            name, child = struct.unpack_from('<II', data, at + 16 + n * 8)
            if name & 0x80000000:
                continue
            if child & 0x80000000:
                if len(keys) >= 2:
                    raise ValueError('Unexpected host resource hierarchy')
                visit(child & 0x7fffffff, keys + (name,))
            else:
                rva, size = struct.unpack_from('<II', data, root + child)
                position = offset(rva, size)
                resources[keys + (name,)] = data[position:position + size]
    visit(0, ())
    groups = [(keys, blob) for keys, blob in resources.items() if keys[:2] == (14, 101)]
    expected = frames((ROOT / 'icon.ico').read_bytes())
    if len(groups) != 1:
        raise ValueError('The application icon group 101 is missing or ambiguous')
    keys, group = groups[0]
    if struct.unpack_from('<HHH', group) != (0, 1, len(expected)):
        raise ValueError('Host ICO directory differs from icon.ico')
    for n, (width, height, _, payload) in enumerate(expected):
        w, h, _, _, _, _, size, image_id = struct.unpack_from('<BBBBHHIH', group, 6 + n * 14)
        if (w or 256, h or 256, size) != (width, height, len(payload)) or resources.get((3, image_id, keys[2])) != payload:
            raise ValueError('Host icon image differs from icon.ico')
    return dict(group_id=101, frames=len(expected), source_images_byte_equal=True)


def verify_android(path, manifest_tree, resource_values):
    expected = ANDROID_ICON.read_bytes()
    with zipfile.ZipFile(path) as package:
        images = [name for name in package.namelist() if name.startswith('res/') and name.endswith('/ic_launcher_art.png')]
        adaptive = [name for name in package.namelist() if 'mipmap-anydpi-v26/' in name and name.endswith('/ic_launcher.xml')]
        if len(images) != 1 or package.read(images[0]) != expected or not adaptive:
            raise ValueError('Android launcher artwork/adaptive icon is missing or changed')
    regular = re.search(r':icon\([^\n]*?=@(0x[0-9a-fA-F]+)', manifest_tree)
    rounded = re.search(r':roundIcon\([^\n]*?=@(0x[0-9a-fA-F]+)', manifest_tree)
    if not regular or not rounded or regular[1] != rounded[1]:
        raise ValueError('Android application icon references are missing or disagree')
    colours = ET.parse(ROOT / 'android/app/src/main/res/values/icon_colors.xml').getroot()
    background = [value.text.strip().lower() for value in colours.findall('color') if value.get('name') == 'ic_launcher_background']
    if len(background) != 1 or not re.fullmatch(r'#[0-9a-f]{6}(?:[0-9a-f]{2})?', background[0]):
        raise ValueError('Invalid adaptive launcher background colour')
    expected_background = '#ff' + background[0][1:] if len(background[0]) == 7 else background[0]
    compiled_background = re.findall(r'(?mi)^\s*resource\s+0x[0-9a-f]+\s+color/ic_launcher_background\s*\n\s*\(\)\s+(#[0-9a-f]{8})\s*$', resource_values)
    if compiled_background != [expected_background]:
        raise ValueError('APK adaptive icon background differs from the configured colour')
    return dict(artwork=images[0], adaptive_icon=adaptive[0], resource_id=regular[1], source_png_byte_equal=True,
                adaptive_background_argb=expected_background, compiled_background_matches=True)


if __name__ == '__main__':
    print(prepare())
