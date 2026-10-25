"""Read a bounded operand vocabulary from existing Ghidra listing records.

This module never decodes bytes. Ambiguous/unsupported listing text is rejected.
The parsed operands feed the same C++ emitter that produced the cached oracle.
"""
import re

REGISTERS = {name: (index, width, shift) for width, names in (
    (32, 'eax ecx edx ebx esp ebp esi edi'), (16, 'ax cx dx bx sp bp si di'),
    (8, 'al cl dl bl ah ch dh bh')) for index, name in enumerate(names.split()) for index, shift in [(index%4 if width==8 else index, 8 if width==8 and index>=4 else 0)]}
ALLOWED = {'mov','lea','cmp','test','add','sub','and','or','xor','shl','shr','sar','push','pop',
           'adc','sbb','neg','not','inc','dec','movzx','movsx','imul','mul','shrd','rcr','fcomp'}
SIZES = {'byte':8,'word':16,'dword':32,'qword':64,'float':32,'double':64}


def operand(text):
    text=text.strip().lower()
    if text in REGISTERS:return dict(kind='register',width=REGISTERS[text][1],register=text)
    if re.fullmatch(r'-?0x[0-9a-f]+',text):return dict(kind='immediate',width=32,value_hex=hex(int(text,16)&0xffffffff),signed=True,relative=False)
    size=0
    prefix=re.match(r'(byte|word|dword|qword|float|double) ptr ',text)
    if prefix:size=SIZES[prefix[1]];text=text[prefix.end():]
    segment='ds'
    prefix=re.match(r'(fs|gs|ds|es|ss):',text)
    if prefix:segment=prefix[1];text=text[prefix.end():]
    if not (text.startswith('[') and text.endswith(']')):raise ValueError('unsupported cached operand: '+text)
    body=text[1:-1].replace(' ','')
    prefix=re.match(r'(fs|gs|ds|es|ss):',body)
    if prefix:segment=prefix[1];body=body[prefix.end():]
    base=index='none';scale=0;displacement=0
    for term in body.split('+'):
        if re.fullmatch(r'-?0x[0-9a-f]+',term):displacement+=int(term,16)
        elif term in REGISTERS and REGISTERS[term][1]==32 and base=='none':base=term
        else:
            match=re.fullmatch(r'(eax|ecx|edx|ebx|esp|ebp|esi|edi)\*(0x[1248]|[1248])',term)
            if not match or index!='none':raise ValueError('unsupported cached address term: '+term)
            index=match[1];scale=int(match[2],0)
    if not -(1<<31)<=displacement<1<<32:raise ValueError('cached displacement exceeds source address width')
    return dict(kind='memory',width=size,base=base,index=index,scale=scale,displacement=displacement,
                segment=segment,address_width=32)


def parse(record):
    mnemonic=record['mnemonic'].lower();raw=bytes.fromhex(record['bytes'])
    if record['flow_type']!='FALL_THROUGH':raise ValueError('operand emitter only accepts cached fallthrough instructions')
    if mnemonic=='scasb.repne':
        if raw!=b'\xf2\xae' or record['operand_text']!=['ES:EDI']:raise ValueError('unsupported cached scan form')
        return dict(address=record['address'],bytes=record['bytes'],size=2,text=record['text'],mnemonic='scasb',
                    operands=[],operand_width=8,opcode=0xae,modrm=0,repeat=2)
    if mnemonic not in ALLOWED:raise ValueError('unsupported cached operand mnemonic: '+mnemonic)
    ops=[operand(text) for text in record['operand_text']]
    if not ops or len(ops)>3:raise ValueError('unsupported cached operand count')
    known_widths=[o['width'] for o in ops if o['kind']!='immediate' and o['width']]
    if not known_widths and mnemonic!='push':raise ValueError('ambiguous cached operand width')
    primary=known_widths[0] if known_widths else 32
    for op in ops:
        if op['kind']=='memory' and not op['width']:op['width']=primary
        # The checked emitter stores arithmetic/data immediates as signed
        # source-width values, even when Ghidra prints their unsigned spelling.
        # Shift counts are a separate unsigned operand and must stay unchanged.
        if op['kind']=='immediate' and primary in (8,16) and mnemonic not in ('shl','shr','sar','shrd','rcr'):
            value=int(op['value_hex'],16)
            if value < (1<<primary) and value & (1<<(primary-1)):
                op['value_hex']=hex((value-(1<<primary))&0xffffffff)
            op['width']=primary
    if mnemonic in ('movzx','movsx') and ops[1]['width'] not in (8,16):raise ValueError('ambiguous cached extension width')
    if mnemonic=='fcomp':
        if len(raw)<2 or raw[0] not in (0xd8,0xdc) or ((raw[1]>>3)&7)!=3 or len(ops)!=1 or ops[0]['kind']!='memory' or ops[0]['width']!=(64 if raw[0]==0xdc else 32):
            raise ValueError('cached FCOMP form differs from emitter contract')
        opcode=raw[0];modrm=1;width=32
    else:opcode=modrm=0;width=primary
    return dict(address=record['address'],bytes=record['bytes'],size=record['size'],text=record['text'],
                mnemonic=mnemonic,operands=ops,operand_width=width,opcode=opcode,modrm=modrm,repeat=0)


def serialize(model):
    words=[model['address'],model['bytes'],model['mnemonic'],model['operand_width'],model['opcode'],model['modrm'],model['repeat'],len(model['operands'])]
    for op in model['operands']:
        kind=op['kind'];words += [kind[0],op['width'],op.get('register','none'),op.get('base','none'),op.get('index','none'),op.get('scale',0),
                                op.get('displacement',0),int(op.get('value_hex','0'),16)&0xffffffff,op.get('segment','ds')]
    return ' '.join(map(str,words))+'\n'
