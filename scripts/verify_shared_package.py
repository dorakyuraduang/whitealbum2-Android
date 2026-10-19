"""Audit actual packaged bytes and link provenance; never run the game."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]
COMMON_TARGETS=('wa2_shared_aot.dir','wa2_function_process.dir','wa2_shared_audio.dir','wa2_aot_memory.dir',
    'wa2_initial_data.dir','wa2_platform.dir','wa2_engine.dir','wa2_shared_graphics.dir','wa2_fixed_pipeline.dir',
    'wa2_common_shader_parser.dir','wa2_common_shader_assembler.dir','wa2_common_shader_gl.dir','wa2_movie.dir','wa2_wave_codec.dir','wa2_pixel_transfer.dir','wa2_scalar_math.dir')


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_resource_table(path):
    """Check Android's install-time resource-table rule in the final APK."""
    with zipfile.ZipFile(path) as package:
        try:info=package.getinfo('resources.arsc')
        except KeyError as error:raise ValueError('Missing Android resource table') from error
        if info.compress_type!=zipfile.ZIP_STORED:
            raise ValueError('resources.arsc must be stored uncompressed for Android 11+ installation')
        with Path(path).open('rb') as source:
            source.seek(info.header_offset);header=source.read(30)
        if len(header)!=30 or header[:4]!=b'PK\x03\x04':raise ValueError('Invalid resource table ZIP header')
        if struct.unpack_from('<H',header,8)[0]!=zipfile.ZIP_STORED:raise ValueError('Resource table ZIP compression headers disagree')
        name_size,extra_size=struct.unpack_from('<HH',header,26)
        offset=info.header_offset+30+name_size+extra_size
        if offset%4:raise ValueError('resources.arsc must be aligned on a 4-byte boundary for Android installation')
        return dict(name=info.filename,compression='stored',data_offset=offset,four_byte_aligned=True)


def common_sources(directory):
    commands=json.loads((directory/'compile_commands.json').read_text())
    return sorted(str(Path(item['file']).resolve()).lower() for item in commands
        if any(target in item.get('command','') for target in COMMON_TARGETS))


