/*
 * msvc_compat.h - force-included (-include) shim that lets the MSVC-only
 * MultiCAD sources build with the MinGW-w64 GCC cross toolchain on Linux.
 *
 * This file, and everything else under mingw/, is an unofficial Linux build
 * path. It is deliberately kept out of the Visual Studio project.
 */
#pragma once

#if defined(__GNUC__) && !defined(_MSC_VER)

/* --------------------------------------------------------------------------
 * Standard names that MSVC's <windows.h> / STL headers expose transitively
 * (so the sources use `uint8_t`, `size_t`, `std::memcpy`, `std::strlen`
 * without including <cstdint>/<cstring>) but libstdc++ does not leak.
 * <crtdbg.h> ships no-op stubs in MinGW (_CrtSetDbgFlag -> (int)0), which
 * covers the debug-CRT calls in main.cpp / DllMain.
 * ------------------------------------------------------------------------ */
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <crtdbg.h>

/* --------------------------------------------------------------------------
 * Calling-convention function *type* spelling.
 *
 * The patch engine writes `getFn<RET(__stdcall)(ARGS)>(addr)` /
 * `getPtr<RET(__cdecl)(ARGS)>(addr)`. MSVC accepts a bare calling-convention
 * token in that abstract-declarator slot; GCC cannot parse it there.
 * prep.py rewrites those call sites to
 *     getFn<mscc::stdcall_<RET(ARGS)>::type>(addr)
 * using the aliases below, where GCC's trailing-attribute form is legal.
 * Pointer forms - `RET(__thiscall* f)(ARGS)` - already parse under GCC and
 * are left untouched; __cdecl / __stdcall / __thiscall / __fastcall keep
 * their normal MinGW attribute meaning everywhere else.
 * ------------------------------------------------------------------------ */
#include <_mingw.h>
namespace mscc {
    template<class T> struct cdecl_;
    template<class R, class... A> struct cdecl_<R(A...)>    { typedef R type(A...) __attribute__((__cdecl__)); };
    template<class T> struct stdcall_;
    template<class R, class... A> struct stdcall_<R(A...)>  { typedef R type(A...) __attribute__((__stdcall__)); };
    template<class T> struct thiscall_;
    template<class R, class... A> struct thiscall_<R(A...)> { typedef R type(A...) __attribute__((__thiscall__)); };
    template<class T> struct fastcall_;
    template<class R, class... A> struct fastcall_<R(A...)> { typedef R type(A...) __attribute__((__fastcall__)); };
}

/* MSVC predefined macro referenced inside a #pragma comment(linker,"/EXPORT")
 * in core/cad.cpp that GCC ignores anyway - keep the token defined. */
#ifndef __FUNCDNAME__
#  define __FUNCDNAME__ __func__
#endif

#endif /* __GNUC__ && !_MSC_VER */
