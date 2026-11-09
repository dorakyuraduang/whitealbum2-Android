"""Compile both Android adapter sets and DEX the native one; never launch an app.

The retained AOT widget C++ fixtures are not available in this workspace. This
checks its Java adapter/type integration and existing pure gesture/path checks,
not the unavailable original widget transport fixture or on-device behavior.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
from android_source_sets import ROOT,java_sources,LEGACY_ONLY

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    if os.name!='nt':raise SystemExit('This verifier uses the locally verified Windows JDK/API tools')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)];kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
    process=kernel.GetCurrentProcess();current=ctypes.c_size_t();system=ctypes.c_size_t()
    if not kernel.SetPriorityClass(process,0x4000) or not kernel.GetProcessAffinityMask(process,ctypes.byref(current),ctypes.byref(system)) or not kernel.SetProcessAffinityMask(process,current.value&-current.value):raise ctypes.WinError()
    jdk=ROOT/'tools/source-analysis/jdk-21.0.12.1+1';api=ROOT/'tools/android-api-35/android.jar';bt=ROOT/'tools/android-sdk/build-tools/34.0.0'
    official=json.loads((ROOT/'logs/android-api35.json').read_text())
    if digest(api)!=official['android_jar_sha256']:raise ValueError('Android API stubs changed')
    source_sets={backend:java_sources(backend) for backend in ('shared-aot','native')}
    sdl=sorted((ROOT/'tools/SDL-release-2.32.10/android-project/app/src/main/java').rglob('*.java'))
    tests=[ROOT/'tests/android/org/wa2/port'/name for name in ('TouchGesturesTest.java','ResourceLocationTest.java','LegacySettingsPathTest.java','LegacySavePathTest.java','GameStorageTest.java','LaunchProfileTest.java','LauncherTextTest.java','CompatibleStoragePathTest.java','DocumentDirectoryTest.java','RuntimeReportFileTest.java')]
    inputs={path.relative_to(ROOT).as_posix():digest(path) for path in {*sdl,*tests,*[path for sources in source_sets.values() for path in sources],Path(__file__),ROOT/'scripts/android_source_sets.py',ROOT/'android/app/src/main/AndroidManifest.xml'}}
    identity=hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest()[:16];directory=ROOT/'build/native-android-ui'/identity;directory.mkdir(parents=True,exist_ok=True)
    flags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS;records={}
    log=ROOT/'logs/native-android-ui.log'
    with log.open('wb') as output:
        def run(command,timeout=90):
            command=list(map(str,command));output.write(('RUN '+subprocess.list2cmdline(command)+'\n').encode());output.flush();subprocess.run(command,cwd=ROOT,creationflags=flags,stdout=output,stderr=subprocess.STDOUT,timeout=timeout,check=True)
        for backend,own in source_sets.items():
            print('Compiling '+backend+' Android presentation; one hidden low-priority job, no app starts.',flush=True)
            classes=directory/backend;classes.mkdir(exist_ok=True)
            run([jdk/'bin/javac.exe','-J-XX:ActiveProcessorCount=1','-J-XX:+UseSerialGC','-J-Xmx256m','-encoding','UTF-8','-source','8','-target','8','-parameters','-classpath',api,'-d',classes,*own,*sdl])
            compiled=sorted(path.relative_to(classes).as_posix() for path in classes.rglob('*.class'))
            if backend=='native':
                forbidden={name[:-5] for name in LEGACY_ONLY}-{'PortUi'}
                for path in classes.rglob('*.class'):
                    if any(name.encode() in path.read_bytes() for name in forbidden):raise ValueError('Native class depends on an AOT widget/file-dialog adapter: '+str(path))
            records[backend]=dict(sources=[path.relative_to(ROOT).as_posix() for path in own],classes=compiled,classes_directory=str(classes),classes_sha256={name:digest(classes/name) for name in compiled})
        native=directory/'native';jar=directory/'native-ui.jar';dex=directory/'dex';dex.mkdir(exist_ok=True)
        run([jdk/'bin/jar.exe','--create','--file',jar,'-C',native,'.'])
        run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-XX:+UseSerialGC','-Xmx256m','-cp',bt/'lib/d8.jar','com.android.tools.r8.D8','--min-api','26','--lib',api,'--output',dex,jar])
        data=(dex/'classes.dex').read_bytes()
        for name in ('NativeWidgets','WidgetSession','WidgetControls','WidgetIcon','FileChooser'):
            if ('Lorg/wa2/port/'+name+';').encode() in data:raise ValueError('Native DEX contains a legacy adapter: '+name)
        test_classes=directory/'test-classes';test_classes.mkdir(exist_ok=True)
        classpath=os.pathsep.join(map(str,(native,test_classes)))
        run([jdk/'bin/javac.exe','-J-XX:ActiveProcessorCount=1','-J-Xmx128m','-encoding','UTF-8','-cp',native,'-d',test_classes,*tests])
        reports={}
        for test in tests:
            path=directory/(test.stem+'.json');run([jdk/'bin/java.exe','-XX:ActiveProcessorCount=1','-XX:+UseSerialGC','-Xmx128m','-cp',classpath,'org.wa2.port.'+test.stem,path]);reports[test.stem]=json.loads(path.read_text())
            if not reports[test.stem]['passed']:raise ValueError('Native UI pure state test failed: '+test.stem)
        saves=reports['LegacySavePathTest']
        if saves['cases']<406 or saves['guards']<17 or saves['source_files_opened'] or saves['android_device_verified']:raise ValueError('Incomplete read-only original-save path checks')
        storage=reports['GameStorageTest']
        if storage['cases']<1020 or storage['guards']<24 or not storage['fault_injected_publication'] or not storage['interrupted_write_recovery'] or storage['source_files_opened'] or storage['android_device_verified']:raise ValueError('Incomplete selected-directory publication checks')
        profile=reports['LaunchProfileTest']
        compatible=reports['CompatibleStoragePathTest']
        location=reports['ResourceLocationTest']
        if not location['folder_name_unrestricted'] or not location['opaque_document_ids_allowed'] or not location['arbitrary_directory_allowed']:raise ValueError('Folder selection still requires a fixed name/provider')
        directory=reports['DocumentDirectoryTest']
        report_choice=reports['RuntimeReportFileTest']
        if report_choice['cases']<6 or not report_choice['newest_private_report_selected'] or not report_choice['complete_preferred_on_tie']:raise ValueError('Incomplete private runtime report selection checks')
        if directory['lookups']!=400 or directory['same_directory_queries_for_400_lookups']!=1 or not all(directory[key] for key in ('operation_scoped_snapshot','next_operation_sees_external_edits','publication_index_updates','ambiguity_and_size_guards')):raise ValueError('Incomplete operation-scoped document directory checks')
        if compatible['cases']<852 or compatible['guards']<22 or compatible['original_files_opened'] or compatible['android_device_verified']:raise ValueError('Incomplete shared-AOT selected-directory path checks')
        if profile['cases']<17 or profile['guards']<34 or not all(profile[key] for key in ('directory_isolation','unchanged_writes_suppressed','failed_write_retry','inflight_edit_not_lost')) or profile['source_files_opened'] or profile['android_device_verified']:raise ValueError('Incomplete external launcher-configuration checks')
        if profile['language_roundtrips']!=64 or profile['legacy_migrations']!=8 or not all(profile[key] for key in ('language_arguments_exact','language_edits_preserve_flags','automatic_matched_pairs','legacy_mixed_pair_corrected','upgrade_write_retry')):raise ValueError('Incomplete launcher language/migration checks')
        if profile['v2_migrations']!=96 or profile['v3_migrations']!=288 or profile['external_subtitle_roundtrips']!=192 or not profile['external_subtitle_write_retry']:raise ValueError('Incomplete external subtitle configuration checks')
        if profile['v4_migrations']!=96 or not profile['older_profiles_default_chinese'] or not profile['explicit_language_retained_after_upgrade']:raise ValueError('Incomplete Chinese-default upgrade checks')
        if profile['v5_migrations']!=96 or not all(profile[key] for key in ('v5_language_preserved','menu_visibility_persisted','menu_preference_write_retry','menu_edits_preserve_game_args')):raise ValueError('Incomplete menu-visibility preference checks')
        text=reports['LauncherTextTest']
        if text['languages']!=28 or text['translated_messages']!=1176 or text['formatted_message_cases']!=252 or text['rtl_languages']!=4 or text['locale_resolution_cases']!=15 or not all(text[key] for key in ('default_chinese','process_locale_unchanged','game_profiles_unchanged')):raise ValueError('Incomplete launcher language catalogue checks')
    for name,value in inputs.items():
        if digest(ROOT/name)!=value:raise ValueError('Android UI input changed during verification: '+name)
    report=dict(schema='wa2.native-android-ui.v1',source_sha256=inputs,source_sets=records,api_jar_sha256=digest(api),javac_sha256=digest(jdk/'bin/javac.exe'),d8_sha256=digest(bt/'lib/d8.jar'),
        native_dex_file=str(dex/'classes.dex'),native_dex_sha256=digest(dex/'classes.dex'),native_dex_bytes=len(data),native_ui_jar_file=str(jar),native_ui_jar_sha256=digest(jar),native_legacy_widget_classes=False,
        pure_state_tests=reports,retained_original_widget_fixture_checked=False,native_jni_implementation_link_checked=False,
        android_device_verified=False,apk_created=False,game_launched=False,screen_access=False,passed=True)
    (ROOT/'logs/native-android-ui.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS Android source-set compilation, native-only DEX and pure gesture/path checks; not an APK/device/JNI-runtime test.')

if __name__=='__main__':main()
