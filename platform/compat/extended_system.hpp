#pragma once
namespace wr {class Process;}
namespace wa2::platform::compat {
// Additional Win32 ABI services used by the Special Contents CRT. These use
// the same portable filesystem, encoding, heap and renderer on every host.
void install_extended_system(wr::Process&);
void install_d3dx43_aliases(wr::Process&);
}
