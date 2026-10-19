#pragma once
#include <cstdint>
#include <cstddef>
// Serialized source layouts. These definitions never describe a native host
// thread, and dispatch never passes them to a Windows exception API.
namespace wr::port {
inline constexpr std::uint32_t WOW64_CONTEXT_ALL=0x1003f;
inline constexpr std::uint32_t EXCEPTION_NONCONTINUABLE=1,EXCEPTION_UNWINDING=2,EXCEPTION_EXIT_UNWIND=4;
inline constexpr std::uint32_t EXCEPTION_EXECUTE_HANDLER=1,EXCEPTION_CONTINUE_EXECUTION=0xffffffffu,EXCEPTION_MAXIMUM_PARAMETERS=15;
struct EXCEPTION_RECORD32 {
    std::uint32_t ExceptionCode,ExceptionFlags,ExceptionRecord,ExceptionAddress,NumberParameters,ExceptionInformation[15];
};
struct WOW64_CONTEXT {
    std::uint32_t ContextFlags,Dr0,Dr1,Dr2,Dr3,Dr6,Dr7;
    struct Floating {
        std::uint32_t ControlWord,StatusWord,TagWord,ErrorOffset,ErrorSelector,DataOffset,DataSelector;
        std::uint8_t RegisterArea[80];std::uint32_t Cr0NpxState;
    } FloatSave;
    std::uint32_t SegGs,SegFs,SegEs,SegDs,Edi,Esi,Ebx,Edx,Ecx,Eax,Ebp,Eip,SegCs,EFlags,Esp,SegSs;
    std::uint8_t ExtendedRegisters[512];
};
static_assert(sizeof(EXCEPTION_RECORD32)==80 && sizeof(WOW64_CONTEXT)==716);
static_assert(offsetof(WOW64_CONTEXT,Eax)==176 && offsetof(WOW64_CONTEXT,Esp)==196 && offsetof(WOW64_CONTEXT,ExtendedRegisters)==204);
}
