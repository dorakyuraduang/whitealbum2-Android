"""Generate fixed pure-x87 batches from already translated instruction bodies.

This is a build-time generator, never a runtime decoder. Eligibility follows
the existing translator's x87 helper calls and excludes flag-dependent FCMOV.
"""
from pathlib import Path
import hashlib
import json
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/python"))
import capstone
import pefile
from retired_modules import RELEASE_EXCLUSIONS,contains as retired_contains,MANIFEST as RETIRED_MANIFEST

PAGES = [0x408, 0x43a, 0x43b, 0x43c, 0x43d, 0x456, 0x469, 0x46d, 0x46e, 0x470, 0x473, 0x474]
BASE, END = min(PAGES) << 12, (max(PAGES) + 1) << 12
specialized_entries = {int(value[:-1], 16) for value in re.findall(
    r'0x[0-9a-f]+u', (ROOT/'port/specialized_entries.inc').read_text())}
raw = (ROOT / "WA2_chs.exe").read_bytes()
assert hashlib.sha256(raw).hexdigest() == "246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327"
pe = pefile.PE(data=raw)
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
decoder.detail = True
cases = re.compile(r'case (0x[0-9a-f]+)u: \{ wr::check_code\(m,s.eip,\{([^}]+)\}\);\n(.*?)\ns.eip=(0x[0-9a-f]+)u;\nreturn true; \}', re.S)
reg_call = re.compile(r'wr::x87_execute\(s,wr_x87_\d+_r\d+,nullptr,(\d+),false,false\);')
mem_call = re.compile(r'wr::x87_memory\(s,m,wr_x87_\d+_m\d+,(.+),(\d+),(true|false),(\d+),false\);')
registers = {name: n for n, name in enumerate(['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'])}
sequences = []
for page in PAGES:
    run = []
    # Short x87 runs inside mixed blocks avoid a return to the central
    # dispatcher. Other pages retain the original minimum batch length.
    minimum = 3 if page in (0x43a,0x46d) else 1
    for match in cases.finditer((ROOT / f"generated/wa2-win64/code_page_{page}.cpp").read_text()):
        pc, end = int(match[1], 16), int(match[4], 16)
        encoded = bytes(int(x) for x in match[2].split(','))
        assert pe.get_data(pc - pe.OPTIONAL_HEADER.ImageBase, len(encoded)) == encoded
        decoded = list(decoder.disasm(encoded, pc))
        assert len(decoded) == 1
        instruction = decoded[0]
        register_match, memory_match = reg_call.fullmatch(match[3]), mem_call.fullmatch(match[3])
        allowed = bool(register_match or memory_match) and not instruction.mnemonic.startswith('fcmov')
        if memory_match:
            operands = [o for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM]
            assert len(operands) == 1
            operand = operands[0]
            allowed = allowed and instruction.addr_size == 4 and operand.mem.segment == capstone.x86.X86_REG_INVALID
            allowed = allowed and 'segment_address' not in memory_match[1] and int(memory_match[2]) <= 10
        if not allowed or (run and (run[-1]['end'] != pc or pc in specialized_entries)):
            if len(run) >= minimum:
                sequences.append(run)
            run = []
        if allowed:
            assert end == pc + len(encoded)
            item = dict(pc=pc, end=end, encoded=encoded, instruction=instruction,
                        fop=int(memory_match[4] if memory_match else register_match[1]))
            if memory_match:
                operand = next(o for o in instruction.operands if o.type == capstone.x86.X86_OP_MEM)
                base = registers.get(instruction.reg_name(operand.mem.base), -1)
                index = registers.get(instruction.reg_name(operand.mem.index), -1)
                item['memory'] = (base, index, operand.mem.scale if index >= 0 else 0,
                                  operand.mem.disp & 0xffffffff, int(memory_match[2]))
                item['store'] = memory_match[3] == 'true'
            run.append(item)
    if len(run) >= minimum:
        sequences.append(run)

