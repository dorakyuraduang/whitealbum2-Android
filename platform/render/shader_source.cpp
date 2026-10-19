#include "shader_source.hpp"
#include <stdexcept>
namespace wa2::graphics {
std::string alpha_test_source(std::string source){
    const auto main=source.find("void main()");if(main==source.npos || source.find("ps_oC0")==source.npos || source.find("wa2_original_shader_main")!=source.npos)
        throw std::invalid_argument("unexpected translated pixel shader structure");
    source.replace(main,11,"void wa2_original_shader_main()");
    source.insert(main,"uniform int wa2_alpha_mode;\nuniform float wa2_alpha_reference;\n");
    source+=R"(
void main() {
    wa2_original_shader_main();
    float wa2_a = ps_oC0.a;
    bool wa2_pass = wa2_alpha_mode == 0 || wa2_alpha_mode == 8 ||
        (wa2_alpha_mode == 2 && wa2_a < wa2_alpha_reference) ||
        (wa2_alpha_mode == 3 && wa2_a == wa2_alpha_reference) ||
        (wa2_alpha_mode == 4 && wa2_a <= wa2_alpha_reference) ||
        (wa2_alpha_mode == 5 && wa2_a > wa2_alpha_reference) ||
        (wa2_alpha_mode == 6 && wa2_a != wa2_alpha_reference) ||
        (wa2_alpha_mode == 7 && wa2_a >= wa2_alpha_reference);
    if (!wa2_pass) discard;
}
)";
    return source;
}
}
