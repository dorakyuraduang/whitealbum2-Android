"""Build static integer traces from the already generated translation.

This does not decode or compile guest instructions at runtime. The generated
instruction functions remain untouched and serve as the equivalence oracle.
Only audited transactional integer templates and verified pure-x87 batches
are eligible. A faulting integer body has not yet changed Cpu or guest memory.
Strings, APIs, indirect control flow and calls/returns remain in the oracle.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/python'))
import pefile
PAGES = (0x408, 0x439, 0x43B, 0x43C, 0x43D, 0x456, 0x464, 0x469,
         0x46E, 0x470, 0x473, 0x474, 0x47E, 0x49F)
SPECIALIZED_ENTRIES = {int(value[:-1], 16) for value in re.findall(
    r'0x[0-9a-f]+u', (ROOT/'port/specialized_entries.inc').read_text())}
INTEGER = {
    "mov", "movzx", "movsx", "lea", "add", "sub", "adc", "sbb",
    "and", "or", "xor", "cmp", "test", "inc", "dec", "neg", "not",
    "shl", "shr", "sar", "rol", "ror", "shld", "shrd", "imul", "mul",
    "bswap", "clc", "stc", "cmc", "cld", "std", "nop", "push", "pop",
    "cdq", "cwde", "setl", "setle", "setz",
}
CONDITIONAL = {"jz", "jnz", "jl", "jle", "jp", "jnl", "jns", "jnbe", "jnle", "jnp", "jbe"}
HELPERS = {
    "reg", "check_code", "set_reg", "push", "pop", "logic", "sub",
    "add", "shift", "sign_extend", "incdec", "imul", "test_condition",
    "multiply", "double_shift",
}
PURE_HELPERS = HELPERS - {'check_code', 'push', 'pop'}
CASE = re.compile(r"case 0x([0-9a-f]+)u: \{ (.*?)\nreturn true; \}", re.S)
FLAG_BODY = re.compile(
    r"(wr::check_code\(m,s.eip,\{[\d,]+\}\);\n)"
    r"\{((?:auto [abc]=[^;]+;)+)auto(&)? f=s;"
    r"auto v=wr::(incdec|logic|imul|add|sub|shift)\(f,([^;]+)\);"
    r"(.*?)s.flags=f.flags;s.defined_flags=f.defined_flags;\}"
    r"(\ns.eip=0x[0-9a-f]+u;)", re.S)
POP_BODY = re.compile(
    r"(wr::check_code\(m,s.eip,\{[\d,]+\}\);\n)"
    r"\{auto next=s;auto value=wr::pop\(next,m,(\d+)\);"
    r"wr::set_reg\(next,(\d+),value,(\d+),(\d+)\);s=next;\}"
    r"(\ns.eip=0x[0-9a-f]+u;)")
DIRECT_POP_BODY = re.compile(
    r"(wr::check_code\(m,s.eip,\{[\d,]+\}\);\n)"
    r"\{auto value=wr::pop\(s,m,(\d+)\);"
    r"wr::set_reg\(s,(\d+),value,(\d+),(\d+)\);\}"
    r"(\ns.eip=0x[0-9a-f]+u;)")


def register_shape(pc: int, index: str, width: str, shift: str) -> tuple[int, int, int]:
    index, width, shift = int(index), int(width), int(shift)
    if not (0 <= index < 8 and width in (8, 16, 32)
            and (shift == 0 or (shift == 8 and width == 8 and index < 4))):
        raise ValueError(f"unaudited register shape at {pc:#x}")
    return index, width, shift


def fixed_accesses(body: str) -> str:
    """Use the live execution scope for audited constant-width memory calls."""
    replacements = []
    for match in re.finditer(r'm\.(load|store)\(', body):
        start = match.end()
        depth, arguments, previous = 1, [], start
        for end in range(start, len(body)):
            character = body[end]
            if character == '(':
                depth += 1
            elif character == ')':
                depth -= 1
                if not depth:
                    arguments.append(body[previous:end])
                    break
            elif character == ',' and depth == 1:
                arguments.append(body[previous:end])
                previous = end + 1
        else:
            raise ValueError('Unterminated memory access')
        expected = 2 if match[1] == 'load' else 3
        if len(arguments) != expected or arguments[-1] not in ('8','16','32'):
            raise ValueError('Nonliteral or unaudited memory width')
        replacements.append((match.start(), end+1,
            f'execution.{match[1]}<{arguments[-1]}>(' + ','.join(arguments[:-1]) + ')'))
    for start, end, replacement in reversed(replacements):
        body = body[:start] + replacement + body[end:]
    return body


def elide_transaction_copies(pc: int, body: str) -> tuple[str, str | None]:
    """Elide only copies whose final state cannot be followed by GuestFault.

    Operand reads happen before the flag helper. These six helpers cannot
    GuestFault for the literal widths below; their only Cpu outputs are flags.
    A constant register commit is also nonfaulting. Memory destinations retain
    the original Cpu temporary so a failed store cannot expose changed flags.
    POP-to-register similarly performs its sole faulting load before changing
    ESP, followed only by a validated register assignment.
    """
    if "auto f=s;" in body or "auto& f=s;" in body:
        if "m.store(" in body or "wr::push(" in body:
            if "auto& f=s;" in body:
                raise ValueError(f"flag alias precedes guest memory write at {pc:#x}")
            return body, "retained_rmw"
        match = FLAG_BODY.fullmatch(body)
        if not match:
            raise ValueError(f"unaudited flag-copy body at {pc:#x}")
        guard, operands, reference, helper, arguments, commit, finish = match.groups()
        # No memory access or other helper can be introduced after the flag
        # operation by broad textual replacement: every accepted call and
        # commit is matched here in full.
        argument_patterns = {
            "incdec": r"a,(8|16|32),(?:true|false)",
            "logic": r"a[&|^]b,(8|16|32)",
            "imul": r"a,b,(8|16|32)",
            "add": r"a,b,(8|16|32)(?:,s.flags&wr::CF)?",
            "sub": r"(?:a,b|0,a),(8|16|32)(?:,s.flags&wr::CF)?",
            "shift": r"a,c,(8|16|32),[0-6]",
        }
        call = re.fullmatch(argument_patterns[helper], arguments)
        if not call:
            raise ValueError(f"unaudited flag-helper arguments at {pc:#x}")
        width = int(call[1])
        for register in re.finditer(r"wr::reg\(s,(\d+),(\d+),(\d+)\)", operands):
            register_shape(pc, *register.groups())
        if commit != "(void)v;":
            register = re.fullmatch(r"wr::set_reg\(s,(\d+),v,(\d+),(\d+)\);", commit)
            if not register or register_shape(pc, *register.groups())[1] != width:
                raise ValueError(f"unaudited flag-result commit at {pc:#x}")
        return (guard + "{" + operands + "auto& f=s;auto v=wr::" + helper
                + "(f," + arguments + ");" + commit
                + "s.flags=f.flags;s.defined_flags=f.defined_flags;}" + finish), "already_flags" if reference else "flags"
    if "auto next=s;" in body or "auto value=wr::pop(s,m," in body:
        direct = DIRECT_POP_BODY.fullmatch(body)
        match = direct or POP_BODY.fullmatch(body)
        if not match:
            raise ValueError(f"unaudited POP-copy body at {pc:#x}")
        guard, pop_width, index, width, shift, finish = match.groups()
        register_shape(pc, index, width, shift)
        if pop_width != width or int(width) not in (16, 32) or int(shift) != 0:
            raise ValueError(f"unaudited POP width at {pc:#x}")
        return (guard + "{auto value=wr::pop(s,m," + width + ");wr::set_reg(s,"
                + index + ",value," + width + "," + shift + ");}" + finish), "already_pop" if direct else "pop"
    return body, None


def generate() -> dict:
    image_path = ROOT/'WA2_chs.exe'
    if hashlib.sha256(image_path.read_bytes()).hexdigest() != json.loads((ROOT/'config/wa2.json').read_text())['sha256']:
        raise ValueError('Original image/profile mismatch')
    image = pefile.PE(str(image_path))
    graph = json.loads((ROOT / "logs/cfg.json").read_text(encoding="utf-8"))
    instructions = {i["address"]: i for i in graph["instructions"]}
    rows = []
    files = []
    copy_elisions = {"flags": 0, "pop": 0, "retained_rmw": 0, "already_flags": 0, "already_pop": 0}
    register_groups = 0
    deferred_flag_entries = []
    mixed_fp_sequences = []
    fp_sequences = json.loads((ROOT/'port/fp_sequences_manifest.json').read_text())['sequences']
    native_path=ROOT/'port/native_backend_manifest.json'
    preferred_entries=SPECIALIZED_ENTRIES | (set(json.loads(native_path.read_text())['entry_points']) if native_path.exists() else set())
    for page in PAGES:
        path = ROOT / f"generated/wa2-win64/code_page_{page}.cpp"
        source = path.read_text(encoding="utf-8")
        selected = {}
        full_flags = {}
        for pc_hex, body in CASE.findall(source):
            pc = int(pc_hex, 16)
            ins = instructions[pc]
            mnemonic = ins["mnemonic"]
            if pc + ins['size'] > (page + 1) * 4096:
                continue  # A page proof cannot cover a crossing instruction.
            if mnemonic not in INTEGER | CONDITIONAL | {"jmp"}:
                continue
            if mnemonic == "jmp":
                match = re.search(r"\{auto target=0x([0-9a-f]+)u;s.eip=target;\}", body)
                if not match:
                    continue
                targets = [int(match[1], 16)]
            elif mnemonic in CONDITIONAL:
                match = re.search(r"s.eip=.*\?0x([0-9a-f]+)u:0x([0-9a-f]+)u;", body)
                if not match:
                    raise ValueError(f"unrecognized conditional at {pc:#x}")
                targets = [int(match[1], 16), int(match[2], 16)]
            else:
                match = re.search(r"s.eip=0x([0-9a-f]+)u;$", body)
                if not match:
                    raise ValueError(f"nontransactional/control-flow template at {pc:#x}")
                targets = [int(match[1], 16)]
            helpers = set(re.findall(r"wr::(\w+)\(", body))
            if helpers - HELPERS:
                raise ValueError(f"unaudited helper at {pc:#x}: {helpers - HELPERS}")
            guard = re.match(r"wr::check_code\(m,s.eip,\{([\d,]+)\}\);", body)
            if not guard or bytes(map(int, guard[1].split(","))).hex() != ins["bytes"]:
                raise ValueError(f"generated body differs from graph at {pc:#x}")
            # There are no memory writes before a potentially faulting read in
            # these source templates. RMW uses a temporary Cpu and commits flags
            # after the store. Register POP loads before advancing ESP and its
            # register commit is nonfaulting; PUSH stores before changing ESP.
            if mnemonic == "pop" and not (POP_BODY.fullmatch(body) or DIRECT_POP_BODY.fullmatch(body)):
                continue  # Memory/segment POP stays in the original oracle.
            memory_calls = list(re.finditer(r"m\.(?:load|store)\(", body))
            stores = list(re.finditer(r"m\.store\(", body))
            commits = list(re.finditer(r"wr::set_reg\(s,|s\.flags=|s\.defined_flags=|(?<!\w)s=|s\.r\[\w+\]=", body))
            if len(stores) > 1:
                raise ValueError(f"multiple memory commits at {pc:#x}")
            if memory_calls and commits and min(m.start() for m in commits) < max(m.start() for m in memory_calls):
                raise ValueError(f"Cpu commit precedes potentially faulting access at {pc:#x}")
            if stores and any(m.start() > stores[0].start() for m in memory_calls):
                raise ValueError(f"memory commit precedes another access at {pc:#x}")
            body, copy_kind = elide_transaction_copies(pc, body)
            if copy_kind:
                copy_elisions[copy_kind] += 1
            flag_match = FLAG_BODY.fullmatch(body)
            if flag_match and flag_match[4] in ('add','sub'):
                arguments = re.fullmatch(r'(a|0),(a|b),(8|16|32)',flag_match[5])
                if arguments:
                    full_flags[pc] = (flag_match,arguments.groups())
            body = re.sub(r'^wr::check_code\(m,s.eip,\{[\d,]+\}\);\n', '', body)
            selected[pc] = (body, targets, bool(stores) or 'wr::push(' in body)
            rows.append((pc, ins["size"], mnemonic))

        native_fp = {}
        for sequence in fp_sequences:
            entry = sequence['entry']
            if entry >> 12 != page or entry in SPECIALIZED_ENTRIES:
                continue
            members = sorted((i for i in instructions.values() if entry <= i['address'] < sequence['end']),key=lambda i:i['address'])
            if len(members) != sequence['instructions'] or any(i['address'] in selected for i in members):
                raise ValueError('Pure FP sequence does not match the integer oracle boundary')
            native_fp[entry] = sequence
            selected[entry] = ('',[sequence['end']],True)
            rows.extend((i['address'],i['size'],i['mnemonic']) for i in members)
            mixed_fp_sequences.append(entry)

        flag_readers = {'logic','incdec','shift','double_shift','imul','multiply'}
        for pc, (body, targets, writes) in list(selected.items()):
            if pc in full_flags:
                match, (left,right,width) = full_flags[pc]
                # Record only after the potentially faulting memory commit.
                # On a read/store fault, the preceding descriptor is intact.
                operands, helper, commit, finish = match[2],match[4],match[6],match[7]
                operator = '+' if helper == 'add' else '-'
                body = ('{' + operands + f'auto v=({left}{operator}{right})&wr::mask({width});'
                        + commit + f'pending.set({left},{right},{width},{str(helper=="sub").lower()});}}' + finish)
                deferred_flag_entries.append(pc)
            elif instructions[pc]['mnemonic'] in CONDITIONAL:
                encoded = bytes.fromhex(instructions[pc]['bytes'])
                if len(encoded)==2 and 0x70<=encoded[0]<=0x7f:
                    cc=encoded[0]&15
                elif len(encoded)==6 and encoded[0]==0x0f and 0x80<=encoded[1]<=0x8f:
                    cc=encoded[1]&15
                else:
                    raise ValueError('Unaudited conditional branch encoding')
                body=f's.eip=pending.test({cc})?0x{targets[0]:x}u:0x{targets[1]:x}u;'
            else:
                body = re.sub(r'wr::test_condition\(s,(\d+)\)',r'pending.test(\1)',body)
                helpers = set(re.findall(r'wr::(\w+)\(',body))
                if 's.flags' in body or helpers & (flag_readers|{'add','sub'}):
                    body = 'pending.flush();\n' + body
            selected[pc] = (body,targets,writes)

        # A straight run can guard its budget/return boundary once. Read-only
        # memory operations retain a checkpoint after each completed body, so
        # a later read fault returns the exact charged prefix to the oracle.
        pure = {pc for pc, (body, targets, writes) in selected.items()
                if pc not in native_fp and not writes and 'throw' not in body
                and len(targets) == 1 and targets[0] == pc + instructions[pc]['size']
                and set(re.findall(r'wr::(\w+)\(', body)) <= PURE_HELPERS|{'mask','pop'}}
        previous = {pc + instructions[pc]['size']: pc for pc in selected}
        branch_targets = {target for pc, (_, targets, _) in selected.items()
                          if len(targets) != 1 or targets[0] != pc + instructions[pc]['size']
                          for target in targets}
        pending = sorted(pc for pc in pure if previous.get(pc) not in pure or pc in branch_targets)
        groups = {}
        for start in pending:
            if start in groups:
                continue
            group, current = [], start
            while current in pure and len(group) < 12:
                if group and current in preferred_entries:
                    break
                group.append(current)
                current = selected[current][1][0]
            if len(group) >= 2:
                groups[start] = group
            if len(group) == 12 and current in pure and current not in pending:
                pending.append(current)
        register_groups += len(groups)
        out = [
            "// Generated by scripts/generate_static_blocks.py; do not edit.",
            f"// Oracle source SHA256: {hashlib.sha256(path.read_bytes()).hexdigest()}",
            f"static constexpr std::array<std::uint8_t,Memory::PAGE> page_signature_{page}={{",
            ','.join(str(byte) for byte in image.get_data(page*4096-image.OPTIONAL_HEADER.ImageBase,4096)),
            '};',
            f"static unsigned block_page_{page}(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc) {{",
            'auto execution=m.execution_scope();',
            f'try{{if(!m.matches_static_code_page<page_signature_{page}>(0x{page*4096:x}u))return 0;}}catch(const GuestFault&){{return 0;}}',
            'const auto code_epoch=m.code_epoch();',
            'BlockFlags pending(s);',
            "unsigned completed=0;",
            "switch(s.eip){",
        ]
        out += [f"case 0x{pc:x}u:goto pc_{pc:x};" for pc in sorted(selected)]
        out += ["default:return 0;}"]
        for pc, (body, targets, writes) in sorted(selected.items()):
            out += [
                f"pc_{pc:x}:",
                "if(completed==limit || !budget || s.eip==return_pc)return completed;",
            ]
            if pc in native_fp:
                end = targets[0]
                out += ['{const auto n=wa2_fp_sequence_batch(s,m,budget,limit-completed,return_pc);',
                        'if(!n)return completed;completed+=n;}',
                        'if(m.code_epoch()!=code_epoch)return completed;',
                        f'goto pc_{end:x};' if end in selected and end not in preferred_entries else 'return completed;']
                continue
            if pc in groups:
                group = groups[pc]
                end = selected[group[-1]][1][0]
                out.append(f'if(limit-completed>={len(group)} && budget>={len(group)} && (return_pc<0x{pc:x}u || return_pc>=0x{end:x}u)){{')
                out.append('unsigned advanced=0;try{')
                for advanced, member in enumerate(group, 1):
                    out += [fixed_accesses(selected[member][0]),f'advanced={advanced};']
                out.append('}catch(const GuestFault&){budget-=advanced;return completed+advanced;}')
                out.append(f'budget-={len(group)};completed+={len(group)};')
                out.append(f'goto pc_{end:x};' if end in selected and end not in preferred_entries else 'return completed;')
                out.append('}')
            out += ['try {',fixed_accesses(body),'}catch(const GuestFault&){return completed;}','--budget;++completed;']
            if writes:
                out.append('if(m.code_epoch()!=code_epoch)return completed;')
            if len(targets) == 1:
                target = targets[0]
                out.append(f"goto pc_{target:x};" if target in selected and target not in preferred_entries else "return completed;")
            else:
                first, second = targets
                if first in selected and first not in preferred_entries:
                    out.append(f"if(s.eip==0x{first:x}u)goto pc_{first:x};")
                else:
                    out.append(f"if(s.eip==0x{first:x}u)return completed;")
                out.append(f"goto pc_{second:x};" if second in selected and second not in preferred_entries else "return completed;")
        out += ["}", ""]
        destination = ROOT / f"port/static_blocks_{page:x}.inc"
        destination.write_text("\n".join(out), encoding="utf-8")
        files.append({"page": page, "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "instructions": len(selected)})
    (ROOT / "port/static_blocks_entries.inc").write_text(
        "// Generated static entry points, including instruction byte lengths.\n"
        + "\n".join(f"{{0x{pc:x}u,{size}}}," for pc, size, _ in sorted(rows)) + "\n",
        encoding="utf-8",
    )
    report = {"schema": "wa2.static-integer-blocks.v1", "instructions": len(rows), "pages": files,
              "retains_per_instruction_code_guards": False, "cached_immutable_page_proofs": True,
              "yields_after_executable_mutation": True, "transactional_fault_fallback": True,
              "bounded_register_groups": register_groups,
              "read_fault_prefix_checkpoints": True, "scope_fixed_width_accesses": True,
              "deferred_flag_entries": deferred_flag_entries,
              "conditional_flags_materialized_on_exit_only": True,
              "mixed_fp_sequences": mixed_fp_sequences,
              "cpu_copy_elision": copy_elisions}
    (ROOT / "port/static_blocks_manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    argparse.ArgumentParser(description=__doc__).parse_args()
    print(json.dumps(generate(), indent=2))
