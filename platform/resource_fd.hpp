#pragma once
#include "resource_archive.hpp"

namespace wa2::platform {
// Takes exclusive ownership, including on failure. Android supplies this fd
// through the directory grant; no local copy of the original PAK is created.
// The source accepts regular, seekable files. POSIX reads use pread, so parallel
// readers do not share a cursor and never call back into Java per read.
std::shared_ptr<resource::Source> adopt_resource_fd(int descriptor);
}
