// toxcli — small command-line helper for the builder's Tox C2 workflow.
//
// Built against the vendored c-toxcore. It exists so the Qt builder never has
// to link toxcore itself (the builder and the client use different MinGW
// toolchains); all toxcore usage lives in this binary, which is compiled with
// the exact same toolchain as the client.
//
//   toxcli keygen [--name <bot-name>] [--out <file>]
//       Generates a fresh operator identity and a fresh bot identity and
//       prints a JSON object with their Tox IDs and savedata (hex).
//
//   toxcli send --operator-savedata <hex|file> --to <bot-address> --message <text>
//       Boots the operator identity, connects to the DHT, adds the bot as a
//       friend (no handshake, matching the bot's add_norequest) and delivers
//       a one-line message.
//
// Exit code 0 on success, 1 on error.

#include <tox/tox.h>

#include "../include/tox_bootstrap.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// ------------------------------------------------------------- hex utils ----
int hexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<uint8_t> HexToBytes(const std::string& hex)
{
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

// Escapes a string so it can be embedded verbatim in a JSON string literal.
// Discovered hellos can be full status JSON replies (which contain quotes),
// so naive concatenation produced invalid JSON the builder could not parse.
std::string JsonEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\b': o += "\\b";  break;
        case '\f': o += "\\f";  break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
            } else {
                o.push_back(static_cast<char>(c));
            }
        }
    }
    return o;
}

// Accepts a 76-hex Tox address or a 64-hex public key and yields the 32-byte
// public key that tox_friend_add_norequest needs. Bots found by Discover are
// only known by public key, so both forms must work or they can never be
// polled/pushed.
std::vector<uint8_t> AddressArgToPk(const std::string& arg)
{
    const std::vector<uint8_t> bytes = HexToBytes(arg);
    if (bytes.size() == TOX_ADDRESS_SIZE)
        return std::vector<uint8_t>(bytes.begin(), bytes.begin() + TOX_PUBLIC_KEY_SIZE);
    if (bytes.size() == TOX_PUBLIC_KEY_SIZE)
        return bytes;
    return std::vector<uint8_t>();
}

// Adds the peer (by public key) if not already on the list and returns its
// friend number. A savedata that already learned the peer would otherwise
// return ALREADY_SENT and abort the query; look the key up instead.
bool EnsureFriend(Tox* tox, const uint8_t* pk, Tox_Friend_Number& out)
{
    Tox_Err_Friend_Add addErr;
    const Tox_Friend_Number n = tox_friend_add_norequest(tox, pk, &addErr);
    if (addErr == TOX_ERR_FRIEND_ADD_OK && n != UINT32_MAX) {
        out = n;
        return true;
    }
    Tox_Err_Friend_By_Public_Key byErr;
    const uint32_t existing = tox_friend_by_public_key(tox, pk, &byErr);
    if (byErr == TOX_ERR_FRIEND_BY_PUBLIC_KEY_OK) {
        out = existing;
        return true;
    }
    return false;
}

// ------------------------------------------------------- bootstrap nodes ----
// Shared with tox_c2 in include/tox_bootstrap.h.

void Bootstrap(Tox* tox)
{
    for (const ToxBootstrapNode& n : kToxBootstrapNodes) {
        const std::vector<uint8_t> key = HexToBytes(n.keyHex);
        if (key.size() == TOX_PUBLIC_KEY_SIZE)
            tox_bootstrap(tox, n.ip, n.port, key.data(), nullptr);
    }
}

void SetName(Tox* tox, const std::string& name)
{
    if (!name.empty())
        tox_self_set_name(tox, reinterpret_cast<const uint8_t*>(name.c_str()),
                          name.size(), nullptr);
}

struct Identity {
    std::string id;         // 76-hex Tox address
    std::string publicKey;  // 64-hex
    std::string savedata;   // hex of the tox savedata blob
};

// Creates a fresh identity, optionally persisting a name into the savedata.
Identity GenerateIdentity(const std::string& name)
{
    Tox_Err_Options_New optErr;
    Tox_Options* options = tox_options_new(&optErr);
    tox_options_set_start_port(options, 33445);
    tox_options_set_end_port(options, 34445);

    Tox_Err_New newErr;
    Tox* tox = tox_new(options, &newErr);
    tox_options_free(options);
    if (!tox || newErr != TOX_ERR_NEW_OK)
        return Identity();

    SetName(tox, name);

    Tox_Address address;
    tox_self_get_address(tox, address);

    const size_t size = tox_get_savedata_size(tox);
    std::vector<uint8_t> blob(size);
    tox_get_savedata(tox, blob.data());

    Identity id;
    id.id = BytesToHex(address, TOX_ADDRESS_SIZE);
    id.publicKey = BytesToHex(address, TOX_PUBLIC_KEY_SIZE);
    id.savedata = BytesToHex(blob.data(), blob.size());

    tox_kill(tox);
    return id;
}

