"""Compile the Android host, directory access and HUD against official API stubs."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    java_home = ROOT/'tools/source-analysis/jdk-21.0.12.1+1'
    javac = java_home/'bin/javac.exe'
    api = ROOT/'tools/android-api-35/android.jar'
    official = json.loads((ROOT/'logs/android-api35.json').read_text())
    if digest(api)!=official['android_jar_sha256']:
        raise ValueError('Android API compile stubs changed')
    own = sorted((ROOT/'android/app/src/main/java').rglob('*.java'))
    sdl = sorted((ROOT/'tools/SDL-release-2.32.10/android-project/app/src/main/java').rglob('*.java'))
    directory = ROOT/'build/android-java'
    directory.mkdir(parents=True,exist_ok=True)
    command = [str(javac),'-J-XX:ActiveProcessorCount=1','-J-Xmx256m','-encoding','UTF-8',
               '-source','8','-target','8','-parameters','-classpath',str(api),'-d',str(directory),*map(str,own+sdl)]
    manifest=ROOT/'android/app/src/main/AndroidManifest.xml'
    location_test=ROOT/'tests/android/org/wa2/port/ResourceLocationTest.java'
    widget_test=ROOT/'tests/android/org/wa2/port/WidgetControlsTest.java'
    inputs = {str(p.relative_to(ROOT)):digest(p) for p in [*own,Path(__file__),manifest,location_test,widget_test]}
    fixture=ROOT/'build/shared-aot-x64/widget-unicode-fixture.json'
    widget_args=[]
    if fixture.exists():
        contents=json.loads(fixture.read_bytes());lines=[]
        for dialog in contents['dialogs']:
            for control in dialog['transport']['controls']:
                lines.append('\t'.join(map(str,[control['token'],control['id'],control['type'],control['style'],*control['bounds']])))
            lines.append('')
        widget_fixture=directory/'widget-controls.tsv';widget_fixture.write_text('\n'.join(lines)+'\n',encoding='utf-8');widget_args=[str(widget_fixture)]
        inputs[fixture.relative_to(ROOT).as_posix()]=digest(fixture)
    flags = (subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW) if os.name=='nt' else 0
    with (ROOT/'logs/android-java-compile.log').open('wb') as console:
        subprocess.run(command,cwd=ROOT,stdout=console,stderr=subprocess.STDOUT,
                       creationflags=flags,timeout=60,check=True)
        subprocess.run([str(javac),'-J-XX:ActiveProcessorCount=1','-J-Xmx128m','-encoding','UTF-8','-cp',str(directory),'-d',str(directory),str(location_test),str(widget_test)],
            cwd=ROOT,stdout=console,stderr=subprocess.STDOUT,creationflags=flags,timeout=30,check=True)
        location_report=directory/'resource-location.json'
        subprocess.run([str(java_home/'bin/java.exe'),'-XX:ActiveProcessorCount=1','-Xmx128m','-cp',str(directory),'org.wa2.port.ResourceLocationTest',str(location_report)],
            cwd=ROOT,stdout=console,stderr=subprocess.STDOUT,creationflags=flags,timeout=15,check=True)
        widget_report=directory/'widget-ownership.json'
        subprocess.run([str(java_home/'bin/java.exe'),'-XX:ActiveProcessorCount=1','-Xmx128m','-cp',str(directory),'org.wa2.port.WidgetControlsTest',str(widget_report),*widget_args],
            cwd=ROOT,stdout=console,stderr=subprocess.STDOUT,creationflags=flags,timeout=15,check=True)
    ns='{http://schemas.android.com/apk/res/android}'
    activities=ET.parse(manifest).getroot().find('application').findall('activity')
    launchers=[activity for activity in activities if any(category.get(ns+'name')=='android.intent.category.LAUNCHER' for category in activity.findall('intent-filter/category'))]
    if len(launchers)!=1 or launchers[0].get(ns+'name')!='.LauncherActivity':raise ValueError('The launcher must precede the SDL game activity')
    game=next(activity for activity in activities if activity.get(ns+'name')=='.Wa2Activity')
    if game.get(ns+'exported')!='false':raise ValueError('The game activity must not bypass resource configuration')
    for name,value in inputs.items():
        if digest(ROOT/name)!=value:
            raise ValueError('Android Java sources changed during compilation')
    classes = {p.relative_to(directory).as_posix():digest(p) for p in sorted((directory/'org/wa2/port').glob('*.class'))}
    if any('org/wa2/port/'+name+'.class' not in classes for name in ('Wa2Activity','LauncherActivity','LaunchSettings')):
        raise ValueError('Android launcher/game settings did not compile')
    proof = dict(schema='wa2.android-java.v1',source_sha256=inputs,classes=classes,command=command,
                 api35_report_sha256=digest(ROOT/'logs/android-api35.json'),javac_sha256=digest(javac),
                 resource_location=json.loads(location_report.read_text()),launcher_before_game=True,fps_setting_compiled=True,
                 widget_ownership=json.loads(widget_report.read_text()),
                 game_launched=False,screen_access=False,android_device_verified=False,apk_created=False,passed=True)
    (ROOT/'logs/android-java-verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('PASS: Android activity, external PAK directory and FPS HUD compile against official Android API-35 and the existing SDL Java layer.')


if __name__=='__main__':
    main()
