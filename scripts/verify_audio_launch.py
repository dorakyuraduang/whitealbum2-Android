"""Check real-game SDL PCM refills from an ordinary background launch's logs."""
from pathlib import Path
import hashlib
import json
import shutil
import uuid
ROOT=Path(__file__).resolve().parents[1]
from verify_bgm_stream import stream

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def bound(path):return dict(file=str(path),sha256=digest(path))

def check():
    launch_path=ROOT/'logs/fixed-startup-verification.json';launch=json.loads(launch_path.read_text())
    assert launch['output_sha256']==digest(ROOT/'out/WA2_SDL2_x64.exe')
    assert not launch['error'] and launch['alive_at_test_stop'] and launch['elapsed_seconds']>=75
    assert not launch['screen_access'] and not launch['automated_input'] and not launch['fps_analysis']
    source=Path(launch['report']['file']);assert digest(source)==launch['report']['sha256']
    report=json.loads(source.read_text());assert not report.get('error');state=report.get('process',report);audio=state['directsound']
    assert audio['backend']=='sdl2-native-pcm' and audio['plays']>0 and audio['output_callbacks']>0 and audio['callback_errors']==0
    codec=state['native_vorbis']
    assert codec['errors']==codec['declined']==0 and codec['output_bytes']>0
    assert codec['requests']<=codec['input_bytes']//4096+1000 and codec['exhausted_calls']<1000
    assert codec['exhausted_calls']>0 # This observation must cross an actual non-looping stream end.
    case=ROOT/'build/audio-launch-check'/uuid.uuid4().hex;case.mkdir(parents=True);shutil.copyfile(source,case/'runtime.json')
    refill=stream(case);assert refill['refill_deadlines_met'] and refill['observed_stream_seconds']>=30
    return dict(schema='wa2.native-audio-launch.v1',output_sha256=launch['output_sha256'],launch=bound(launch_path),runtime=bound(source),
        audio_backend=audio['backend'],output_callbacks=audio['output_callbacks'],rendered_frames=audio['rendered_frames'],
        largest_lock_bytes=audio['largest_lock_bytes'],
        native_codec=codec,
        callback_errors=audio['callback_errors'],stream=refill,screen_access=False,automated_input=False,fps_analysis=False,
        auditory_listening_performed=False,actual_gameplay_fast_forward_verified=False,normal_exit_verified=False,passed=True)

if __name__=='__main__':
    result=check();(ROOT/'logs/native-audio-launch-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(audio_backend=result['audio_backend'],output_callbacks=result['output_callbacks'],stream=result['stream']),indent=2))
