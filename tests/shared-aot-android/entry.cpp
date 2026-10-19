#include "functions.hpp"
extern "C" __attribute__((visibility("default"))) unsigned wa2_shared_aot_function_count(){
    return unsigned(wa2::aot::functions().size());
}
