#pragma once
// Host ABI requirements. Guest serialization uses explicit 32-bit fields and
// must never be substituted for any of these native pointer-sized types.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
namespace wr {
static_assert(sizeof(void*)==8 && sizeof(std::uintptr_t)==8,"The Windows backend requires an x64 host");
static_assert(sizeof(HANDLE)==8 && sizeof(HWND)==8 && sizeof(HINSTANCE)==8);
static_assert(sizeof(WPARAM)==8 && sizeof(LPARAM)==8 && sizeof(LRESULT)==8);
static_assert(sizeof(LONG_PTR)==8 && sizeof(UINT_PTR)==8 && sizeof(SIZE_T)==8);
static_assert(sizeof(LONG)==4 && sizeof(DWORD)==4 && sizeof(BOOL)==4);
static_assert(sizeof(WNDCLASSEXA)==80 && sizeof(CREATESTRUCTA)==80);
static_assert(sizeof(MSG)==48 && sizeof(WINDOWPOS)==40 && sizeof(NCCALCSIZE_PARAMS)==56);
}
#endif
