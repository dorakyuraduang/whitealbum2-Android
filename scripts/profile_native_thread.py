"""Sample instruction pointers of only a child game thread that this script owns.

This is an intrusive CPU diagnostic, not an FPS benchmark. It never reads a
screen, window contents, input, or memory belonging to another process.
"""
from pathlib import Path
from collections import Counter
from bisect import bisect_right
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import struct
import subprocess
import time
import uuid
import os

from benchmark_original import StartupInfo, ProcessInfo
from benchmark_native_runtime import read_report
from process_metrics import cpu_seconds

ROOT = Path(__file__).resolve().parents[1]


class SymbolInfo(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wintypes.ULONG), ('TypeIndex', wintypes.ULONG),
        ('Reserved', ctypes.c_uint64 * 2), ('Index', wintypes.ULONG),
        ('Size', wintypes.ULONG), ('ModBase', ctypes.c_uint64),
        ('Flags', wintypes.ULONG), ('Value', ctypes.c_uint64),
        ('Address', ctypes.c_uint64), ('Register', wintypes.ULONG),
        ('Scope', wintypes.ULONG), ('Tag', wintypes.ULONG),
        ('NameLen', wintypes.ULONG), ('MaxNameLen', wintypes.ULONG),
        ('Name', ctypes.c_char * 2048)]


class Address64(ctypes.Structure):
    _fields_=[('offset',ctypes.c_uint64),('segment',wintypes.WORD),('mode',wintypes.DWORD)]


class KdHelp64(ctypes.Structure):
    _fields_=[('thread',ctypes.c_uint64),*[(name,wintypes.DWORD) for name in
        ('callback_stack','callback_bstore','next_callback','frame_pointer')],
        *[(name,ctypes.c_uint64) for name in ('call_user_mode','callback_dispatcher','system_range',
            'exception_dispatcher','stack_base','stack_limit')],
        ('build',wintypes.DWORD),('table_size',wintypes.DWORD),('table',ctypes.c_uint64),
        ('stub_offset',wintypes.DWORD),('stub_size',wintypes.DWORD),('reserved',ctypes.c_uint64*2)]


