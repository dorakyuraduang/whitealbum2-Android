"""Self-authored container/caption fixtures; no game files or playback."""
import sys
from pathlib import Path
import struct
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from special_mkv_subtitles import vint,elements,block,inspect,ogg
from prepare_special_english_resources import plain,merge,encode

def element(key,data):
    raw=key.to_bytes((key.bit_length()+7)//8,'big')
    width=next(n for n in range(1,9) if len(data)<(1<<(7*n))-1)
    return raw+((1<<(7*width))|len(data)).to_bytes(width,'big')+data

class ImportChecks(unittest.TestCase):
    def test_lacing(self):
        frames=[b'ab',b'cde',b'fghi']
        for lace,header in [(1,b'\x02\x02\x03'),(3,b'\x02\x82\xc0')]:
            raw=b'\x81\x00\x07'+bytes([lace<<1])+header+b''.join(frames)
            self.assertEqual(block(raw,0,len(raw)),(1,7,frames))
        raw=b'\x81\xff\xfe\x04\x02abcdef'
        self.assertEqual(block(raw,0,len(raw)),(1,-2,[b'ab',b'cd',b'ef']))
        raw=b'\x81\x00\x00\x00single';self.assertEqual(block(raw,0,len(raw))[2],[b'single'])
        for raw in [b'\x00',b'\x81',b'\x81\0\0\x04\x02abcde',b'\x81\0\0\x02\x02\xff']:
            with self.assertRaises((ValueError,IndexError)):block(raw,0,len(raw))
    def test_container_clock_and_und(self):
        def track(number,codec,language):return element(0xae,element(0xd7,bytes([number]))+element(0x86,codec)+element(0x22b59c,language))
        info=element(0x1549a966,element(0x2ad7b1,b'\x0f\x42\x40')+element(0x4489,struct.pack('>d',1000)))
        tracks=element(0x1654ae6b,track(1,b'A_VORBIS',b'jpn')+track(2,b'S_TEXT/UTF8',b'und'))
        group=element(0xa0,element(0xa1,b'\x82\0\x05\0Fixture caption')+element(0x9b,b'\x14'))
        cluster=element(0x1f43b675,element(0xe7,b'\x0a')+group)
        raw=element(0x18538067,info+tracks+cluster)
        self.assertEqual(inspect(raw)['captions'],[[15,35,'Fixture caption']])
        with self.assertRaises(ValueError):inspect(raw[:-1])
        with self.assertRaises(ValueError):list(elements(b'\xa3\x85abc'))
        with self.assertRaises(ValueError):vint(b'\0',0)
    def test_overlay_and_text(self):
        self.assertEqual(merge([[0,10,'A'],[5,15,'B']]),[[0,5,'A'],[5,10,'A\nB'],[10,15,'B']])
        self.assertEqual(merge([[0,5,'A'],[5,10,'A']]),[[0,10,'A']])
        self.assertEqual(plain('<font size="36"><i>A &amp; B</i></font>'),'A & B')
        for bad in ['<script>bad</script>','\0','x'*513]:
            with self.assertRaises(ValueError):plain(bad)
        output,counts=encode({80<<16:[[10,20,'A\nB']]})
        self.assertEqual(output[:12],b'SCU1'+struct.pack('<II',2,1));self.assertEqual(counts,dict(tracks=1,cues=1,codepoints=3,characters=2))
    def test_bad_ogg(self):
        for raw in [b'',b'OggS',b'OggS\0'+bytes(30)]:
            with self.assertRaises(ValueError):ogg(raw)

if __name__=='__main__':unittest.main()
