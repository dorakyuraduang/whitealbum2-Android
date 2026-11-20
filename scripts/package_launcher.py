"""Build the Android launcher alone or update it in a verified shared-AOT APK."""

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

from android_source_sets import java_sources as game_java_sources


ROOT = Path(__file__).resolve().parents[1]
STANDALONE_SOURCE = ROOT / 'android/launcher/src/main'
INTEGRATED_SOURCE = ROOT / 'android/app/src/main'
ANDROID = '{http://schemas.android.com/apk/res/android}'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_content(apk, expected, preserved):
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
        for name, digest in preserved.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != digest:
                raise ValueError(f'Integrated APK changed an existing runtime entry: {name}')
        return sorted(names)


def audited_base():
    receipt = json.loads((ROOT / 'logs/shared-host-package.json').read_text(encoding='utf-8'))
    audit = json.loads((ROOT / 'logs/shared-package-verification.json').read_text(encoding='utf-8'))
    artifact = receipt['artifacts']['android']
    base = Path(artifact['file']).resolve()
    version = receipt['version'].removesuffix('-shared-aot')
    if not base.is_relative_to((ROOT / 'out/android').resolve()) or not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Expected a locally verified, versioned shared-AOT APK')
    if not (receipt['passed'] and audit['passed'] and audit['android_signature_v2_verified']
            and not receipt['original_resources_in_apk']
            and sha256(base) == artifact['sha256'] == audit['artifacts']['android']['sha256']):
        raise ValueError('Original shared-AOT APK does not match its verification records')
    libraries = {'libwa2_android.so', 'libSDL2.so', 'libc++_shared.so'}
    expected = {f'lib/arm64-v8a/{name}': receipt['libraries'][name]['stripped_sha256']
                for name in libraries}
    return base, version + '-launcher.1', int(receipt['version_code']) + 1, expected