bool WriteFile(const std::string& path, const std::string& data)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
        return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.close();
    return true;
}

// Writes the (possibly updated) Tox savedata back to the file it was loaded
// from, so the operator's friend list persists across sessions. No-op when the
// savedata came from a raw hex argument instead of a file.
bool PersistSavedataFile(Tox* tox, const std::string& path)
{
    if (path.empty() || !tox)
        return false;
    const size_t size = tox_get_savedata_size(tox);
    if (size == 0)
        return false;
    std::vector<uint8_t> blob(size);
    tox_get_savedata(tox, blob.data());
    return WriteFile(path, std::string(reinterpret_cast<const char*>(blob.data()), blob.size()));
}

int DoKeygen(int argc, char** argv)
{
    std::string name = "bot";
    std::string out;

    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    }

    Identity bot = GenerateIdentity(name);
    Identity op = GenerateIdentity("operator");
    if (bot.id.empty() || op.id.empty()) {
        std::fprintf(stderr, "keygen: failed to create Tox identity\n");
        return 1;
    }

    std::ostringstream json;
    json << "{"
         << "\"operator\":{\"id\":\"" << op.id << "\","
         << "\"public_key\":\"" << op.publicKey << "\","
         << "\"savedata_hex\":\"" << op.savedata << "\"},"
         << "\"bot\":{\"id\":\"" << bot.id << "\","
         << "\"public_key\":\"" << bot.publicKey << "\","
         << "\"savedata_hex\":\"" << bot.savedata << "\"},"
         << "\"name\":\"" << name << "\"}";

    const std::string text = json.str();
    if (!out.empty()) {
        if (!WriteFile(out, text)) {
            std::fprintf(stderr, "keygen: cannot write %s\n", out.c_str());
            return 1;
        }
        std::printf("OK %s\n", out.c_str());
    } else {
        std::printf("%s\n", text.c_str());
    }
    return 0;
}

