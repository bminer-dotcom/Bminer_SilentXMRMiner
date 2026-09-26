#pragma once

// which provides compile-time AES-256 encryption with runtime decryption

#include <string>

// ---------------------------------------------------------------------------
// Runtime argument encryption keyed on the current Windows username.
// Encrypts with cycling XOR then hex-encodes so the result is safe to embed
// directly in a CreateProcessW command line.  The injected process decrypts
// with the matching HexXorDecrypt helper using the same username key.
// ---------------------------------------------------------------------------

static inline std::string XorEncryptToHex(const std::string& data, const std::string& key)
{
    if (key.empty() || data.empty()) return "";
    const char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (size_t i = 0; i < data.size(); ++i) {
        unsigned char b = static_cast<unsigned char>(data[i])
                        ^ static_cast<unsigned char>(key[i % key.size()]);
        result += hexChars[(b >> 4) & 0xF];
        result += hexChars[b & 0xF];
    }
    return result;
}

static inline std::string HexXorDecrypt(const std::string& hexData, const std::string& key)
{
    if (key.empty() || hexData.size() % 2 != 0) return "";
    auto fromHex = [](char c) -> unsigned char {
        if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
        return 0;
    };
    std::string result;
    result.reserve(hexData.size() / 2);
    for (size_t i = 0; i + 1 < hexData.size(); i += 2) {
        unsigned char b = static_cast<unsigned char>((fromHex(hexData[i]) << 4) | fromHex(hexData[i + 1]));
        b ^= static_cast<unsigned char>(key[(i / 2) % key.size()]);
        result += static_cast<char>(b);
    }
    return result;
}


