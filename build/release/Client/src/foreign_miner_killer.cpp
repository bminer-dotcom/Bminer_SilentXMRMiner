#include "../include/foreign_miner_killer.h"

#include "../include/dvm_str.h"
#include "../include/ntddk.h"

#include <iphlpapi.h>

#include <algorithm>
#include <cctype>
#include <codecvt>
#include <cstdlib>
#include <cwchar>
#include <locale>
#include <string>
#include <unordered_set>
#include <vector>

#include <tlhelp32.h>


namespace ForeignMinerKiller {

namespace {

// ---------------------------------------------------------------- globals --
HANDLE g_mutex = nullptr;

// Processes we already suspended. Suspending (not killing) keeps the process in
// the task list, so a watchdog that relaunches on process death sees it alive
// and never spawns a replacement; the miner thread is frozen and cannot mine.
// We track pid+exe so a recycled PID running a different binary is not skipped.
std::unordered_set<std::string> g_suspendedKeys;

// ASCII-lowercase a wide string (process names / Windows paths here are ASCII).
std::wstring LowerAsciiW(const std::wstring& in)
{
    std::wstring out = in;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c; });
    return out;
}

// Stable key for a suspended process (pid + lowercased exe name).
std::string SuspensionKey(DWORD pid, const std::wstring& exe)
{
    const std::wstring lower = LowerAsciiW(exe);
    std::string narrow(lower.begin(), lower.end());
    return std::to_string(pid) + "\x1f" + narrow;
}

// Drop cache entries for processes that have since exited (their PIDs may be
// recycled). Call once per sweep before detection.
void PruneSuspendedCache()
{
    for (auto it = g_suspendedKeys.begin(); it != g_suspendedKeys.end();) {
        const DWORD pid = static_cast<DWORD>(std::strtoul(it->c_str(), nullptr, 10));
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) {
            it = g_suspendedKeys.erase(it);
            continue;
        }
        CloseHandle(h);
        ++it;
    }
}

// ------------------------------------------------------------ detection ----
// Stratum pool ports in host byte order.
bool IsStratumPort(unsigned short port)
{
    switch (port) {
    case 3333: case 4444: case 5555: case 7777:
    case 14444: case 18081: case 2082: case 2086: case 14433:
        return true;
    default:
        return false;
    }
}

struct TcpRowOwnerPid {
    DWORD dwState;
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
    DWORD dwRemoteAddr;
    DWORD dwRemotePort;
    DWORD dwOwningPid;
};
struct TcpTableOwnerPid {
    DWORD dwNumEntries;
    TcpRowOwnerPid table[1];
};

typedef DWORD(WINAPI* PFN_GetExtendedTcpTable)(PVOID, PDWORD, BOOL, ULONG, DWORD, ULONG);

// PIDs with an outbound TCP connection to a stratum port. Loopback peers are
// skipped (a local dev tool talking to 127.0.0.1:5555 is not a miner).
std::unordered_set<DWORD> CollectStratumPids()
{
    std::unordered_set<DWORD> pids;

    static PFN_GetExtendedTcpTable pfn = nullptr;
    if (!pfn)
        pfn = (PFN_GetExtendedTcpTable)GetExtendedTcpTable;
    if (!pfn)
        return pids;

    // 2 = AF_INET, 5 = TCP_TABLE_OWNER_PID_ALL. Written as literals: AF_INET is
    // a winsock macro already defined by <windows.h>, so a local constant named
    // AF_INET would not compile.
    ULONG size = 0;
    pfn(nullptr, &size, FALSE, 2, 5, 0);
    if (size == 0)
        return pids;

    std::vector<BYTE> buf(size);
    auto* table = reinterpret_cast<TcpTableOwnerPid*>(buf.data());
    if (pfn(table, &size, FALSE, 2, 5, 0) != NO_ERROR)
        return pids;

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const TcpRowOwnerPid& row = table->table[i];

        // dwRemotePort is in network byte order in the low 16 bits.
        unsigned short port = static_cast<unsigned short>(
            ((row.dwRemotePort & 0xFF) << 8) | ((row.dwRemotePort >> 8) & 0xFF));
        if (!IsStratumPort(port))
            continue;

        // dwRemoteAddr is in network byte order, so the first octet sits in the
        // low byte of the little-endian value. 127.* is loopback — skip it.
        if ((row.dwRemoteAddr & 0xFF) == 127)
            continue;

        pids.insert(row.dwOwningPid);
    }
    return pids;
}

