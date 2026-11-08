#pragma once
#include <memory>
namespace wr {class Process;class MutableFiles;}
namespace wa2::android {void mount_compiled_resources(wr::Process&);std::shared_ptr<wr::MutableFiles> compiled_save_files();}
