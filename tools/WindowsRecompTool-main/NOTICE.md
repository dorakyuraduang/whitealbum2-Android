# Provenance and dependency boundaries

WinRecomp-owned source is MIT licensed. Upstream projects retain their own licenses.

The build links **Zydis 4.1.1** at `a2278f1d254e492f6a6b39f6cb5d1f5d515659dc`,
with its **Zycore** submodule at `0b2432ced0884fd152b471d97ecf0258ff4d859f`.
Keep both upstream MIT license notices with distributions. Do not replace them
with WinRecomp's license.

`research/upstreams.lock.json` pins nine repositories. Only Zydis is compiled
into the tool. Research submodules are NOT dependencies of generated C++.
No implementation from the other reference repositories has been copied into
WinRecomp-owned source in this foundation change. Their presence is not a
license grant to merge them. Review file-level licenses and notices before reuse.

Optional differential tests invoke the separately installed **Unicorn 2.1.2**
Python package as an independent execution oracle. Unicorn is GPL licensed;
it is neither linked into the WinRecomp CLI nor into generated code. Test
installations retain the package's upstream licenses. No Unicorn binary or
source is redistributed in this repository's own source tree.

The emitted code uses WinRecomp's own header-only subset runtime. The original
input program and any generated game-derived code may have separate rights;
WinRecomp's license does not relicense that material. Tests and CI contain only
our synthetic machine-code fixtures. The E3 integration contract contains
hashes, addresses and metadata, not the game executable or its assets.