// ------------------------------------------------------------ whitelist ----
typedef BOOL(WINAPI* PFN_QueryFullProcessImageNameW)(HANDLE, DWORD, LPWSTR, PDWORD);

std::wstring GetImagePath(DWORD pid)
{
    static PFN_QueryFullProcessImageNameW pfn = nullptr;
    if (!pfn)
        pfn = (PFN_QueryFullProcessImageNameW)QueryFullProcessImageNameW;
    if (!pfn)
        return std::wstring();

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return std::wstring();

    wchar_t buf[512] = {0};
    DWORD sz = 512;
    BOOL ok = pfn(h, 0, buf, &sz);
    CloseHandle(h);
    return ok ? std::wstring(buf) : std::wstring();
}

// Legitimate Windows / installed-software locations. A miner would have to be
// dropped there deliberately; cryptominers live in %APPDATA%, %TEMP%, %ProgramData%.
// Case-insensitive ASCII prefix match, boundary-checked on a directory edge.
bool IsTrustedSystemPath(const std::wstring& path)
{
    static const wchar_t* prefixes[] = {
        L"c:\\windows\\system32",
        L"c:\\windows\\syswow64",
        L"c:\\program files",
        L"c:\\program files (x86)",
    };

    for (const wchar_t* p : prefixes) {
        const size_t n = wcslen(p);
        if (path.size() < n)
            continue;

        bool match = true;
        for (size_t i = 0; i < n; ++i) {
            wchar_t a = path[i];
            wchar_t b = p[i];
            if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a + 32);
            if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b + 32);
            if (a != b) { match = false; break; }
        }
        if (match && (path.size() == n || path[n] == L'\\'))
            return true;
    }
    return false;
}

// Every process running the exact same client image as us. Miners spawned by
// any of them (our own instance, or a peer instance that is passive because it
// lost the colony mutex) are ours and must never be neutralized.
std::unordered_set<DWORD> CollectColonyClientPids(const std::wstring& selfPath)
{
    std::unordered_set<DWORD> pids;
    if (selfPath.empty())
        return pids;

    const std::wstring selfLower = LowerAsciiW(selfPath);
    const size_t slash = selfLower.find_last_of(L"\\/");
    const std::wstring selfName =
        slash == std::wstring::npos ? selfLower : selfLower.substr(slash + 1);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return pids;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            const DWORD pid = pe.th32ProcessID;
            if (pid <= 4)
                continue;
            // Cheap name pre-filter, then confirm by full image path so an
            // unrelated binary that merely shares our name is not trusted.
            if (LowerAsciiW(pe.szExeFile) != selfName)
                continue;
            if (LowerAsciiW(GetImagePath(pid)) == selfLower)
                pids.insert(pid);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

} // namespace

// ------------------------------------------------------------------- API --
bool Initialize(const std::string& deviceHash)
{
    typedef HANDLE(WINAPI* PFN_CreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
    static PFN_CreateMutexA pfn = nullptr;
    if (!pfn)
        pfn = (PFN_CreateMutexA)CreateMutexA;
    if (!pfn)
        return false;

    const char* kFmkPrefix = DVM_STR("Global\\BminerFMK_");
    const std::string name = std::string(kFmkPrefix) + deviceHash;
    DVM_FREE(kFmkPrefix);

    HANDLE m = pfn(nullptr, TRUE, name.c_str());
    if (!m)
        return false;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        return false;   // another instance owns the colony — stay passive
    }

    g_mutex = m;
    return true;
}