def copy_runtime(base, destination, libraries):
    preserved = {}
    with zipfile.ZipFile(base) as archive:
        for info in archive.infolist():
            name = info.filename
            if name not in libraries and not name.startswith('assets/licenses/'):
                if (name in {'AndroidManifest.xml', 'classes.dex', 'resources.arsc'}
                        or name.startswith(('META-INF/', 'res/'))):
                    continue
                raise ValueError(f'Unexpected data in base APK: {name}')
            content = archive.read(info)
            digest = hashlib.sha256(content).hexdigest()
            if name in libraries and digest != libraries[name]:
                raise ValueError(f'Base APK native library differs from the verified build: {name}')
            destination.writestr(info, content)
            preserved[name] = digest
    if not libraries.keys() <= preserved.keys():
        raise ValueError('Base APK is missing the verified shared runtime')
    return preserved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--integrated', action='store_true', help='Update the launcher in the audited regular Android APK')
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

    source = INTEGRATED_SOURCE if args.integrated else STANDALONE_SOURCE
    manifest = source / 'AndroidManifest.xml'
    package = ET.parse(manifest).getroot()
    base = None
    if args.integrated:
        base, version, version_code, libraries = audited_base()
        app_id = 'org.wa2.port'
        package.attrib.update({'package': app_id, ANDROID + 'versionName': version,
                               ANDROID + 'versionCode': str(version_code)})
        uses_sdk = ET.Element('uses-sdk', {ANDROID + 'minSdkVersion': '26', ANDROID + 'targetSdkVersion': '35'})
        package.insert(0, uses_sdk)
        package.find('application').attrib.update({ANDROID + 'extractNativeLibs': 'true',
                                                   ANDROID + 'debuggable': 'false'})
    else:
        app_id = 'org.wa2.launcher'
        version = package.attrib[ANDROID + 'versionName']
        version_code = int(package.attrib[ANDROID + 'versionCode'])
        if package.get('package') != app_id or not re.fullmatch(r'\d+\.\d+\.\d+', version):
            raise ValueError('Unexpected standalone launcher identity')

    min_sdk = package.find('uses-sdk').attrib[ANDROID + 'minSdkVersion']
    label = 'integrated-launcher' if args.integrated else 'standalone-launcher'
    build = (ROOT / 'build' / label).resolve()
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
    with tempfile.TemporaryDirectory(dir=build) as path, (logs / f'{label}.log').open('w', encoding='utf-8') as log:
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
        if args.integrated:
            ET.register_namespace('android', ANDROID[1:-1])
            manifest = staging / 'AndroidManifest.xml'
            ET.ElementTree(package).write(manifest, encoding='utf-8', xml_declaration=True)
            base_signing = run([*signer, 'verify', '--print-certs', base])
        run([aapt, 'compile', '--dir', source / 'res', '-o', resources])
        run([aapt, 'link', '--manifest', manifest, '-I', api, '--java', sources,
             '-o', resource_apk, resources])
        if args.integrated:
            sdl = ROOT / 'tools/SDL-release-2.32.10/android-project/app/src/main/java'
            java_sources = game_java_sources('shared-aot') + sorted(sdl.rglob('*.java'))
        else:
            java_sources = sorted((source / 'java').rglob('*.java'))
            java_sources.append(INTEGRATED_SOURCE / 'java/org/wa2/launcher/GameFolderSelection.java')
        java_sources += sorted(sources.rglob('R.java'))
        run([javac, '-J-XX:ActiveProcessorCount=1', '-J-XX:+UseSerialGC', '-J-Xmx192m',
             '-J-Duser.language=en', '-encoding', 'UTF-8', '-source', '8', '-target', '8', '-parameters', '-classpath', api,
             '-d', classes, *java_sources])
        bytecode = staging / 'launcher.jar'
        run([jar, '--create', '--file', bytecode, '-C', classes, '.'])
        run([*java, '-cp', tools / 'lib/d8.jar', 'com.android.tools.r8.D8',
             '--min-api', min_sdk, '--lib', api, '--output', dex, bytecode])

        unsigned = staging / 'unsigned.apk'
        preserved = {}
        with zipfile.ZipFile(resource_apk) as resources_zip, zipfile.ZipFile(unsigned, 'w') as apk:
            for info in resources_zip.infolist():
                compression = zipfile.ZIP_STORED if info.filename == 'resources.arsc' else info.compress_type
                apk.writestr(info.filename, resources_zip.read(info), compress_type=compression)
            apk.write(dex / 'classes.dex', 'classes.dex', compress_type=zipfile.ZIP_DEFLATED)
            expected = set(resources_zip.namelist()) | {'classes.dex'}
            if args.integrated:
                preserved = copy_runtime(base, apk, libraries)
                expected.update(preserved)
            else:
                apk.write(ROOT / 'LICENSE', 'META-INF/WA2-LICENSE.txt', compress_type=zipfile.ZIP_DEFLATED)
                expected = {'AndroidManifest.xml', 'resources.arsc', 'classes.dex',
                            'res/drawable/launcher_icon.xml', 'META-INF/WA2-LICENSE.txt'}

        aligned = staging / 'aligned.apk'
        signed = staging / 'launcher.apk'
        run([zipalign, '-f', '4', unsigned, aligned])
        run([*signer, 'sign', '--ks', key.resolve(), '--ks-key-alias', args.alias,
             '--ks-pass', 'env:WA2_LAUNCHER_STORE_PASSWORD',
             '--key-pass', 'env:WA2_LAUNCHER_KEY_PASSWORD', '--out', signed, aligned])
        verification = run([*signer, 'verify', '--verbose', '--print-certs', signed])
        if not re.search(r'Verified using v2 scheme[^\n]+: true', verification):
            raise ValueError('Launcher APK signature verification did not confirm v2 signing')
        if args.integrated:
            certificate = r'Signer #1 certificate SHA-256 digest: ([a-fA-F0-9]{64})'
            original = re.findall(certificate, base_signing)
            if len(original) != 1 or original != re.findall(certificate, verification):
                raise ValueError('Integrated APK must retain the regular app signing identity')
        run([zipalign, '-c', '4', signed])
        badging = run([aapt, 'dump', 'badging', signed])
        if f"package: name='{app_id}' versionCode='{version_code}' versionName='{version}'" not in badging:
            raise ValueError('Launcher APK identity does not match its manifest')
        if f"launchable-activity: name='{app_id}.LauncherActivity'" not in badging:
            raise ValueError('Launcher activity is unavailable')
        permissions = run([aapt, 'dump', 'permissions', signed])
        if any(line.startswith('uses-permission') for line in permissions.splitlines()):
            raise ValueError('The standalone launcher should use only selected-folder grants')
        entries = verify_content(signed, expected, preserved)

        prefix = 'WA2-arm64-' if args.integrated else 'WA2-Launcher-'
        artifact = output / f'{prefix}{version}.apk'
        os.replace(signed, artifact)
        info = dict(package=app_id, version=version, version_code=version_code,
                    file=str(artifact), sha256=sha256(artifact), bytes=artifact.stat().st_size,
                    entries=entries, signature_v2_verified=True, alignment_verified=True,
                    base_apk_sha256=sha256(base) if args.integrated else None,
                    preserved_entries=preserved,
                    app_launched=False, passed=True)
        (logs / f'{label}.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(info, indent=2))


if __name__ == '__main__':
    main()
