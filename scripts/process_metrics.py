"""Read time and memory counters for an explicitly owned child handle."""
import ctypes
from ctypes import wintypes
import os

if os.name=='nt':
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetProcessTimes.argtypes=[wintypes.HANDLE,*([ctypes.POINTER(wintypes.FILETIME)]*4)]
    kernel.GetProcessTimes.restype=wintypes.BOOL
    kernel.QueryPerformanceCounter.argtypes=[ctypes.POINTER(ctypes.c_longlong)]
    kernel.QueryPerformanceFrequency.argtypes=[ctypes.POINTER(ctypes.c_longlong)]
    class MemoryCounters(ctypes.Structure):
        _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD),
            *[(name,ctypes.c_size_t) for name in ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage',
                'QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]]
    kernel.K32GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(MemoryCounters),wintypes.DWORD]
    kernel.K32GetProcessMemoryInfo.restype=wintypes.BOOL
    kernel.ReadProcessMemory.argtypes=[wintypes.HANDLE,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
    kernel.ReadProcessMemory.restype=wintypes.BOOL


def cpu_seconds(handle):
    if os.name!='nt':return None
    creation,exit_time,system,user=(wintypes.FILETIME() for _ in range(4))
    if not kernel.GetProcessTimes(wintypes.HANDLE(int(handle)),ctypes.byref(creation),ctypes.byref(exit_time),ctypes.byref(system),ctypes.byref(user)):
        return None
    value=lambda item:((item.dwHighDateTime<<32)|item.dwLowDateTime)/10000000
    return value(system)+value(user)


def memory_counters(handle):
    if os.name!='nt':return None
    counters=MemoryCounters();counters.cb=ctypes.sizeof(counters)
    if not kernel.K32GetProcessMemoryInfo(wintypes.HANDLE(int(handle)),ctypes.byref(counters),counters.cb):return None
    return dict(peak_working_set_bytes=counters.PeakWorkingSetSize,working_set_bytes=counters.WorkingSetSize,private_bytes=counters.PrivateUsage)


def original_crt_features(handle):
    """Two known CRT feature flags, only on the original child we created."""
    values=(ctypes.c_uint32*2)();copied=ctypes.c_size_t()
    if not kernel.ReadProcessMemory(wintypes.HANDLE(int(handle)),ctypes.c_void_p(0xbc830c),values,8,ctypes.byref(copied)) or copied.value!=8:return None
    return dict(sse_math=values[0],sse_ftol=values[1])


def qpc():
    value=ctypes.c_longlong();kernel.QueryPerformanceCounter(ctypes.byref(value));return value.value


def qpc_hz():
    value=ctypes.c_longlong();kernel.QueryPerformanceFrequency(ctypes.byref(value));return value.value
