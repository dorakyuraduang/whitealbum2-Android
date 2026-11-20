"""Install one verified APK in place, preserve data, and never start the app."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import subprocess
from prepare_special_contents import ROOT, digest, write_json


def install(receipt_path,serial=None):
    receipt=json.loads(Path(receipt_path).read_text())
    apk=Path(receipt['file']).resolve()
    if (not receipt['passed'] or not receipt['signature_v2_verified'] or not receipt['alignment_verified']
            or receipt['package']!='org.wa2.port' or digest(apk)!=receipt['sha256']
            or not apk.is_relative_to((ROOT/'out/android').resolve())):
        raise ValueError('APK installation requires a verified local package receipt')
    adb=ROOT/'tools/android-sdk/platform-tools/adb.exe'
    flags=getattr(subprocess,'CREATE_NO_WINDOW',0)|getattr(subprocess,'BELOW_NORMAL_PRIORITY_CLASS',0)
    def call(args,timeout=30):
        return subprocess.run([str(adb),*args],cwd=ROOT,creationflags=flags,stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,check=True,timeout=timeout).stdout.decode('utf-8',errors='replace')
    state=dict(schema='wa2.android-installation.v1',package=receipt['package'],version=receipt['version'],
        version_code=receipt['version_code'],apk_sha256=receipt['sha256'],game_launched=False,screen_access=False)
    destination=ROOT/'logs/special-android-installed.json'
    try:
        devices=call(['devices','-l'])
        connected=[line.split()[0] for line in devices.splitlines() if re.match(r'^\S+\s+device(?:\s|$)',line)]
        if serial is None:
            if len(connected)!=1:
                state.update(installation_pending=True,reason='Exactly one authorized online Android device is required',passed=False)
                write_json(destination,state);return state
            serial=connected[0]
        if serial not in connected:
            state.update(installation_pending=True,reason='Authorized Android device is not online',passed=False)
            write_json(destination,state);return state
        def device(args,timeout=30):return call(['-s',serial,*args],timeout)
        def package_state():
            text=device(['shell','dumpsys','package',receipt['package']])
            result={}
            for key,pattern in {'uid':r'\buserId=(\d+)', 'version_code':r'\bversionCode=(\d+)',
                                'version':r'\bversionName=([^\r\n]+)', 'first_install':r'\bfirstInstallTime=([^\r\n]+)'}.items():
                match=re.search(pattern,text)
                if match:result[key]=match[1].strip()
            if result.get('version_code'):
                # Android releases differ between userId/appId in dumpsys.
                # Package-manager UID output is stable and package-exact.
                uid_text=device(['shell','cmd','package','list','packages','-U',receipt['package']])
                uid=re.search(r'^package:'+re.escape(receipt['package'])+r'\s+uid:(\d+)\s*$',uid_text,re.M)
                if not uid:raise ValueError('Exact installed application UID is unavailable')
                result['uid']=uid[1]
            return result
        before=package_state()
        if before.get('version_code') and int(before['version_code'])>receipt['version_code']:
            raise ValueError('Installed package is newer; refusing an automatic downgrade')
        state.update(before=before,installation_pending=True,stage='before-in-place-install')
        write_json(destination,state)
        result=device(['install','-r',str(apk)],180)
        if not re.search(r'^Success\s*$',result,re.M):raise ValueError('ADB did not confirm in-place installation')
        after=package_state()
        if after.get('version')!=receipt['version'] or int(after.get('version_code',-1))!=receipt['version_code']:
            raise ValueError('Installed version differs from the verified APK')
        paths=device(['shell','pm','path',receipt['package']]).splitlines()
        base=[line.removeprefix('package:') for line in paths if line.startswith('package:') and line.endswith('/base.apk')]
        if len(base)!=1 or not base[0].startswith('/data/app/'):
            raise ValueError('Installed base package path is unavailable')
        remote=device(['shell','sha256sum',base[0]]).split()[0].lower()
        if remote!=receipt['sha256']:raise ValueError('Installed APK hash does not match the verified package')
        if before and (before.get('uid')!=after.get('uid') or
                (before.get('first_install') and before.get('first_install')!=after.get('first_install'))):
            raise ValueError('In-place installation identity was not preserved')
        state.update(installed_in_place=True,application_data_preserved=True,
            application_uid_preserved=before.get('uid')==after.get('uid') if before else None,
            first_install_time_preserved=before.get('first_install')==after.get('first_install') if before.get('first_install') else None,
            installed_apk_hash_matches=True,installation_pending=False,stage='verified-installed-package',passed=True)
    except (OSError,subprocess.SubprocessError,ValueError) as error:
        state.update(installation_pending=True,reason=str(error),passed=False)
    write_json(destination,state)
    return state


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('receipt',type=Path);parser.add_argument('--serial')
    args=parser.parse_args();print(json.dumps(install(args.receipt,args.serial),indent=2),flush=True)
