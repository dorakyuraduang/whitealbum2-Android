"""Copy character prefixes/placement from the existing typed data cache."""
import hashlib
import json
from pathlib import Path
import re
from cached_game_data import Constants

ROOT=Path(__file__).resolve().parents[1]


def main():
    constants=Constants()
    source=(ROOT/'build/source-analysis/functions/00415ab0.c').read_text()
    addresses=[int(re.search(r'local_3a0\['+str(n)+r'\] = &PTR_DAT_([0-9a-f]+);',source)[1],16) for n in range(4)]
    for offset in range(0x390,0x308,-4):
        addresses.append(int(re.search(r'local_'+format(offset,'x')+r' = &(?:PTR_)?DAT_([0-9a-f]+);',source)[1],16))
    names=[constants.read(a,16).split(b'\0')[0].decode('ascii') for a in addresses]
    positions=[constants.number(0x4be0bc+n*4,'i') for n in range(11)]
    output='// Typed original tables from the existing build cache; no executable input.\n'
    output+='static constexpr std::array<std::string_view,%d> character_prefixes{{'%len(names)
    output+=','.join('"'+name+'"' for name in names)+'}};\n'
    output+='static constexpr std::array<std::int32_t,11> character_positions{{'+','.join(map(str,positions))+'}};\n'
    path=ROOT/'engine/character_constants.inc';path.write_text(output)
    proof=dict(schema='wa2.native-character-constants.v1',prefixes=names,positions=positions,
        cached_object_sha256=constants.object_sha256,generated_source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        source_executable_opened=False,new_instruction_analysis=False)
    (ROOT/'logs/native-character-constants.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Copied',len(names),'native character prefixes and',len(positions),'positions.')


if __name__=='__main__':main()
