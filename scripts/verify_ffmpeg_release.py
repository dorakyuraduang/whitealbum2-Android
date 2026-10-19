"""Verify the official FFmpeg detached RSA/OpenPGP release signature."""
import base64
import hashlib
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding,utils,rsa

FINGERPRINT='FCF986EA15E6E293A5644F10B4322F04D67658D8'


def packets(armor):
    lines=armor.decode('ascii').splitlines();inside=False;encoded=[]
    for line in lines:
        if line.startswith('-----BEGIN '):inside=True;continue
        if line.startswith('-----END '):break
        if inside and line and ':' not in line and not line.startswith('='):encoded.append(line)
    data=base64.b64decode(''.join(encoded),validate=True);at=0
    while at<len(data):
        tag=data[at];at+=1
        if not tag&128:raise ValueError('Invalid OpenPGP packet header')
        if tag&64:
            kind=tag&63;n=data[at];at+=1
            if n<192:size=n
            elif n<224:size=((n-192)<<8)+data[at]+192;at+=1
            elif n==255:size=int.from_bytes(data[at:at+4],'big');at+=4
            else:raise ValueError('Partial OpenPGP packet is unsupported')
        else:
            kind=(tag>>2)&15;width=(1,2,4,0)[tag&3]
            if not width:raise ValueError('Indefinite OpenPGP packet is unsupported')
            size=int.from_bytes(data[at:at+width],'big');at+=width
        if at+size>len(data):raise ValueError('Truncated OpenPGP packet')
        yield kind,data[at:at+size];at+=size


def mpi(data,at):
    bits=int.from_bytes(data[at:at+2],'big');size=(bits+7)//8;at+=2
    if at+size>len(data):raise ValueError('Truncated OpenPGP integer')
    value=int.from_bytes(data[at:at+size],'big')
    if value.bit_length()!=bits:raise ValueError('Invalid OpenPGP integer length')
    return value,at+size


def verify(archive,key_armor,signature_armor):
    key=None
    for kind,body in packets(key_armor):
        if kind!=6 or body[:1]!=b'\x04':continue
        fingerprint=hashlib.sha1(b'\x99'+len(body).to_bytes(2,'big')+body).hexdigest().upper()
        if fingerprint!=FINGERPRINT:continue
        if body[5]!=1:raise ValueError('Expected the pinned FFmpeg RSA public key')
        n,at=mpi(body,6);e,at=mpi(body,at);key=rsa.RSAPublicNumbers(e,n).public_key()
    if key is None:raise ValueError('FFmpeg public key fingerprint does not match')
    signatures=[body for kind,body in packets(signature_armor) if kind==2]
    if len(signatures)!=1:raise ValueError('Expected one release signature')
    sig=signatures[0]
    if sig[:3]!=b'\x04\x00\x01':raise ValueError('Expected a version-4 binary RSA signature')
    algorithms={8:hashes.SHA256,9:hashes.SHA384,10:hashes.SHA512,11:hashes.SHA224}
    algorithm=algorithms.get(sig[3])
    if algorithm is None:raise ValueError('Unsupported release signature hash')
    end=6+int.from_bytes(sig[4:6],'big');signed=sig[:end]
    end+=2+int.from_bytes(sig[end:end+2],'big');prefix=sig[end:end+2];number,at=mpi(sig,end+2)
    if at!=len(sig):raise ValueError('Trailing release signature data')
    hasher=hashes.Hash(algorithm())
    with archive.open('rb') as source:
        for chunk in iter(lambda:source.read(1024*1024),b''):hasher.update(chunk)
    hasher.update(signed);hasher.update(b'\x04\xff'+len(signed).to_bytes(4,'big'));digest=hasher.finalize()
    if digest[:2]!=prefix:raise ValueError('FFmpeg release hash prefix differs')
    key.verify(number.to_bytes((key.key_size+7)//8,'big'),digest,padding.PKCS1v15(),utils.Prehashed(algorithm()))
    return FINGERPRINT