declarations, descriptors, assembly, rows = [], [], [], []
release_rows=[];retired_rows=[]
max_operands = 0
for number, sequence in enumerate(sequences):
    assert not any(sequence[0]['pc'] < pc < sequence[-1]['end'] for pc in specialized_entries)
    name = f'wa2_fp_sequence_{number}'
    declarations.append(f'extern "C" void {name}(void*,void*);')
    operands = {}
    code = b''.join(i['encoded'] for i in sequence)
    body = []
    last_memory = -1
    for item in sequence:
        encoded = item['encoded']
        body.append(f"    // {item['pc']:#x}: {item['instruction'].mnemonic} {item['instruction'].op_str}")
        if 'memory' in item:
            key = item['memory']
            if key not in operands:
                operands[key] = [len(operands), False, False]
            slot = operands[key]
            slot[2 if item['store'] else 1] = True
            last_memory = slot[0]
            # R11 is the checked scratch base, one 16-byte slot per expression.
            # /digit is retained from the exact original x87 opcode.
            assert 0xd8 <= encoded[0] <= 0xdf
            offset = slot[0] * 16
            if offset <= 127:
                encoded = bytes([0x41, encoded[0], 0x43 | (encoded[1] & 0x38), offset])
            else:
                encoded = bytes([0x41, encoded[0], 0x83 | (encoded[1] & 0x38)]) + offset.to_bytes(4, 'little')
        body.append('    .byte ' + ', '.join(f'0x{x:02x}' for x in encoded))
    max_operands = max(max_operands, len(operands))
    declarations.append(f'static constexpr std::uint8_t fp_code_{number}[]={{' + ','.join(str(x) for x in code) + '};')
    if operands:
        declarations.append(f'static constexpr Wa2FpOperand fp_operands_{number}[]={{')
        for (base, index, scale, displacement, size), (slot, read, write) in operands.items():
            declarations.append(f'    {{{base},{index},{scale},{displacement}u,{size},{str(read).lower()},{str(write).lower()}}},')
        declarations.append('};')
        operand_span = f'fp_operands_{number}'
    else:
        operand_span = '{}'
    last = sequence[-1]
    retired=retired_contains(sequence[0]['pc'],RELEASE_EXCLUSIONS)
    assert all(retired_contains(item['pc'],RELEASE_EXCLUSIONS)==retired for item in sequence)
    if retired:descriptors.append('#if !defined(WA2_RELEASE_FP_SEQUENCES)')
    descriptors.append(f'    {{{{{sequence[0]["pc"]}u,{last["end"]}u,{len(sequence)},{last["pc"]}u,{last["fop"]},{last_memory},{operand_span}}},fp_code_{number},{name}}},')
    if retired:descriptors.append('#endif')
    assembly.append(f""".section .text${name},"xr",discard,{name}
.globl {name}
.seh_proc {name}
{name}:
    mov r10, rcx
    mov r11, rdx
    sub rsp, 520
    .seh_stackalloc 520
    .seh_endprologue
    .byte 0x48, 0x0f, 0xae, 0x04, 0x24
    .byte 0x49, 0x0f, 0xae, 0x0a
""" + '\n'.join(body) + """
    .byte 0x49, 0x0f, 0xae, 0x02
    .byte 0x48, 0x0f, 0xae, 0x0c, 0x24
    add rsp, 520
    ret
.seh_endproc
""")
    row=dict(entry=sequence[0]['pc'], end=last['end'], instructions=len(sequence), operands=len(operands))
    rows.append(row);(retired_rows if retired else release_rows).append(row)

generated = '// Generated by scripts/generate_fp_sequences.py.\n' + '\n'.join(declarations)
generated += '\nstatic constexpr FpSequence sequences[]={\n' + '\n'.join(descriptors) + '\n};\n'
generated += f'static constexpr unsigned max_fp_operands={max_operands};\n'
(ROOT / 'port/fp_sequences_data.inc').write_text(generated)
(ROOT / 'port/fp_sequences.S').write_text('// Generated fixed Win64 x87 helpers; checked scratch only.\n.intel_syntax noprefix\n.text\n' + '\n'.join(assembly))
(ROOT / 'port/fp_sequences_manifest.json').write_text(json.dumps(dict(schema='wa2.fp-sequences.v1', pages=PAGES, sequences=rows, max_operands=max_operands,
    release_sequences=release_rows,retired_sequences=retired_rows,retired_module_manifest_sha256=hashlib.sha256(RETIRED_MANIFEST.read_bytes()).hexdigest(),
    generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),helpers_in_separate_comdats=True), indent=2)+'\n')
print(f'Generated {len(sequences)} sequences, {sum(len(s) for s in sequences)} x87 instructions; max {max_operands} operands')
