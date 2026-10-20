"""Audit main-only subtitle generation and the bounded native memory report."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from prepare_subtitles import DATA, ROOT, parse_source, prepare


def verify(report_path, manifest_path=None):
    assets=prepare(check=True)
    tracks=parse_source((DATA/'sub.yaml').read_bytes())
    report=json.loads(Path(report_path).read_text())
    glyphs=sum(len(c['text']) for t in tracks for c in t['content'])
    if (not report.get('passed') or report['tracks']!=46 or report['cues']!=315 or
        report['characters']!=891 or report['time_boundaries']!=1890 or
        report['glyph_quads']!=glyphs or report['source_audio_events']<20 or report['generated_static_returns']!=4 or
        report['presentation_cases']!=16 or report['projected_cues']!=315*16 or report['projected_glyphs']!=glyphs*16 or
        report['invalid_viewports']!=9 or report['backbuffer_independent_projection'] is not True or
        report['rejections']<8 or report['special_contents_included'] is not False or
        report['source_executable_opened'] or report['game_launched'] or report['graphics_device_opened'] or report['screen_access']):
        raise ValueError('Main-story subtitle memory checks are incomplete')
    # Independently expand the committed bitmap format and compare every final
    # RGBA byte through its FNV64. Neither side opens a font, GPU or game image.
    font=(DATA/'font.inc').read_text()
    dimensions=re.search(r'subtitle_font\{([0-9u,]+)\}',font)
    cell,columns,rows,advance,origin,baseline=[int(x.rstrip('u')) for x in dimensions[1].split(',')]
    runs=bytes(map(int,re.search(r'subtitle_font_rle\[\]=\{(.*?)\};',font,re.S)[1].replace('\n','').strip(',').split(',')))
    masks=b''.join(bytes([b&15])*((b>>4)+1) for b in runs)
    provenance=json.loads((DATA/'source.json').read_text())
    if hashlib.sha256(masks).hexdigest()!=provenance['font']['atlas']['coverage_sha256']:
        raise ValueError('Subtitle fill/outline expansion differs from the generated bitmap')
    width,height=cell*columns,cell*rows
    image=bytearray(width*height*4)
    tile=cell*cell
    for n in range(891):
        fill=masks[(2*n)*tile:(2*n+1)*tile]
        stroke=masks[(2*n+1)*tile:(2*n+2)*tile]
        ox,oy=(n%columns)*cell,(n//columns)*cell
        for p,(a,b) in enumerate(zip(fill,stroke)):
            if a>b:
                raise ValueError('Subtitle outline does not cover its fill')
            colour=(a*255+b//2)//b if b else 0
            index=((oy+p//cell)*width+ox+p%cell)*4
            image[index:index+4]=bytes((colour,colour,colour,b*17))
    hash_value=14695981039346656037
    for b in image:
        hash_value=((hash_value^b)*1099511628211)&0xffffffffffffffff
    if str(hash_value)!=report['font_rgba_fnv64'] or len(image)!=report['font_rgba_bytes']:
        raise ValueError('Native subtitle font RGBA differs from the independent reference')
    # The importer deliberately rejects SC groups and YAML extension syntax.
    rejected=0
    small=b'soundEffect:\n    - id: 1\n      content:\n          - begin: 0\n            end: 5\n            text: hello\nvoice:\n'
    if len(parse_source(small))!=1:
        raise ValueError('Subtitle parser sanity check failed')
    for bad in (small+b'bgm:\n',small+b'specialContents:\n',small.replace(b'text: hello',b'text: !python/object malicious'),
        small.replace(b'end: 5',b'end: 0'),small.replace(b'    - id: 1',b'    - id: -1'),
        small.replace(b'text: hello',b'text: |'),small.replace(b'      content:',b'      unknown:'),
        small+small,small.replace(b'voice:\n',b'    - id: 1\n      content:\n          - begin: 1\n            end: 2\n            text: duplicate\nvoice:\n')):
        try:
            parse_source(bad)
        except (ValueError,UnicodeError):
            rejected+=1
        else:
            raise ValueError('Subtitle parser accepted unsupported or ambiguous input')
    result=dict(assets=assets,memory=report,independent_font_rgba_verified=True,
        rejected_import_inputs=rejected,original_executable_opened=False,screen_access=False,passed=True)
    if manifest_path:
        manifest=json.loads(Path(manifest_path).read_text())
        contract=json.loads((ROOT/'config/subtitle_hooks.json').read_text())
        expected=sorted(int(e['entry'],0) for e in contract['events'])
        hooks=manifest['main_story_subtitle_events']
        if hooks['function_entries']!=expected or hooks['return_locations']!=10 or not hooks['original_functions_preserved'] or hooks['per_instruction_polling']:
            raise ValueError('Subtitle source-return coverage changed')
        generated='\n'.join(p.read_text() for p in sorted((ROOT/'generated/shared-aot').glob('functions_*.cpp')))
        for event in contract['events']:
            instruction=f"notify_subtitle_return<Metered>({event['entry']}u,s,m,subtitle_arguments);"
            if generated.count(instruction)!=len(event['return_locations']):
                raise ValueError('Subtitle observer missing at a proven original return')
        result['static_audio_returns']=hooks
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report',type=Path)
    parser.add_argument('--manifest',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=verify(args.report,args.manifest)
    if args.output:
        args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
