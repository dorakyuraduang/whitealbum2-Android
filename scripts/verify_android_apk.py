"""Audit the delivered common static-recompiled APK and native host."""
import ctypes
import json
from pathlib import Path
from verify_shared_package import verify

if __name__=="__main__":
    kernel=ctypes.WinDLL("kernel32");kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.SetPriorityClass.argtypes=[ctypes.c_void_p,ctypes.c_uint];kernel.SetPriorityClass(kernel.GetCurrentProcess(),0x4000)
    verify(json.loads((Path(__file__).resolve().parents[1]/"logs/shared-host-package.json").read_text()),publish=True)