class StackFrame64(ctypes.Structure):
    _fields_=[(name,Address64) for name in ('pc','ret','frame','stack','bstore')]+[
        ('function_table',ctypes.c_void_p),('params',ctypes.c_uint64*4),('far',wintypes.BOOL),
        ('virtual',wintypes.BOOL),('reserved',ctypes.c_uint64*3),('help',KdHelp64)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--warmup', type=float, default=45)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--interval-ms', type=float, default=5)
    parser.add_argument('--unwind', action='store_true', help='Also unwind a bounded number of owned-thread call stacks')
    parser.add_argument('--native-exits', action='store_true', help='Sample native exits without enabling instruction profiling')
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 120 or not 1 <= args.interval_ms <= 50 or not 0 <= args.warmup <= 120:
        raise ValueError('Invalid bounded sampling interval')
    program = args.program.resolve()
    if args.native_exits:os.environ['WA2_NATIVE_EXITS']='1'
    if program == (ROOT / 'WA2_chs.exe').resolve():
        raise ValueError('This sampler is for the generated x64 program only')
    evidence = args.evidence.resolve()
    if evidence in (program, (ROOT / 'WA2_chs.exe').resolve()):
        raise ValueError('Evidence must not overwrite an executable')
    case = ROOT / 'build/native-backend' / ('sample-' + uuid.uuid4().hex)
    case.mkdir(parents=True)
    report_file = case / 'runtime.json'
    command = [str(program), str(ROOT / 'WA2_chs.exe'), '--root', str(args.root.resolve()),
        '--budget', '1000000000000', '--frames', '1000000000', '--report', str(report_file)]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    debug = ctypes.WinDLL('dbghelp', use_last_error=True)
    kernel.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p,
        ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
        ctypes.POINTER(StartupInfo), ctypes.POINTER(ProcessInfo)]
    kernel.CreateProcessW.restype = wintypes.BOOL
    for name in ('ResumeThread', 'SuspendThread'):
        getattr(kernel, name).argtypes = [wintypes.HANDLE]
        getattr(kernel, name).restype = wintypes.DWORD
    kernel.GetThreadContext.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
    kernel.GetThreadContext.restype = wintypes.BOOL
    kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
        ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    kernel.ReadProcessMemory.restype = wintypes.BOOL
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    debug.SymSetOptions.argtypes = [wintypes.DWORD]
    debug.SymInitializeW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, wintypes.BOOL]
    debug.SymInitializeW.restype = wintypes.BOOL
    debug.SymRefreshModuleList.argtypes = [wintypes.HANDLE]
    debug.SymFromAddr.argtypes = [wintypes.HANDLE, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SymbolInfo)]
    debug.SymFromAddr.restype = wintypes.BOOL
    debug.SymCleanup.argtypes = [wintypes.HANDLE]
    debug.StackWalk64.argtypes=[wintypes.DWORD,wintypes.HANDLE,wintypes.HANDLE,
        ctypes.POINTER(StackFrame64),ctypes.c_void_p,*([ctypes.c_void_p]*4)]
    debug.StackWalk64.restype=wintypes.BOOL
    assert ctypes.sizeof(StackFrame64)==264 and ctypes.sizeof(KdHelp64)==112
    startup = StartupInfo(); startup.cb = ctypes.sizeof(startup)
    info = ProcessInfo()
    line = ctypes.create_unicode_buffer(subprocess.list2cmdline(command))
    if not kernel.CreateProcessW(str(program), line, None, None, False, 0x08000004,
            None, str(ROOT), ctypes.byref(startup), ctypes.byref(info)):
        raise ctypes.WinError(ctypes.get_last_error())
    context = ctypes.create_string_buffer(1232 + 15)
    context_address = (ctypes.addressof(context) + 15) & ~15
    # CONTROL is enough for RIP samples; an unwinder also needs integer regs.
    ctypes.c_uint32.from_address(context_address + 48).value = 0x100003 if args.unwind else 0x100001
    samples = []; counts = Counter(); symbols = []; latest = None; warmup_report = None
    modules = []; symbol_error = None; stack_samples = []; stack_symbols = {}; native_regions = Counter(); native_blocks = Counter()
    unwound_samples=[]
    failure = None; suspended = True; initialized = False; used_before = used_after = None
    started = time.perf_counter()
    try:
        debug.SymSetOptions(0x4 | 0x2 | 0x200 | 0x80000 | 0x40000 | 0x1000)
        if kernel.ResumeThread(info.thread) == 0xffffffff:
            raise ctypes.WinError(ctypes.get_last_error())
        suspended = False
        while time.perf_counter() - started < args.warmup:
            if kernel.WaitForSingleObject(info.process, 0) == 0:
                raise RuntimeError('Owned game exited during warmup')
            time.sleep(.1)
        # The initial suspended process has not initialized its loader list.
        # Invading it before ResumeThread can fail with ERROR_INVALID_PARAMETER.
        symbol_path=';'.join(map(str,(program.parent,ROOT/'build/symbols')))
        initialized = bool(debug.SymInitializeW(info.process, symbol_path, True))
        if not initialized:
            symbol_error = str(ctypes.WinError(ctypes.get_last_error()))
        kernel.K32EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
            wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
        kernel.K32GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE,
            wintypes.LPWSTR, wintypes.DWORD]
        kernel.K32GetModuleInformation.argtypes = [wintypes.HANDLE, wintypes.HMODULE,
            ctypes.c_void_p, wintypes.DWORD]
        handles = (wintypes.HMODULE * 1024)(); needed = wintypes.DWORD()
        if not kernel.K32EnumProcessModulesEx(info.process, handles, ctypes.sizeof(handles), ctypes.byref(needed), 3):
            raise ctypes.WinError(ctypes.get_last_error())
        for handle in handles[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
            name = ctypes.create_unicode_buffer(32768); details = ctypes.create_string_buffer(24)
            if kernel.K32GetModuleFileNameExW(info.process, handle, name, len(name)) and kernel.K32GetModuleInformation(info.process, handle, details, len(details)):
                base, size = struct.unpack_from('<QI', details.raw)
                modules.append(dict(file=name.value, base=base, size=size))
        used_before = cpu_seconds(info.process)
        warmup_report=read_report(report_file)
        sampling_started = time.perf_counter()
        while time.perf_counter() - sampling_started < args.seconds:
            if kernel.WaitForSingleObject(info.process, 0) == 0:
                raise RuntimeError('Owned game exited during sampling')
            tick = time.perf_counter()
            if kernel.SuspendThread(info.thread) == 0xffffffff:
                raise ctypes.WinError(ctypes.get_last_error())
            suspended = True
            try:
                if not kernel.GetThreadContext(info.thread, context_address):
                    raise ctypes.WinError(ctypes.get_last_error())
                rip = ctypes.c_uint64.from_address(context_address + 248).value
                if len(stack_samples)<160 and len(samples)%29==0:
                    stack = ctypes.c_uint64.from_address(context_address + 152).value
                    data = ctypes.create_string_buffer(256); got = ctypes.c_size_t()
                    if kernel.ReadProcessMemory(info.process, stack, data, len(data), ctypes.byref(got)) and got.value==len(data):
                        # These are bounded stack candidates, not an unwound
                        # call chain. Resolve only values in this child's modules.
                        candidates = [address for address in struct.unpack('<32Q',data.raw)
                            if any(module['base']<=address<module['base']+module['size'] for module in modules)]
                        stack_samples.append(dict(rip=rip, candidates=candidates))
                if args.unwind and initialized and len(unwound_samples)<64 and len(samples)%67==0:
                    frame=StackFrame64();frame.pc.offset=rip
                    frame.frame.offset=ctypes.c_uint64.from_address(context_address+160).value
                    frame.stack.offset=ctypes.c_uint64.from_address(context_address+152).value
                    frame.pc.mode=frame.frame.mode=frame.stack.mode=3
                    copied=ctypes.create_string_buffer(1232+15);copied_address=(ctypes.addressof(copied)+15)&~15
                    ctypes.memmove(copied_address,context_address,1232);chain=[]
                    for depth in range(24):
                        if not debug.StackWalk64(0x8664,info.process,info.thread,ctypes.byref(frame),copied_address,
                                None,ctypes.cast(debug.SymFunctionTableAccess64,ctypes.c_void_p),
                                ctypes.cast(debug.SymGetModuleBase64,ctypes.c_void_p),None):break
                        if not frame.pc.offset or (chain and chain[-1]==frame.pc.offset):break
                        chain.append(frame.pc.offset)
                    unwound_samples.append(dict(rip=rip,chain=chain))
            finally:
                if kernel.ResumeThread(info.thread) == 0xffffffff:
                    raise ctypes.WinError(ctypes.get_last_error())
                suspended = False
            counts[rip] += 1
            samples.append([round(tick - sampling_started, 6), rip])
            time.sleep(args.interval_ms / 1000)
        used_after = cpu_seconds(info.process)
        latest = read_report(report_file)
        if latest and latest.get('process', latest).get('direct3d9', {}).get('frames_captured', 0):
            raise RuntimeError('Screen capture was unexpectedly enabled')
        # Symbol resolution happens after sampling, so PDB I/O cannot bias the
        # distribution. The handle and module list belong to this child only.
        if initialized:
            debug.SymRefreshModuleList(info.process)
            for address, count in counts.most_common():
                symbol = SymbolInfo(); symbol.SizeOfStruct = 88; symbol.MaxNameLen = 2047
                displacement = ctypes.c_uint64()
                found = bool(debug.SymFromAddr(info.process, address,
                    ctypes.byref(displacement), ctypes.byref(symbol)))
                symbols.append(dict(address=address, samples=count,
                    symbol=bytes(symbol.Name).decode('utf-8', errors='replace') if found else None,
                    displacement=displacement.value if found else None,
                    module=symbol.ModBase if found else None))
            for address in {address for stack in stack_samples for address in stack['candidates']}|{address for stack in unwound_samples for address in stack['chain']}:
                symbol=SymbolInfo();symbol.SizeOfStruct=88;symbol.MaxNameLen=2047;displacement=ctypes.c_uint64()
                if debug.SymFromAddr(info.process,address,ctypes.byref(displacement),ctypes.byref(symbol)):
                    stack_symbols[str(address)]=dict(symbol=bytes(symbol.Name).decode('utf-8',errors='replace'),displacement=displacement.value)
        layout=(latest or {}).get('process',latest or {}).get('native_execution',{}).get('code_layout')
        if layout:
            size=layout['point_count']*8;base=layout['base'];end=layout['end'];points_address=layout['points']
            own=next((module for module in modules if module['base']<=base<end<=module['base']+module['size']),None)
            if not own or not 0<size<8_000_000 or layout['stride']!=8 or not own['base']<=points_address<points_address+size<=own['base']+own['size']:
                raise ValueError('Invalid owned native code layout')
            data=ctypes.create_string_buffer(size);got=ctypes.c_size_t()
            if not kernel.ReadProcessMemory(info.process,points_address,data,size,ctypes.byref(got)) or got.value!=size:
                raise ctypes.WinError(ctypes.get_last_error())
            points=list(struct.iter_unpack('<II',data.raw));offsets=[point[0] for point in points]
            if offsets!=sorted(offsets) or offsets[0]!=0 or offsets[-1]>=end-base:
                raise ValueError('Invalid native code point order')
            kinds={0:'body',8:'guard',2:'metadata',4:'budget',1:'fault',6:'out',3:'region-body',5:'region-metadata',7:'region-guard'}
            for address,count in counts.items():
                if not base<=address<end:continue
                offset,tag=points[bisect_right(offsets,address-base)-1]
                if tag<0x400000:kind={1:'dispatch',2:'state-api',3:'exit',4:'entry',5:'dynamic-out'}.get(tag,'unknown')
                else:kind=kinds.get(tag>>28,'unknown');native_blocks[hex(tag&0x0fffffff)]+=count
                native_regions[kind]+=count
    except Exception as error:
        failure = str(error)
    finally:
        if suspended:
            kernel.ResumeThread(info.thread)
        if kernel.WaitForSingleObject(info.process, 0) != 0:
            kernel.TerminateProcess(info.process, 0xe0000001)
            kernel.WaitForSingleObject(info.process, 15000)
        if initialized:
            debug.SymCleanup(info.process)
        kernel.CloseHandle(info.thread); kernel.CloseHandle(info.process)
    groups = Counter()
    for symbol in symbols:
        groups[symbol['symbol'] or '<unresolved>'] += symbol['samples']
    record = dict(schema='wa2.owned-thread-sample.v1', pid=info.pid, tid=info.tid,
        program=str(program), program_sha256=hashlib.sha256(program.read_bytes()).hexdigest(),
        command=command, native_exit_sampling=args.native_exits,warmup_runtime=warmup_report,warmup_seconds=args.warmup, sampling_seconds=args.seconds,
        interval_ms=args.interval_ms, samples=samples, instruction_pointers=symbols,
        grouped_symbols=groups.most_common(), modules=modules, symbol_error=symbol_error,
        stack_samples=stack_samples,stack_symbols=stack_symbols,
        unwound_samples=unwound_samples,
        native_regions=native_regions.most_common(),native_blocks=native_blocks.most_common(),process_cpu_seconds_before=used_before,
        process_cpu_seconds_after=used_after, report_file=str(report_file), runtime=latest,
        error=failure, screen_access=False, automated_input=False,
        limits='Intrusive RIP samples of an owned main thread; not an FPS benchmark or scene validation.')
    evidence.parent.mkdir(parents=True, exist_ok=True)
    evidence.write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(dict(samples=len(samples), error=failure, symbol_error=symbol_error, grouped_symbols=groups.most_common(25)), indent=2))
    return 1 if failure or not samples else 0


if __name__ == '__main__':
    raise SystemExit(main())
