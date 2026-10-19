"""Reject resource-table layouts Android refuses, without installing or running."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from verify_shared_package import verify_resource_table


def main():
    checks=0
    with tempfile.TemporaryDirectory(prefix='apk-install-rules-',dir=ROOT/'build') as temporary:
        directory=Path(temporary)
        for compression,aligned in ((zipfile.ZIP_DEFLATED,False),(zipfile.ZIP_STORED,False),(zipfile.ZIP_STORED,True)):
            path=directory/(str(compression)+'-'+str(aligned)+'.apk')
            info=zipfile.ZipInfo('resources.arsc');info.compress_type=compression
            if aligned:
                # Local header 30 + name 14 + extra 4 = 48, divisible by four.
                info.extra=struct.pack('<HH',0xcafe,0)
            else:
                # Local header 30 + name 14 + extra 5 = 49, deliberately unaligned.
                info.extra=struct.pack('<HHB',0xcafe,1,0)
            with zipfile.ZipFile(path,'w') as package:package.writestr(info,b'resource-table-fixture')
            if compression==zipfile.ZIP_STORED and aligned:
                assert verify_resource_table(path)['four_byte_aligned']
            else:
                try:verify_resource_table(path)
                except ValueError as error:
                    assert ('uncompressed' if compression==zipfile.ZIP_DEFLATED else '4-byte') in str(error)
                else:raise AssertionError('An un-installable resource table was accepted')
            checks+=1
        missing=directory/'missing.apk'
        with zipfile.ZipFile(missing,'w') as package:package.writestr('classes.dex',b'fixture')
        try:verify_resource_table(missing)
        except ValueError as error:assert 'Missing' in str(error)
        else:raise AssertionError('Missing resource table was accepted')
        checks+=1
    print(json.dumps(dict(checks=checks,passed=True,game_launched=False,screen_access=False)))


if __name__=='__main__':main()
