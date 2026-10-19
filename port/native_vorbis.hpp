#pragma once
namespace wr {class Process;
// Tests can retain the original complete decoder for a reference comparison.
void install_wa2_native_vorbis(Process&,bool enabled=true);
}
