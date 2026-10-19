"""Check the added user-mode PUSHFD/POPFD semantics against independent IA-32."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parent.parent
sys.path[:0]=[str(ROOT/'tools'/'python'),str(ROOT/'tools'/'WindowsRecompTool-main'/'tests')]
import conformance
conformance.CASES={
    'pushfd_read':'9c58c3',
    'popfd_id_toggle':'9c583500002000509d9c58c3',
    'popfd_df_toggle':'9c583500040000509d9c58c3',
}
sys.argv=[__file__,'--tool',str(ROOT/'build'/'winrecomp'/'winrecomp.exe'),'--cxx',str(ROOT/'scripts'/'zig-cxx.cmd'),'--out',str(ROOT/'build'/'flags-tests'),'--vectors','256']
conformance.main()
