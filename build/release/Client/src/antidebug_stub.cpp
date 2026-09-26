// DVM anti-debug gate stub (MinGW/GCC compatible).
//
// bvm.exe --anti-debug scans .rdata for the ADBGGATE locator and injects a
// call to the gate at every VM section entry point. Without this stub in the
// binary the flag does nothing.
//
// MinGW does not support MSVC SEH (__try/__except), so the NtClose invalid-
// handle trick is implemented with AddVectoredExceptionHandler instead.

#include <windows.h>
#include <cstdint>
#include <cstdlib>

namespace
{
    constexpr wchar_t k_relaunch_env_var[] = L"__DVM_ADBG_PPID";

    // ---- NtClose invalid-handle trick via VEH ----------------------------
    static volatile bool s_veh_caught = false;

    static LONG CALLBACK veh_handler(EXCEPTION_POINTERS* ep)
    {
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_INVALID_HANDLE)
        {
            s_veh_caught = true;
            ep->ContextRecord->Rip += 2; // skip the syscall (2 bytes: 0F 05)
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    bool check_ntclose_trick()
    {
        using NtClose_t = LONG(__stdcall*)(HANDLE);
        const auto ntdll   = GetModuleHandleW(L"ntdll.dll");
        const auto NtClose = ntdll
            ? reinterpret_cast<NtClose_t>(GetProcAddress(ntdll, "NtClose"))
            : nullptr;
        if (!NtClose)
            return false;

        s_veh_caught = false;
        const auto h = AddVectoredExceptionHandler(1, veh_handler);
        NtClose(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0xDEADBEEF)));
        if (h) RemoveVectoredExceptionHandler(h);
        return s_veh_caught;
    }

    // ---- Self-debug spawn trick ------------------------------------------
    bool check_self_debug_trick()
    {
        wchar_t self_path[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, self_path, MAX_PATH))
            return false;

        wchar_t ppid_value[16];
        wsprintfW(ppid_value, L"%lu", GetCurrentProcessId());
        SetEnvironmentVariableW(k_relaunch_env_var, ppid_value);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        const BOOL created = CreateProcessW(self_path, self_path, nullptr, nullptr,
            FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

        SetEnvironmentVariableW(k_relaunch_env_var, nullptr);

        if (!created)
            return false;

        bool detected = false;
        if (WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0)
        {
            DWORD exit_code = 0;
            if (GetExitCodeProcess(pi.hProcess, &exit_code) && exit_code != 0)
                detected = true;
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return detected;
    }

    // ---- Early-init: runs in the child spawned by check_self_debug_trick --
    __attribute__((constructor))
    void early_init()
    {
        wchar_t ppid_str[32] = {};
        const DWORD len = GetEnvironmentVariableW(k_relaunch_env_var, ppid_str, 32);
        if (len == 0 || len >= 32)
            return;

        const DWORD parent_pid = wcstoul(ppid_str, nullptr, 10);
        const BOOL attached = DebugActiveProcess(parent_pid);
        if (attached)
            DebugActiveProcessStop(parent_pid);

        ExitProcess(attached ? 0 : 1);
    }
}

extern "C" __attribute__((noinline)) void __dvm_antidebug_gate()
{
    static bool already_checked = false;
    if (already_checked)
        return;
    already_checked = true;

    int signals = 0;
    if (check_ntclose_trick())    signals++;
    if (check_self_debug_trick()) signals++;

    if (signals >= 2)
        WaitForSingleObject(GetCurrentThread(), INFINITE);
}

#pragma pack(push, 1)
struct antidebug_locator_t
{
    uint8_t  signature[8];
    void   (*gate)();
};
#pragma pack(pop)

// ADBGGATE tag + gate pointer in .rdata — bvm scans for this.
extern "C" const antidebug_locator_t __dvm_antidebug_locator = {
    { 'A', 'D', 'B', 'G', 'G', 'A', 'T', 'E' },
    __dvm_antidebug_gate
};
