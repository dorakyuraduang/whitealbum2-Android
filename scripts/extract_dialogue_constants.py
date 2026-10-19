"""Copy typed dialogue tables from the existing build cache, not the source EXE."""
import hashlib
import json
from pathlib import Path
from cached_game_data import Constants

ROOT=Path(__file__).resolve().parents[1]


def literal(value):
    return 'std::string_view("'+''.join('\\x%02x'%v for v in value)+'",'+str(len(value))+')'


def main():
    constants=Constants()
    def string(address):return constants.read(address,128).split(b'\0')[0] if address else b''
    names=[string(constants.number(0x4be348+i*4,'I')) for i in range(32)]
    fonts=[string(a) for a in (0x4a2bb4,0x4a2ba8,0x4a2b98,0x4a2b8c)]
    special=[string(a) for a in (0x4a1f14,0x4a1f0c,0x4a1f04,0x4a1ef8,0x4a1ef0,0x4a1ee8,
        0x4a1ee0,0x4a1ed8,0x4a1ed0,0x4a1ec8,0x4a1ec0,0x4a1eb8,0x4a1eb0,0x4a1ea4,
        0x4a1e98,0x4a1e94,0x4a1e84,0x4a1e7c,0x4a1e6c,0x4a1e64)]
    kinsoku=[constants.number(a,'H') for a in (*range(0x4a5334,0x4a52f0,-4),0x4a3090)]
    output='// Typed constants from the existing compilation cache; see logs/native-dialogue-constants.json.\n'
    for name,values in (('speaker_names',names),('font_names',fonts),('special_names',special)):
        output+='static constexpr std::array<std::string_view,%d> %s{{\n'%(len(values),name)
        output+=''.join(literal(v)+',\n' for v in values)+'}};\n'
    output+='static constexpr std::array<std::uint16_t,%d> hanging_punctuation{{'%len(kinsoku)
    output+=','.join('0x%04x'%v for v in kinsoku)+'}};\n'
    output+='static constexpr std::array<std::uint32_t,16> text_palette{{'
    output+=','.join('0x%08x'%constants.number(0x4c2538+i*4,'I') for i in range(16))+'}};\n'
    path=ROOT/'engine/dialogue_constants.inc';path.write_text(output)
    proof=dict(schema='wa2.native-dialogue-constants.v1',cached_object_sha256=constants.object_sha256,
        generated_source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),source_executable_opened=False,
        new_instruction_analysis=False,screen_access=False,speaker_count=len(names),font_count=len(fonts))
    (ROOT/'logs/native-dialogue-constants.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Copied native speaker/font/line-break tables from the existing nonexecutable build cache.')


if __name__=='__main__':main()