// ---------------------------------------------------------------- send ------
int DoSend(int argc, char** argv)
{
    std::string savedataArg;
    std::string toArg;
    std::string message;
    std::string messageFile;
    std::string name = "operator";

    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--operator-savedata") && i + 1 < argc) savedataArg = argv[++i];
        else if (!strcmp(argv[i], "--to") && i + 1 < argc) toArg = argv[++i];
        else if (!strcmp(argv[i], "--message") && i + 1 < argc) message = argv[++i];
        else if (!strcmp(argv[i], "--message-file") && i + 1 < argc) messageFile = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
    }

    if (savedataArg.empty() || toArg.empty() || (message.empty() && messageFile.empty())) {
        std::fprintf(stderr, "send: need --operator-savedata, --to and --message (or --message-file)\n");
        return 1;
    }

    if (!messageFile.empty()) {
        std::ifstream f(messageFile, std::ios::binary);
        if (!f.good()) {
            std::fprintf(stderr, "send: cannot read message file %s\n", messageFile.c_str());
            return 1;
        }
        message.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        if (message.empty()) {
            std::fprintf(stderr, "send: message file is empty\n");
            return 1;
        }
    }

    // Savedata may be a file path or raw hex.
    std::vector<uint8_t> savedata;
    std::string savedataFilePath;
    {
        std::ifstream f(savedataArg, std::ios::binary);
        if (f.good()) {
            savedata.assign(std::istreambuf_iterator<char>(f),
                            std::istreambuf_iterator<char>());
            if (!savedata.empty())
                savedataFilePath = savedataArg;
        }
        if (savedata.empty())
            savedata = HexToBytes(savedataArg);
    }
    if (savedata.empty()) {
        std::fprintf(stderr, "send: invalid operator savedata\n");
        return 1;
    }

    const std::vector<uint8_t> botPk = AddressArgToPk(toArg);
    if (botPk.size() != TOX_PUBLIC_KEY_SIZE) {
        std::fprintf(stderr, "send: invalid bot address/public key\n");
        return 1;
    }

    Tox_Err_Options_New optErr;
    Tox_Options* options = tox_options_new(&optErr);
    tox_options_set_start_port(options, 33445);
    tox_options_set_end_port(options, 34445);
    tox_options_set_savedata_type(options, TOX_SAVEDATA_TYPE_TOX_SAVE);
    tox_options_set_savedata_data(options, savedata.data(), savedata.size());

    Tox_Err_New newErr;
    Tox* tox = tox_new(options, &newErr);
    tox_options_free(options);
    if (!tox || newErr != TOX_ERR_NEW_OK) {
        std::fprintf(stderr, "send: cannot restore operator identity\n");
        return 1;
    }

    SetName(tox, name);
    Bootstrap(tox);

    const uint32_t friendsBefore = tox_self_get_friend_list_size(tox);
    Tox_Friend_Number friendNum = UINT32_MAX;
    if (!EnsureFriend(tox, botPk.data(), friendNum)) {
        std::fprintf(stderr, "send: cannot add bot as friend\n");
        tox_kill(tox);
        return 1;
    }

    bool sent = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (std::chrono::steady_clock::now() < deadline) {
        tox_iterate(tox, nullptr);

        if (!sent) {
            Tox_Err_Friend_Query qErr;
            const Tox_Connection status = tox_friend_get_connection_status(tox, friendNum, &qErr);
            if (status != TOX_CONNECTION_NONE) {
                Tox_Err_Friend_Send_Message mErr;
                tox_friend_send_message(tox, friendNum, TOX_MESSAGE_TYPE_NORMAL,
                                        reinterpret_cast<const uint8_t*>(message.data()),
                                        message.size(), &mErr);
                if (mErr == TOX_ERR_FRIEND_SEND_MESSAGE_OK) {
                    sent = true;
                    std::fprintf(stderr, "send: message delivered\n");
                } else {
                    std::fprintf(stderr, "send: delivery failed (err %d)\n", (int)mErr);
                }
            }
        }

        const uint32_t interval = tox_iteration_interval(tox);
        for (uint32_t slept = 0; slept < (interval ? interval : 10); slept += 10) {
            if (sent)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (sent) {
            // Flush the send queue for a short while before teardown.
            const auto flushEnd = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < flushEnd) {
                tox_iterate(tox, nullptr);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Only rewrite the savedata when a bot was actually added: toxcore
    // normalises the blob on every load, so an unconditional write would make
    // it look "changed" on every call.
    if (tox_self_get_friend_list_size(tox) > friendsBefore)
        PersistSavedataFile(tox, savedataFilePath);
    tox_kill(tox);
    if (!sent) {
        std::fprintf(stderr, "send: timed out waiting for the bot to come online\n");
        return 1;
    }
    return 0;
}

// --------------------------------------------------------------- query ------
namespace {
std::string g_queryReply;
bool g_queryGotReply = false;
void onQueryFriendMessage(Tox* /*tox*/, uint32_t /*friendNum*/, Tox_Message_Type type,
                          const uint8_t* message, size_t length, void* /*userData*/)
{
    if (type != TOX_MESSAGE_TYPE_NORMAL) return;
    // Bots also broadcast "hello <hash>" heartbeats. Ignore those so a stray
    // heartbeat arriving before the status reply cannot masquerade as it.
    std::string s(reinterpret_cast<const char*>(message), length);
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return;
    if (s.compare(first, 6, "hello ") == 0) return;
    g_queryReply.assign(reinterpret_cast<const char*>(message), length);
    g_queryGotReply = true;
}
} // anon

int DoQuery(int argc, char** argv)
{
    std::string savedataArg, toArg, message, messageFile, name = "operator";
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--operator-savedata") && i + 1 < argc) savedataArg = argv[++i];
        else if (!strcmp(argv[i], "--to") && i + 1 < argc) toArg = argv[++i];
        else if (!strcmp(argv[i], "--message") && i + 1 < argc) message = argv[++i];
        else if (!strcmp(argv[i], "--message-file") && i + 1 < argc) messageFile = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
    }
    if (savedataArg.empty() || toArg.empty() || (message.empty() && messageFile.empty())) {
        std::fprintf(stderr, "query: need --operator-savedata, --to and --message (or --message-file)\n");
        return 1;
    }
    if (!messageFile.empty()) {
        std::ifstream f(messageFile, std::ios::binary);
        if (!f.good()) { std::fprintf(stderr, "query: cannot read message file %s\n", messageFile.c_str()); return 1; }
        message.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        if (message.empty()) { std::fprintf(stderr, "query: message file is empty\n"); return 1; }
    }
    std::vector<uint8_t> savedata;
    std::string savedataFilePath;
    {
        std::ifstream f(savedataArg, std::ios::binary);
        if (f.good()) {
            savedata.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            if (!savedata.empty()) savedataFilePath = savedataArg;
        }
        if (savedata.empty()) savedata = HexToBytes(savedataArg);
    }
    if (savedata.empty()) { std::fprintf(stderr, "query: invalid operator savedata\n"); return 1; }
    const std::vector<uint8_t> botPk = AddressArgToPk(toArg);
    if (botPk.size() != TOX_PUBLIC_KEY_SIZE) { std::fprintf(stderr, "query: invalid bot address/public key\n"); return 1; }

    Tox_Options* options = tox_options_new(nullptr);
    tox_options_set_start_port(options, 33445);
    tox_options_set_end_port(options, 34445);
    tox_options_set_savedata_type(options, TOX_SAVEDATA_TYPE_TOX_SAVE);
    tox_options_set_savedata_data(options, savedata.data(), savedata.size());
    Tox* tox = tox_new(options, nullptr);
    tox_options_free(options);
    if (!tox) { std::fprintf(stderr, "query: cannot restore operator identity\n"); return 1; }
    SetName(tox, name);
    Bootstrap(tox);
    tox_callback_friend_message(tox, onQueryFriendMessage);
    const uint32_t friendsBefore = tox_self_get_friend_list_size(tox);
    Tox_Friend_Number friendNum = UINT32_MAX;
    if (!EnsureFriend(tox, botPk.data(), friendNum)) { std::fprintf(stderr, "query: cannot add bot as friend\n"); tox_kill(tox); return 1; }

    bool sent = false;
    g_queryReply.clear(); g_queryGotReply = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    const auto sendDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    bool replied = false;
    while (std::chrono::steady_clock::now() < deadline) {
        tox_iterate(tox, nullptr);
        if (!sent && std::chrono::steady_clock::now() < sendDeadline) {
            Tox_Err_Friend_Query qErr;
            const Tox_Connection status = tox_friend_get_connection_status(tox, friendNum, &qErr);
            if (status != TOX_CONNECTION_NONE) {
                Tox_Err_Friend_Send_Message mErr;
                tox_friend_send_message(tox, friendNum, TOX_MESSAGE_TYPE_NORMAL,
                                        reinterpret_cast<const uint8_t*>(message.data()), message.size(), &mErr);
                if (mErr == TOX_ERR_FRIEND_SEND_MESSAGE_OK) {
                    sent = true;
                    std::fprintf(stderr, "query: message delivered, waiting for reply...\n");
                }
            }
        }
        if (g_queryGotReply) { replied = true; break; }
        const uint32_t interval = tox_iteration_interval(tox);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval ? interval : 20));
    }
    // Only rewrite when a bot was added (see DoSend for rationale).
    if (tox_self_get_friend_list_size(tox) > friendsBefore)
        PersistSavedataFile(tox, savedataFilePath);
    tox_kill(tox);
    if (!sent) { std::fprintf(stderr, "query: timed out waiting for the bot to come online\n"); return 1; }
    if (!replied) { std::fprintf(stderr, "query: sent but no reply within timeout\n"); return 1; }
    std::printf("%s\n", g_queryReply.c_str());
    return 0;
}

