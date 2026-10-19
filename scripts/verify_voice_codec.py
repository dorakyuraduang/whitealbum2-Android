"""Bind the supplementary real-voice codec comparison to the execution suite."""
from pathlib import Path
import hashlib
import json
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def bound(path):return dict(file=str(Path(path).resolve()),sha256=digest(path))
def read(path):return json.loads(Path(path).read_text())
def check():
    samples=read(ROOT/'logs/voice-codec-samples.json');report=read(ROOT/'logs/native-voice-codec-tests.json')
    assert samples['original_payloads_unchanged'] and samples['source_bytes']==Path(samples['source']).stat().st_size
    assert digest(samples['fixture'])==samples['fixture_sha256']
    with Path(samples['source']).open('rb') as source:
        for sample in samples['samples']:
            source.seek(sample['offset']);assert hashlib.sha256(source.read(sample['size'])).hexdigest()==sample['sha256']
            assert sample['channels']==1 and sample['rate']==48000
    assert report['passed'] and report['fragmentation_matches'] and report['loop_restart_matches']
    assert report['non_looping_eof_matches']
    assert report['native_lifecycle'] and report['metadata_matches_original'] and report['paged_memory_matches']
    assert report['original_format_query_preserved'] and report['output_canary_unchanged'] and len(report['tracks'])==3
    assert [v['member'] for v in report['tracks']]==[v['name'] for v in samples['samples']]
    assert all(v['maximum_sample_error']<=16 and v['rms_sample_error']<=1.5 for v in report['tracks'])
    fixture=next(v for v in read(ROOT/'logs/execution-suite.json')['fixtures'] if v['fixture']=='wa2_native_vorbis_tests')
    program=ROOT/'build/wa2-win64/wa2_port/wa2_native_vorbis_tests.exe';assert digest(program)==fixture['program_sha256']
    return dict(schema='wa2.voice-codec-verification.v1',program=bound(program),samples=bound(ROOT/'logs/voice-codec-samples.json'),
        report=bound(ROOT/'logs/native-voice-codec-tests.json'),log=bound(ROOT/'logs/native-voice-codec-tests.log'),
        tracks=3,source_payload_hashes_match=True,screen_access=False,passed=True)
if __name__=='__main__':
    result=check();(ROOT/'logs/native-voice-codec-verification.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS three real mono voices; original PCM lengths, bounded sample error, fragmentation and restart')
