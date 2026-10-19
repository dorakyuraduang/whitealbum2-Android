"""Install the pinned official Android build tools inside this workspace."""
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

ROOT=Path(__file__).resolve().parents[1]
BASE='https://dl.google.com/android/repository/'


def digest(path,algorithm='sha256'):
    result=hashlib.new(algorithm)
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):result.update(chunk)
    return result.hexdigest()


def download(url,path,size=None,checksum=None,algorithm='sha256'):
    path.parent.mkdir(parents=True,exist_ok=True)
    if path.is_file() and (size is None or path.stat().st_size==size) and (checksum is None or digest(path,algorithm)==checksum):return
    partial=path.with_suffix(path.suffix+'.part');received=0;last=time.monotonic()
    with urllib.request.urlopen(url,timeout=60) as response,partial.open('wb') as out:
        while chunk:=response.read(512*1024):
            out.write(chunk);received+=len(chunk)
            if received>(size if size is not None else 256*1024*1024):raise ValueError('Download exceeds expected size')
            if time.monotonic()-last>10:
                print(path.name,round(received/2**20,1),'MiB',flush=True);last=time.monotonic()
    if size is not None and received!=size:raise ValueError('Download length mismatch')
    if checksum is not None and digest(partial,algorithm)!=checksum:raise ValueError('Download checksum mismatch')
    partial.replace(path)


def extract(archive,destination,strip_root=True):
    destination.mkdir(parents=True,exist_ok=True);absolute=destination.resolve()
    with zipfile.ZipFile(archive) as zipped:
        roots={PurePosixPath(item.filename).parts[0] for item in zipped.infolist() if PurePosixPath(item.filename).parts}
        if strip_root and len(roots)!=1:raise ValueError('Expected one package directory')
        for item in zipped.infolist():
            parts=PurePosixPath(item.filename).parts
            if not parts or parts[0] in ('/','') or '..' in parts:raise ValueError('Unsafe archive entry')
            relative=parts[1:] if strip_root else parts
            if not relative:continue
            output=destination.joinpath(*relative)
            if not output.resolve().is_relative_to(absolute):raise ValueError('Archive entry escapes package')
            if item.is_dir():output.mkdir(parents=True,exist_ok=True);continue
            output.parent.mkdir(parents=True,exist_ok=True)
            with zipped.open(item) as source,output.open('wb') as target:shutil.copyfileobj(source,target,512*1024)


def main():
    if os.name=='nt':ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(),0x4000)
    repository=ROOT/'tools/android-api-35/repository2-1.xml'
    tree=ET.fromstring(repository.read_bytes());sdk=ROOT/'tools/android-sdk';cache=ROOT/'tools/downloads';records=[]
    specs=[('ndk;27.0.12077973',sdk/'ndk/27.0.12077973','build/cmake/android.toolchain.cmake'),
        ('platforms;android-35',sdk/'platforms/android-35','android.jar'),
        ('build-tools;34.0.0',sdk/'build-tools/34.0.0','aapt2.exe'),
        ('cmake;3.22.1',sdk/'cmake/3.22.1','bin/cmake.exe')]
    for name,destination,probe in specs:
        candidates=[]
        for package in tree.iter():
            if not package.tag.endswith('remotePackage') or package.attrib.get('path')!=name:continue
            for entry in package.findall('.//archives/archive'):
                if entry.findtext('host-os') not in (None,'windows'):continue
                complete=entry.find('complete')
                if name=='platforms;android-35' and not complete.findtext('url').startswith('platform-35_r'):continue
                candidates.append(complete)
        if len(candidates)!=1:raise ValueError('Ambiguous pinned Android archive: '+name)
        complete=candidates[0];url=urllib.parse.urljoin(BASE,complete.findtext('url'))
        parsed=urllib.parse.urlparse(url)
        if parsed.scheme!='https' or parsed.netloc!='dl.google.com' or not parsed.path.startswith('/android/repository/'):
            raise ValueError('Unexpected Android package source')
        size=int(complete.findtext('size'));checksum=complete.findtext('checksum').lower().strip()
        algorithm=complete.find('checksum').attrib.get('type','sha1')
        archive=cache/Path(parsed.path).name;stamp=destination/'.wa2-package.json'
        if stamp.is_file() and (destination/probe).is_file() and json.loads(stamp.read_text()).get('repository_checksum')==checksum:
            records.append(json.loads(stamp.read_text()));print('Present:',name,flush=True);continue
        print('Downloading:',name,flush=True);download(url,archive,size,checksum,algorithm)
        print('Extracting:',name,flush=True);extract(archive,destination,strip_root=not name.startswith('cmake;'))
        if not (destination/probe).is_file():raise ValueError('Missing Android package file: '+probe)
        record=dict(package=name,path=str(destination),official_url=url,archive_bytes=size,checksum_algorithm=algorithm,
            repository_checksum=checksum,archive_sha256=digest(archive),installed=True)
        stamp.write_text(json.dumps(record,indent=2)+'\n');records.append(record)
    gradle=ROOT/'tools/gradle-8.9';stamp=gradle/'.wa2-package.json'
    if stamp.is_file() and (gradle/'lib/gradle-launcher-8.9.jar').is_file():records.append(json.loads(stamp.read_text()))
    else:
        base='https://services.gradle.org/distributions/gradle-8.9-bin.zip'
        with urllib.request.urlopen(base+'.sha256',timeout=30) as response:checksum=response.read(1024).decode().strip()
        if len(checksum)!=64 or any(c not in '0123456789abcdef' for c in checksum):raise ValueError('Invalid Gradle checksum')
        archive=cache/'gradle-8.9-bin.zip';print('Downloading: Gradle 8.9',flush=True);download(base,archive,checksum=checksum)
        extract(archive,gradle);record=dict(package='gradle-8.9',path=str(gradle),official_url=base,archive_bytes=archive.stat().st_size,
            checksum_algorithm='sha256',repository_checksum=checksum,archive_sha256=digest(archive),installed=True)
        stamp.write_text(json.dumps(record,indent=2)+'\n');records.append(record)
    (ROOT/'logs/android-toolchain-install.json').write_text(json.dumps(dict(schema='wa2.android-toolchain.v1',packages=records,
        screen_access=False,emulator_installed=False,apk_created=False),indent=2)+'\n')
    print('Official Android SDK, NDK and Gradle prepared inside the workspace; no APK built.',flush=True)


if __name__=='__main__':main()
