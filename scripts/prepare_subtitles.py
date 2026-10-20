"""Compile the pinned main-story subtitle data; never import SpecialContents.

Normal builds only validate checked-in assets. Regenerating the small bitmap
charset additionally requires Pillow and the pinned, externally supplied font.
No upstream executable, DLL, Python script or game resource is executed/read.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'third_party/wa2_subtitles'
UPSTREAM = 'bb3cbc2f459549f8afbcb56cafd4b861d763bd29'
MAIN_BLOB = '373c94efbaaf9a4df50c8706f832462f8ce51b07'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def parse_source(data):
    """Read only the plain-scalar subset used by the pinned upstream file.

    Reject additional groups, YAML directives/objects, unknown fields, duplicate
    keys and ambiguous scalars instead of executing a general YAML constructor.
    """
    groups = {}
    group = track = cue = None
    for number, line in enumerate(data.decode('utf-8').splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        def fail(message):
            raise ValueError(f'main subtitle source line {number}: {message}')
        if line in ('soundEffect:', 'voice:'):
            group = line[:-1]
            if group in groups:
                fail('duplicate group')
            groups[group] = []
            track = cue = None
            continue
        match = re.fullmatch(r'    - (id|scene): ([0-9]+)', line)
        if match:
            if group is None or match[1] != ('id' if group == 'soundEffect' else 'scene'):
                fail('unexpected track')
            track = {match[1]: int(match[2])}
            groups[group].append(track)
            cue = None
            continue
        match = re.fullmatch(r'      id: ([0-9]+)', line)
        if match:
            if group != 'voice' or track is None or 'id' in track:
                fail('unexpected voice id')
            track['id'] = int(match[1])
            continue
        if line == '      content:':
            if track is None or 'content' in track:
                fail('unexpected content')
            track['content'] = []
            continue
        match = re.fullmatch(r'          - begin: ([0-9]+)', line)
        if match:
            if track is None or 'content' not in track:
                fail('cue outside content')
            cue = {'begin': int(match[1])}
            track['content'].append(cue)
            continue
        match = re.fullmatch(r'            end: ([0-9]+)', line)
        if match:
            if cue is None or 'end' in cue:
                fail('unexpected cue end')
            cue['end'] = int(match[1])
            continue
        if line.startswith('            text: '):
            value = line[len('            text: '):].strip(' \t')
            if cue is None or 'text' in cue or not value or len(value) > 512:
                fail('unexpected or overlong cue text')
            if value[0] in '!&*{}[]|>\'"%@`' or any(ord(c) < 32 or 0xd800 <= ord(c) <= 0xdfff for c in value):
                fail('only literal plain subtitle strings are supported')
            cue['text'] = value
            continue
        fail('unsupported structure')
    if set(groups) != {'soundEffect', 'voice'}:
        raise ValueError('Only main-story soundEffect and voice groups are supported')
    tracks = []
    keys = set()
    for kind, items in groups.items():
        for item in items:
            expected = {'id', 'content'} | ({'scene'} if kind == 'voice' else set())
            if set(item) != expected or not item['content'] or not 0 <= item['id'] <= 65535:
                raise ValueError('Incomplete main-story track')
            scene = item.get('scene', 0)
            if not (1 <= scene <= 65535 if kind == 'voice' else scene == 0):
                raise ValueError('Invalid original audio scene')
            key = scene << 16 | item['id']
            if key in keys:
                raise ValueError('Duplicate original audio identity')
            keys.add(key)
            end = 0
            for q in item['content']:
                if set(q) != {'begin', 'end', 'text'} or not end <= q['begin'] < q['end'] <= 0xffffffff:
                    raise ValueError('Invalid or overlapping subtitle interval')
                end = q['end']
            tracks.append(dict(key=key, **item))
    return sorted(tracks, key=lambda t: t['key'])


def array(name, values, type_name, width=24):
    result = [f'static constexpr {type_name} {name}[]={{']
    result += [','.join(str(v) for v in values[n:n+width])+',' for n in range(0, len(values), width)]
    result.append('};')
    return result


def compile_text(tracks, charset):
    lookup = {c: i for i, c in enumerate(charset)}
    text, cues, indexes = [], [], []
    for t in tracks:
        first = len(cues)
        for q in t['content']:
            offset = len(text)
            text.extend(lookup[c] for c in q['text'])
            cues.append((q['begin'], q['end'], offset, len(q['text'])))
        indexes.append((t['key'], first, len(t['content'])))
    result = ['// Generated only from nxs7 main-story script/sub.yaml (MIT).',
              '// SpecialContents data is deliberately excluded.']
    result += array('subtitle_characters', [ord(c) for c in charset], 'char32_t')
    result += array('subtitle_glyph_ids', text, 'std::uint16_t')
    result += ['static constexpr Cue subtitle_cues[]={']
    result += [f'{{{a}u,{b}u,{{subtitle_glyph_ids+{o}u,{n}u}}}},' for a,b,o,n in cues]
    result += ['};', 'static constexpr Track subtitle_tracks[]={']
    result += [f'{{{k}u,{{subtitle_cues+{o}u,{n}u}}}},' for k,o,n in indexes]
    result += ['};']
    return ('\n'.join(result)+'\n').encode()


def rle(data):
    result = bytearray()
    at = 0
    while at < len(data):
        value = data[at]
        count = 1
        while count < 16 and at+count < len(data) and data[at+count] == value:
            count += 1
        result.append((count-1) << 4 | value)
        at += count
    return bytes(result)


def font_assets(path, charset, expected_sha):
    from PIL import Image, ImageDraw, ImageFont
    font_bytes = path.read_bytes()
    if digest(font_bytes) != expected_sha:
        raise ValueError('Font differs from the pinned open-license input')
    font = ImageFont.truetype(str(path), 28)
    missing=font.getmask('\U0010ffff')
    missing_signature=(missing.size,bytes(missing))
    for c in charset:
        mask=font.getmask(c)
        if not c.isspace() and (mask.size,bytes(mask))==missing_signature:
            raise ValueError(f'Pinned font lacks subtitle character U+{ord(c):04X}')
    boxes = [font.getbbox(c, anchor='ls', stroke_width=2) for c in charset]
    left = min(b[0] for b in boxes); top = min(b[1] for b in boxes)
    cell = max(max(b[2] for b in boxes)-left, max(b[3] for b in boxes)-top)+2
    origin_x, baseline = 1-left, 1-top
    if cell > 40 or len(charset) > 1536:
        raise ValueError('Subtitle charset exceeds the bounded atlas')
    planes = bytearray()
    # Independent coverage masks make a white glyph with a black outline.
    for c in charset:
        fill=None
        for stroke in (0, 2):
            image = Image.new('L', (cell, cell), 0)
            ImageDraw.Draw(image).text((origin_x, baseline), c, font=font,
                anchor='ls', fill=255, stroke_width=stroke, stroke_fill=255)
            values=bytes((v+8)//17 for v in image.tobytes())
            if stroke:
                values=bytes(max(a,b) for a,b in zip(fill,values))
            else:
                fill=values
            planes.extend(values)
    packed = rle(planes)
    columns = 32
    rows = (len(charset)+columns-1)//columns
    result = ['// Generated coverage from AOSP Droid Sans Fallback (Apache 2.0).',
              '// Only the main-story subtitle charset is included; not a font file.',
              f'static constexpr FontSpec subtitle_font{{{cell}u,{columns}u,{rows}u,28u,{origin_x}u,{baseline}u}};']
    result += array('subtitle_font_rle', packed, 'std::uint8_t', 32)
    return ('\n'.join(result)+'\n').encode(), dict(cell=cell, columns=columns, rows=rows,
        glyphs=len(charset), packed_bytes=len(packed), coverage_sha256=digest(planes),
        atlas_rgba_bytes=cell*columns*cell*rows*4)


def prepare(font=None, check=False):
    provenance = json.loads((DATA/'source.json').read_text())
    source = (DATA/'sub.yaml').read_bytes()
    if provenance['upstream_commit'] != UPSTREAM or provenance['included_path'] != 'script/sub.yaml' or provenance['special_contents_included'] is not False:
        raise ValueError('Subtitle source scope changed')
    if digest(source) != provenance['source_sha256']:
        raise ValueError('Pinned main-story subtitle source changed')
    blob=hashlib.sha1(b'blob '+str(len(source)).encode()+b'\0'+source).hexdigest()
    if blob!=MAIN_BLOB or provenance['source_blob']!=MAIN_BLOB:
        raise ValueError('Subtitle data is not the pinned upstream main-story file')
    tracks = parse_source(source)
    charset = sorted({c for t in tracks for q in t['content'] for c in q['text']})
    text = compile_text(tracks, charset)
    outputs = {'main_story.inc': text}
    if font:
        if check:
            raise ValueError('--font and --check are mutually exclusive')
        outputs['font.inc'], provenance['font']['atlas'] = font_assets(font, charset, provenance['font']['sha256'])
        provenance['font']['generated_sha256'] = digest(outputs['font.inc'])
        (DATA/'source.json').write_text(json.dumps(provenance, indent=2)+'\n')
    if digest((DATA/'font.inc').read_bytes() if 'font.inc' not in outputs else outputs['font.inc']) != provenance['font']['generated_sha256']:
        raise ValueError('Pinned subtitle coverage asset changed')
    for name, data in outputs.items():
        target = DATA/name
        if check:
            if not target.is_file() or target.read_bytes() != data:
                raise ValueError('Stale compiled main-story subtitle data: '+name)
        elif not target.is_file() or target.read_bytes() != data:
            target.write_bytes(data)
    return dict(schema='wa2.main-story-subtitle-assets.v1', upstream_commit=UPSTREAM,
        tracks=len(tracks), sound_effects=sum(t['key'] < 65536 for t in tracks),
        voices=sum(t['key'] >= 65536 for t in tracks), cues=sum(len(t['content']) for t in tracks),
        glyphs=len(charset), special_contents_included=False, source_sha256=digest(source),
        font=provenance['font']['atlas'], passed=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    print(json.dumps(prepare(args.font, args.check)))
