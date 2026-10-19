"""Fetch only Google's API-35 compile stubs, not Studio, an emulator or an NDK."""
import hashlib
import json
from pathlib import Path
import shutil
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BASE = 'https://dl.google.com/android/repository/'


def digest(path, algorithm='sha256'):
    result = hashlib.new(algorithm)
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            result.update(block)
    return result.hexdigest()


def main():
    destination = ROOT/'tools/android-api-35'
    destination.mkdir(parents=True, exist_ok=True)
    metadata = destination/'repository2-1.xml'
    if not metadata.exists():
        with urllib.request.urlopen(BASE+'repository2-1.xml', timeout=30) as response:
            payload = response.read(16*1024*1024)
        metadata.write_bytes(payload)
    tree = ET.fromstring(metadata.read_bytes())
    package = next(p for p in tree.iter() if p.tag.endswith('remotePackage') and p.attrib.get('path')=='platforms;android-35')
    complete = package.find('.//archives/archive/complete')
    if complete is None:
        raise ValueError('Google repository has no complete API-35 archive')
    url = urllib.parse.urljoin(BASE, complete.findtext('url'))
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme!='https' or parsed.netloc!='dl.google.com' or not parsed.path.startswith('/android/repository/'):
        raise ValueError('Unexpected Android download origin')
    size = int(complete.findtext('size'))
    checksum = complete.findtext('checksum').strip().lower()
    algorithm = complete.find('checksum').attrib.get('type','sha1')
    if algorithm not in ('sha1','sha256'):
        raise ValueError('Unsupported Google repository checksum')
    archive = destination/'platform-35.zip'
    if not archive.exists() or archive.stat().st_size!=size or digest(archive,algorithm)!=checksum:
        partial = archive.with_suffix('.part')
        with urllib.request.urlopen(url, timeout=60) as response, partial.open('wb') as output:
            shutil.copyfileobj(response,output,1024*1024)
        if partial.stat().st_size!=size or digest(partial,algorithm)!=checksum:
            raise ValueError('Google API archive size or checksum mismatch')
        partial.replace(archive)
    with zipfile.ZipFile(archive) as zipped:
        candidates = [name for name in zipped.namelist() if len(Path(name).parts)==2 and Path(name).name=='android.jar']
        if len(candidates)!=1:
            raise ValueError('Expected exactly one platform android.jar')
        with zipped.open(candidates[0]) as source, (destination/'android.jar').open('wb') as output:
            shutil.copyfileobj(source,output,1024*1024)
    proof = dict(schema='wa2.android-api.v1', official_repository=BASE+'repository2-1.xml', archive_url=url,
                 archive_bytes=size, repository_checksum_algorithm=algorithm, repository_checksum=checksum,
                 archive_sha256=digest(archive), android_jar_sha256=digest(destination/'android.jar'),
                 complete_sdk_installed=False, ndk_installed=False, apk_created=False)
    (ROOT/'logs/android-api35.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Verified Google API-35 compile stubs; no SDK emulator or game was started.')


if __name__=='__main__':
    main()