int ScanAndKill(const std::vector<DWORD>& ownPids,
                const std::vector<std::string>& watchedProcesses)
{
    if (!g_mutex)
        return 0;

    PruneSuspendedCache();

    // PIDs that must never be killed.
    std::unordered_set<DWORD> excluded;
    for (DWORD p : ownPids)
        excluded.insert(p);
    excluded.insert(GetCurrentProcessId());

    // Children of any process running our client image are colony miners —
    // including a peer instance that is passive because it lost the mutex.
    // This is what stops two clients from suspending each other's miners.
    const std::wstring selfPath = GetImagePath(GetCurrentProcessId());
    std::unordered_set<DWORD> colonyClientPids = CollectColonyClientPids(selfPath);
    colonyClientPids.insert(GetCurrentProcessId());

    // Only processes with a live stratum connection are candidates.
    const std::unordered_set<DWORD> netPids = CollectStratumPids();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(snap, &pe)) {
        CloseHandle(snap);
        return 0;
    }

    int neutralized = 0;

    do {
        const DWORD pid = pe.th32ProcessID;
        if (pid <= 4 || netPids.count(pid) == 0 || excluded.count(pid) != 0)
            continue;

        // Spawned by one of our clients (self or a peer instance) -> our miner.
        if (colonyClientPids.count(pe.th32ParentProcessID) != 0)
            continue;

        const std::wstring exePath = GetImagePath(pid);
        const std::wstring exeName(pe.szExeFile);

        // Already suspended on a previous sweep — skip so the suspend count
        // does not stack up (a stack of N suspends needs N resumes to thaw).
        const std::string key = SuspensionKey(pid, exeName);
        if (g_suspendedKeys.count(key) != 0)
            continue;

        const int wlen = lstrlenW(pe.szExeFile);
        int needed = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, wlen, NULL, 0, NULL, NULL);
        std::string name(needed > 0 ? (size_t)needed : 0, '\0');
        if (needed > 0)
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, wlen, &name[0], needed, NULL, NULL);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });

        // Exact-name match (case-insensitive). Substring matching was too
        // loose: a short entry like "svc" would spare any process whose name
        // merely contained it. Entries may be written with or without ".exe".
        const bool hasExeSuffix =
            name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0;
        bool watched = false;
        for (const auto& w : watchedProcesses) {
            std::string wl = w;
            std::transform(wl.begin(), wl.end(), wl.begin(),
                           [](unsigned char c) { return static_cast<char>(::tolower(c)); });
            if (wl.empty())
                continue;
            if (name == wl) {
                watched = true;
                break;
            }
            if (hasExeSuffix &&
                name.compare(0, name.size() - 4, wl) == 0) {
                watched = true;
                break;
            }
        }
        if (watched)
            continue;

        // Not a legitimate Windows / installed-software process -> treat as miner.
        if (IsTrustedSystemPath(exePath))
            continue;

        // Re-check the exclusion set immediately before neutralizing.
        if (excluded.count(pid) != 0)
            continue;

        // Suspend is preferred over kill: a suspended process stays in the
        // system's process list, so a watchdog that restarts a dead miner
        // believes it is still running and never spawns a replacement, while
        // the miner's threads are frozen and it cannot mine. A process that
        // denies suspend access is a fallback kill victim.
        HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) {
            h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (h) {
                if (TerminateProcess(h, 1))
                    ++neutralized;
                CloseHandle(h);
            }
            continue;
        }

        const NTSTATUS st = NtSuspendProcess(h);
        if (st >= 0) {
            g_suspendedKeys.insert(key);
            ++neutralized;
        } else {
            // Suspend failed (denied / process tearing down): fall back to kill.
            CloseHandle(h);
            h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (h) {
                if (TerminateProcess(h, 1))
                    ++neutralized;
            }
        }
        if (h)
            CloseHandle(h);
    } while (Process32NextW(snap, &pe));

    CloseHandle(snap);
    return neutralized;
}

} // namespace ForeignMinerKiller
