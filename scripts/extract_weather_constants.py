"""Recover the existing weather curve as typed constants, without the source EXE."""
import hashlib
import json
from pathlib import Path
import struct
from cached_game_data import Constants,CACHED,ROOT


def main():
    source=Constants();data=source.read(0x4a5b28,512)
    values=struct.unpack('<256h',data)
    destination=ROOT/'engine/weather_curve.inc'
    code='// Typed weather curve from the prior compilation cache; see logs/native-weather-constants.json.\n'
    code+='static constexpr std::array<std::int16_t,256> weather_curve{{\n'
    code+=''.join(','.join(map(str,values[n:n+16]))+',\n' for n in range(0,256,16))+'}};\n'
    destination.write_text(code)
    constants={hex(at):source.number(at) for at in (0x4a1e00,0x4a2248,0x4a2288,0x4a2290,0x4a2300,
        0x4a2308,0x4a2310,0x4a2318,0x4a2320,0x4a2330,0x4a2338,0x4a2340,0x4a2348)}
    proof=dict(schema='wa2.native-weather-constants.v1',cached_object=str(CACHED),cached_object_sha256=source.object_sha256,
        curve_source_address=0x4a5b28,curve_entries=len(values),curve_sha256=hashlib.sha256(data).hexdigest(),
        generated_source=str(destination),generated_source_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
        constants=constants,original_executable_opened=False,new_instruction_analysis=False,screen_access=False)
    (ROOT/'logs/native-weather-constants.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Recovered 256 weather curve entries from prior cached data; no source EXE opened.')


if __name__=='__main__':main()
