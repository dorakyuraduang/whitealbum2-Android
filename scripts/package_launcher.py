"""Build the standalone Android launcher with only its Java code and UI resources."""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'android/launcher/src/main'
ANDROID = '{http://schemas.android.com/apk/res/android}'
PACKAGE = 'org.wa2.launcher'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_content(apk):
    expected = {'AndroidManifest.xml', 'resources.arsc', 'classes.dex',
                'res/drawable/launcher_icon.xml', 'META-INF/WA2-LICENSE.txt'}
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        payload = {name for name in names if name != 'META-INF/MANIFEST.MF'
                   and not re.fullmatch(r'META-INF/[^/]+\.(SF|RSA|EC|DSA)', name)}
        if payload != expected or len(names) != len(set(names)):
            raise ValueError(f'Unexpected launcher APK entries: {sorted(payload ^ expected)}')
        if archive.getinfo('resources.arsc').compress_type != zipfile.ZIP_STORED:
            raise ValueError('Android requires a stored resource table')
        if archive.testzip() is not None:
            raise ValueError('Launcher APK contains an invalid ZIP entry')
        return sorted(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=ROOT / 'tools/android-sdk')
    parser.add_argument('--jdk', type=Path, default=ROOT / 'tools/source-analysis/jdk-21.0.12.1+1')
    parser.add_argument('--keystore', type=Path)
    parser.add_argument('--alias', default='wa2-dev')
    args = parser.parse_args()

    key = args.keystore or ROOT / 'private/android-signing/wa2-native-dev.p12'
    if args.keystore is None and not key.is_file():
        key = ROOT / 'build/android-signing/wa2-native-dev.p12'
    if not key.is_file():
        raise ValueError('No existing signing key. Supply --keystore and the signing password.')

    sdk = args.sdk.resolve()
    jdk = args.jdk.resolve()
    api = sdk / 'platforms/android-35/android.jar'
    tools = sdk / 'build-tools/34.0.0'
    flags = 0
    executable_suffix = ''
    if os.name == 'nt':
        executable_suffix = '.exe'
        flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint]
        if not kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000):
            raise ctypes.WinError(ctypes.get_last_error())

    javac = jdk / 'bin' / ('javac' + executable_suffix)
    jar = jdk / 'bin' / ('jar' + executable_suffix)
    java = [jdk / 'bin' / ('java' + executable_suffix), '-XX:ActiveProcessorCount=1',
            '-XX:+UseSerialGC', '-Xmx256m']
    aapt = tools / ('aapt2' + executable_suffix)
    zipalign = tools / ('zipalign' + executable_suffix)
    signer = [*java, '-jar', tools / 'lib/apksigner.jar']

    manifest = SOURCE / 'AndroidManifest.xml'
    package = ET.parse(manifest).getroot()
    version = package.attrib[ANDROID + 'versionName']
    version_code = package.attrib[ANDROID + 'versionCode']
    min_sdk = package.find('uses-sdk').attrib[ANDROID + 'minSdkVersion']
    if package.get('package') != PACKAGE or not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Unexpected standalone launcher identity')

    build = (ROOT / 'build/standalone-launcher').resolve()
    output = (ROOT / 'out/android').resolve()
    logs = (ROOT / 'logs').resolve()
    for path in (build, output, logs):
        if not path.is_relative_to(ROOT):
            raise ValueError('Launcher output must stay in the workspace')
        path.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    if args.keystore is None:
        env.setdefault('WA2_LAUNCHER_STORE_PASSWORD', env.get('WA2_NATIVE_SIGNING_PASSWORD', 'android'))
    if 'WA2_LAUNCHER_STORE_PASSWORD' not in env:
        raise ValueError('Set WA2_LAUNCHER_STORE_PASSWORD for the requested keystore')
    env.setdefault('WA2_LAUNCHER_KEY_PASSWORD', env['WA2_LAUNCHER_STORE_PASSWORD'])
    with tempfile.TemporaryDirectory(dir=build) as path, (logs / 'standalone-launcher.log').open('w', encoding='utf-8') as log:
        staging = Path(path)
        env.update(TMP=str(staging), TEMP=str(staging))

        def run(command):
            command = list(map(str, command))
            result = subprocess.run(command, cwd=ROOT, env=env, creationflags=flags,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, encoding='utf-8', errors='replace', timeout=120)
            log.write(f'{Path(command[0]).name} {command[1]}\n{result.stdout}\n')
            log.flush()
            result.check_returncode()
            return result.stdout

        classes, sources, dex = (staging / name for name in ('classes', 'sources', 'dex'))
        for folder in (classes, sources, dex):
            folder.mkdir()
        resources = staging / 'compiled.zip'
        resource_apk = staging / 'resources.apk'
        run([aapt, 'compile', '--dir', SOURCE / 'res', '-o', resources])
        run([aapt, 'link', '--manifest', manifest, '-I', api, '--java', sources,
             '-o', resource_apk, resources])
        java_sources = sorted((SOURCE / 'java').rglob('*.java')) + sorted(sources.rglob('R.java'))
        run([javac, '-J-XX:ActiveProcessorCount=1', '-J-XX:+UseSerialGC', '-J-Xmx192m',
             '-J-Duser.language=en', '-encoding', 'UTF-8', '-source', '8', '-target', '8', '-parameters', '-classpath', api,
             '-d', classes, *java_sources])
        bytecode = staging / 'launcher.jar'
        run([jar, '--create', '--file', bytecode, '-C', classes, '.'])
        run([*java, '-cp', tools / 'lib/d8.jar', 'com.android.tools.r8.D8',
             '--min-api', min_sdk, '--lib', api, '--output', dex, bytecode])

        unsigned = staging / 'unsigned.apk'
        with zipfile.ZipFile(resource_apk) as resources_zip, zipfile.ZipFile(unsigned, 'w') as apk:
            for info in resources_zip.infolist():
                compression = zipfile.ZIP_STORED if info.filename == 'resources.arsc' else info.compress_type
                apk.writestr(info.filename, resources_zip.read(info), compress_type=compression)
            apk.write(dex / 'classes.dex', 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
            apk.write(ROOT / 'LICENSE', 'META-INF/WA2-LICENSE.txt', compress_type=zipfile.ZIP_DEFLATED)

        aligned = staging / 'aligned.apk'
        signed = staging / 'launcher.apk'
        run([zipalign, '-f', '4', unsigned, aligned])
        run([*signer, 'sign', '--ks', key.resolve(), '--ks-key-alias', args.alias,
             '--ks-pass', 'env:WA2_LAUNCHER_STORE_PASSWORD',
             '--key-pass', 'env:WA2_LAUNCHER_KEY_PASSWORD', '--out', signed, aligned])
        verification = run([*signer, 'verify', '--verbose', signed])
        if not re.search(r'Verified using v2 scheme[^\n]+: true', verification):
            raise ValueError('Launcher APK signature verification did not confirm v2 signing')
        run([zipalign, '-c', '4', signed])
        badging = run([aapt, 'dump', 'badging', signed])
        if f"package: name='{PACKAGE}' versionCode='{version_code}' versionName='{version}'" not in badging:
            raise ValueError('Launcher APK identity does not match its manifest')
        if f"launchable-activity: name='{PACKAGE}.LauncherActivity'" not in badging:
            raise ValueError('Launcher activity is unavailable')
        permissions = run([aapt, 'dump', 'permissions', signed])
        if any(line.startswith('uses-permission') for line in permissions.splitlines()):
            raise ValueError('The standalone launcher should use only selected-folder grants')
        entries = verify_content(signed)

        artifact = output / f'WA2-Launcher-{version}.apk'
        os.replace(signed, artifact)
        info = dict(package=PACKAGE, version=version, version_code=int(version_code),
                    file=str(artifact), sha256=sha256(artifact), bytes=artifact.stat().st_size,
                    entries=entries, signature_v2_verified=True, alignment_verified=True,
                    app_launched=False, passed=True)
        (logs / 'standalone-launcher.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(info, indent=2))


if __name__ == '__main__':
    main()