def verify(status,publish=False):
    flags=subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW
    for name,sha in status['source_sha256'].items():
        if digest(ROOT/name)!=sha:raise ValueError('Packaging source changed: '+name)
    verification=Path(status['verification_report'])
    if digest(verification)!=status['verification_sha256']:raise ValueError('Compiled verification changed')
    proof=json.loads(verification.read_text())
    if not proof['passed']:raise ValueError('Shared code verification failed')
    if not proof['shaders'].get('offline_glsl_compile_and_link_verified'):raise ValueError('Missing offline GLSL compile/link verification')
    for name,sha in proof['source_sha256'].items():
        if digest(ROOT/name)!=sha:raise ValueError('Compiled source/library is stale: '+name)
    for host,artifact in status['artifacts'].items():
        if digest(Path(artifact['file']))!=artifact['sha256']:raise ValueError('Packaged artifact changed: '+host)
    apk=Path(status['artifacts']['android']['file']);libraries=[]
    resource_table=verify_resource_table(apk)
    with zipfile.ZipFile(apk) as package:
        names=package.namelist()
        if len(names)!=len(set(names)) or package.testzip() is not None:raise ValueError('Duplicate or damaged APK entry')
        if any(name.lower().endswith(('.pak','.exe','.tga','.bmp','.ogg','.wmv')) for name in names):raise ValueError('Game resources in APK')
        dex=package.read('classes.dex')
        for value in (b'dex\n',b'Lorg/wa2/port/LauncherActivity;',b'Lorg/wa2/port/LaunchSettings;',b'Lorg/wa2/port/Wa2Activity;',b'Lorg/wa2/port/FrameHud;',b'Lorg/wa2/port/ResourceDirectory;',
                      b'Lorg/wa2/port/NativeWidgets;',b'Lorg/wa2/port/FileChooser;',b'Lorg/libsdl/app/SDLActivity;'):
            if value not in dex:raise ValueError('Missing Android application class/header: '+str(value))
        so_names={name for name in names if name.endswith('.so')}
        if so_names!={'lib/arm64-v8a/'+name for name in status['libraries']}:raise ValueError('Unexpected native APK member')
        for name,item in status['libraries'].items():
            data=package.read('lib/arm64-v8a/'+name);sha=hashlib.sha256(data).hexdigest()
            if sha!=item['stripped_sha256']:raise ValueError('APK library differs from the actual linked host: '+name)
            if data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=183:raise ValueError('Wrong ELF architecture')
            offset=struct.unpack_from('<Q',data,32)[0];stride,count=struct.unpack_from('<HH',data,54)
            loads=[struct.unpack_from('<Q',data,offset+n*stride+48)[0] for n in range(count) if struct.unpack_from('<I',data,offset+n*stride)[0]==1]
            if not loads or min(loads)<16384:raise ValueError('ELF not aligned for 16 KiB pages: '+name)
            libraries.append(dict(name=name,bytes=len(data),compressed_bytes=package.getinfo('lib/arm64-v8a/'+name).compress_size,sha256=sha,load_alignments=loads))
    ndk=ROOT/'tools/android-sdk/ndk/27.0.12077973';binpath=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin'
    def run(command):return subprocess.run(list(map(str,command)),cwd=ROOT,creationflags=flags,capture_output=True,text=True,encoding='utf-8',errors='replace',check=True,timeout=30).stdout
    build_tools=ROOT/'tools/android-sdk/build-tools/34.0.0'
    signer=run([ROOT/'tools/source-analysis/jdk-21.0.12.1+1/bin/java.exe','-XX:ActiveProcessorCount=1','-Xmx256m',
        '-jar',build_tools/'lib/apksigner.jar','verify','--verbose',apk])
    if not re.search(r'Verified using v2 scheme.*: true',signer):raise ValueError('APK v2 signature is required for every supported Android version')
    run([build_tools/'zipalign.exe','-c','4',apk])
    from verify_sdl_jni import verify as verify_sdl_jni
    sdl=Path(status['libraries']['libSDL2.so']['stripped_file'])
    sdl_jni=verify_sdl_jni(sdl.parent/'classes',sdl)
    actual=Path(proof['actual_android_host']['file'])
    if digest(actual)!=proof['actual_android_host']['sha256']:raise ValueError('Actual game library no longer matches the AOT proof')
    symbols=run([binpath/'llvm-nm.exe','--defined-only','--demangle',actual])
    if re.search(r'Zydis|shared_aot_oracle|native_weather_color_snapshot|wr_x87_|wr_sse_|NativeMathReferenceScope|native_math_ports_enabled|native_math_test_counts|wa2::game::|wa2::script::|wa2::aot::f_[0-9a-f]+<true>',symbols):
        raise ValueError('Decoder, diagnostic instruction bodies or retired independent story implementation linked')
    for function in ('native_atan2','native_sine','native_cosine'):
        if 'wa2::aot::'+function+'(' not in symbols:raise ValueError('The native CRT function port is not linked: '+function)
    exported=run([binpath/'llvm-nm.exe','-D','--defined-only',status['libraries']['libwa2_android.so']['stripped_file']])
    for name in (' SDL_main',' JNI_OnLoad',' JNI_OnUnload',' Java_org_wa2_port_Wa2Activity_nativeResetFrameMetrics'):
        if name not in exported:raise ValueError('Missing actual Android entry: '+name)
    elf=run([binpath/'llvm-readelf.exe','-d',actual]);needed=re.findall(r'\(NEEDED\).*?\[(.*?)\]',elf)
    allowed={'libSDL2.so','libc++_shared.so','liblog.so','libandroid.so','libatomic.so','libm.so','libc.so','libdl.so'}
    if not needed or set(needed)-allowed:raise ValueError('Unexpected Android dependency: '+str(needed))
    tree=run([ROOT/'tools/android-sdk/build-tools/34.0.0/aapt.exe','dump','xmltree',apk,'AndroidManifest.xml'])
    if not 0<=tree.find('E: uses-sdk')<tree.find('E: application'):raise ValueError('SDK declaration order is invalid')
    if not re.search(r'android:glEsVersion.*0x30000',tree):raise ValueError('Actual renderer requires GLES 3')
    for value in ('"org.wa2.port"','".LauncherActivity"','".Wa2Activity"','".FailureActivity"','android:minSdkVersion','android:targetSdkVersion'):
        if value not in tree:raise ValueError('Missing manifest entry: '+value)
    win=Path(status['artifacts']['windows']['file']);data=win.read_bytes()
    if data[:2]!=b'MZ' or struct.unpack_from('<H',data,struct.unpack_from('<I',data,0x3c)[0]+4)[0]!=0x8664:
        raise ValueError('Windows host is not a native x64 PE')
    coff=run([binpath/'llvm-readobj.exe','--coff-imports',win]);dlls=re.findall(r'^\s*Name: (.*?\.dll)',coff,re.M|re.I)
    if any(name.lower() in ('d3d9.dll','ddraw.dll','dsound.dll','winmm32.dll') for name in dlls):raise ValueError('Legacy graphics/audio DLL imported')
    relink=Path(status['relink_materials'])
    if digest(relink)!=status['relink_sha256']:raise ValueError('Matching relink materials changed')
    with zipfile.ZipFile(relink) as archive:
        manifest=json.loads(archive.read('manifest.json'))
        if not manifest['relink_recipe_verified']:raise ValueError('Relink recipes not checked')
        for host,artifact in status['artifacts'].items():
            if manifest['artifacts'][host]['sha256']!=artifact['sha256']:raise ValueError('Relink materials belong to another build')
        for name,item in manifest['materials'].items():
            if hashlib.sha256(archive.read(name)).hexdigest()!=item['sha256']:raise ValueError('Damaged relink member: '+name)
    result=dict(schema='wa2.shared-package-audit.v1',artifacts=status['artifacts'],libraries=libraries,
        android_resource_table=resource_table,
        android_signature_v2_verified=True,android_zip_alignment_verified=True,
        android_sdl_java_native_bindings=sdl_jni,
        common_source_set_compiled=True,actual_shared_android_entry_verified=True,android_needed=needed,windows_dlls=dlls,
        runtime_instruction_decoder_linked=False,jit_linked=False,legacy_independent_scene_host_linked=False,
        original_resources_in_apk=False,matching_relink_materials=True,game_launched=False,screen_access=False,
        android_device_verified=False,complete_gameplay_verified=False,passed=True)
    if publish:
        (ROOT/'logs/shared-package-verification.json').write_text(json.dumps(result,indent=2)+'\n')
        readme=('WHITE ALBUM2 共用静态重编译开发包\n\n'
            'Windows x64 和 Android ARM64 编译同一套游戏静态函数、资源、音频和渲染代码，从保留的原版入口执行。'
            '此包已替换旧的独立剧情演示实现。\n\n'
            'Android：安装 WA2-arm64-dev.apk，要求 Android 8.0+、ARM64、OpenGL ES 3.0。'
            '启动页选择本机存储中的 Wa2Res（默认内部共享存储根目录），保留 IC 子目录；可保存目录授权和 FPS 显示开关，再点击开始游戏。'
            'APK 不包含游戏资源，不需要批量解包或转码。点击对应鼠标左键，返回键对应 Escape；原版菜单操作由共用游戏代码处理。'
            '开启帧数显示后，左上角显示实际提交帧的 FPS 和平均帧间隔；请求 120 Hz 不代表已经达到 120 FPS。\n\n'
            'Windows：WA2_SDL2_x64.exe 可以放在原版 PAK 旁运行，或使用 --root 指定资源目录。\n\n'
            '编译、差分状态、内存接口和安装包审计已通过；按用户约束没有启动游戏、模拟器或读取屏幕。'
            '完整流程、手机画面和性能尚未实测。仍有 '+str(len(status['unsupported_instruction_sites']))+
            ' 处未验证的 x87 超越运算会明确报错；未知跳转和不支持的系统行为同样不会被跳过。\n\n'
            'WA2-shared-relink.zip 为两端通用解码库的对应源码、构建配置和应用重新链接材料，不需复制到手机。\n')
        (ROOT/'out/README-shared.txt').write_text(readme,encoding='utf-8')
        (ROOT/'out/android/README.txt').write_text(readme,encoding='utf-8')
    return result


if __name__=='__main__':
    kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000)
    verify(json.loads((ROOT/'logs/shared-host-package.json').read_text()),publish=True)