// -------------------------------------------------------------- listen -----
namespace {
std::vector<std::string> g_listenHellos;
void onListenFriendRequest(Tox* tox, const Tox_Public_Key pk, const uint8_t* msg, size_t len, void* /*ud*/) {
    Tox_Err_Friend_Add e; tox_friend_add_norequest(tox, pk, &e);
    std::string s; if (msg && len) s.assign(reinterpret_cast<const char*>(msg), len);
    std::string pkHex = BytesToHex(pk, TOX_PUBLIC_KEY_SIZE);
    if (!s.empty()) g_listenHellos.push_back(pkHex + ":" + s);
    else g_listenHellos.push_back(pkHex + ":hello");
}
void onListenFriendMessage(Tox* tox, uint32_t fn, Tox_Message_Type t, const uint8_t* m, size_t l, void* /*ud*/) {
    if (t != TOX_MESSAGE_TYPE_NORMAL) return;
    std::string s(reinterpret_cast<const char*>(m), l);
    // Capture sender PK for hash-primary mapping
    Tox_Public_Key pk; Tox_Err_Friend_Get_Public_Key err;
    std::string pkHex;
    if (tox_friend_get_public_key(tox, fn, pk, &err) && err==TOX_ERR_FRIEND_GET_PUBLIC_KEY_OK) pkHex = BytesToHex(pk, TOX_PUBLIC_KEY_SIZE);
    std::string entry = pkHex.empty() ? s : pkHex + ":" + s;
    // Only discovery-relevant traffic: hello/pong greetings or a status JSON
    // reply. Everything else (config acks, errors) is noise and would pollute
    // the colony with bogus entries.
    if (s.rfind("hello ",0)==0 || s.rfind("pong ",0)==0) g_listenHellos.push_back(entry);
    else if (s.find("\"device_hash\"")!=std::string::npos) g_listenHellos.push_back(entry);
}
}
int DoListen(int argc, char** argv) {
    std::string savedataArg, out; int secs=30; std::string name="operator";
    for (int i=2;i<argc;++i) {
        if (!strcmp(argv[i],"--operator-savedata")&&i+1<argc) savedataArg=argv[++i];
        else if (!strcmp(argv[i],"--seconds")&&i+1<argc) secs=atoi(argv[++i]);
        else if (!strcmp(argv[i],"--out")&&i+1<argc) out=argv[++i];
        else if (!strcmp(argv[i],"--name")&&i+1<argc) name=argv[++i];
    }
    if (savedataArg.empty()) { std::fprintf(stderr,"listen: need --operator-savedata\n"); return 1; }
    g_listenHellos.clear();
    std::vector<uint8_t> savedata;
    std::string savedataFilePath;
    { std::ifstream f(savedataArg,std::ios::binary); if(f.good()) { savedata.assign(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()); if(!savedata.empty()) savedataFilePath=savedataArg; } if(savedata.empty()) savedata=HexToBytes(savedataArg); }
    if (savedata.empty()) { std::fprintf(stderr,"listen: invalid operator savedata\n"); return 1; }
    Tox_Options* o=tox_options_new(nullptr); tox_options_set_start_port(o,33445); tox_options_set_end_port(o,34445);
    tox_options_set_savedata_type(o,TOX_SAVEDATA_TYPE_TOX_SAVE); tox_options_set_savedata_data(o,savedata.data(),savedata.size());
    Tox* tox=tox_new(o,nullptr); tox_options_free(o); if(!tox) { std::fprintf(stderr,"listen: cannot restore operator\n"); return 1; }
    SetName(tox,name); Bootstrap(tox);
    tox_callback_friend_request(tox,onListenFriendRequest);
    tox_callback_friend_message(tox,onListenFriendMessage);
    const uint32_t friendsBefore = tox_self_get_friend_list_size(tox);
    std::fprintf(stderr,"listen: waiting %d seconds for hellos...\n",secs);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(secs);
    while(std::chrono::steady_clock::now()<deadline) { tox_iterate(tox,nullptr); std::this_thread::sleep_for(std::chrono::milliseconds(tox_iteration_interval(tox)?tox_iteration_interval(tox):20)); }
    // Remember every bot discovered in this listen window on the operator
    // machine. Only rewrite when the friend list actually grew — toxcore
    // normalises the blob on each load, so writing unconditionally would make
    // it look changed on every listen.
    if (tox_self_get_friend_list_size(tox) > friendsBefore)
        PersistSavedataFile(tox, savedataFilePath);
    tox_kill(tox);
    std::sort(g_listenHellos.begin(), g_listenHellos.end()); g_listenHellos.erase(std::unique(g_listenHellos.begin(),g_listenHellos.end()),g_listenHellos.end());
    std::string json="[";
    for(size_t i=0;i<g_listenHellos.size();++i){ if(i) json+=","; json+="\""+JsonEscape(g_listenHellos[i])+"\""; } json+="]";
    if(!out.empty()){ std::ofstream f(out); f<<json; } else std::printf("%s\n",json.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: toxcli keygen|send|query|listen [options]\n");
        return 1;
    }
    if (!strcmp(argv[1], "keygen"))
        return DoKeygen(argc, argv);
    if (!strcmp(argv[1], "send"))
        return DoSend(argc, argv);
    if (!strcmp(argv[1], "query"))
        return DoQuery(argc, argv);
    if (!strcmp(argv[1], "listen"))
        return DoListen(argc, argv);
    std::fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 1;
}
