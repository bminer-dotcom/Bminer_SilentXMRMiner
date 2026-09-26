// bminer DVM_STR runtime stub (MinGW/MSVC compatible).
//
// Self-contained copy of the EagleVM DVM_STR runtime: locates the encrypted string
// directory the protector appends (via the DVMDIR01 directory locator), decrypts marked
// string bodies on first use with the rolling keystream, and caches them. The anti-debug
// portion of the upstream stub is intentionally omitted - it uses MSVC SEH and is not
// needed for string protection. Format is byte-identical to the EagleVM stub so the
// bvm.exe protector lays out its .dvmstr directory against the same structs.

#include <Windows.h>
#include <cstdint>
#include <cstdlib>

#include "dvm_string.h"

namespace
{
    // Directory locator: the protector scans for this tag and back-patches `dir_rva` with
    // the RVA of the encrypted string directory it appends, and flips `protected_flag` to 1.
    // Pre-protection protected_flag stays 0, so dvm_decrypt_string is a pure identity
    // passthrough and the program runs normally.
#pragma pack(push, 1)
    struct dvm_dir_locator_t
    {
        uint8_t signature[8]; // "DVMDIR01"
        uint32_t dir_rva;     // back-patched by protector
        uint32_t protected_flag;
    };
#pragma pack(pop)

    // one decrypted-string cache slot
    struct dvm_cache_slot
    {
        const char* cipher_addr; // image address of the encrypted body (the identity key)
        uint8_t* plain;          // heap buffer holding decrypted plaintext
        uint32_t size;
        uint32_t use_count;
    };

    constexpr uint32_t k_max_cached = 512;
    dvm_cache_slot g_dvm_cache[k_max_cached] = {};
    CRITICAL_SECTION g_dvm_lock;
    bool g_dvm_lock_ready = false;

    inline uint32_t dvm_rotl32(uint32_t v, int s)
    {
        s &= 31;
        return (v << s) | (v >> ((32 - s) & 31));
    }

    inline uint8_t dvm_keystream(uint32_t key, uint32_t i)
    {
        return static_cast<uint8_t>(dvm_rotl32(key, static_cast<int>(i)) + i);
    }

    void dvm_ensure_lock()
    {
        if (!g_dvm_lock_ready)
        {
            InitializeCriticalSection(&g_dvm_lock);
            g_dvm_lock_ready = true;
        }
    }
}

// signature the protector scans for to plant the directory RVA
extern "C" dvm_dir_locator_t __dvm_dir_locator = {
    { 'D', 'V', 'M', 'D', 'I', 'R', '0', '1' },
    0,
    0
};
#ifdef _MSC_VER
#pragma comment(linker, "/include:__dvm_dir_locator")
#endif

extern "C" const char* dvm_decrypt_string(const dvm_string_locator_t* locator)
{
    // unprotected build (or protector never ran): hand back the literal untouched.
    if (!__dvm_dir_locator.protected_flag)
        return locator->str;

    const auto image_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dir = reinterpret_cast<const dvm_string_directory_t*>(image_base + __dvm_dir_locator.dir_rva);
    if (dir->magic != DVM_STRING_MAGIC)
        return locator->str; // directory not where we expected - fail open to avoid a crash

    const uint32_t key = dir->key;
    const auto* entries = reinterpret_cast<const dvm_string_entry_t*>(
        reinterpret_cast<const uint8_t*>(dir) + sizeof(dvm_string_directory_t));

    // find the entry whose (decrypted) id matches this locator's id
    const uint32_t want_id = locator->id;
    const dvm_string_entry_t* found = nullptr;
    for (uint32_t i = 0; i < dir->count; i++)
    {
        if ((entries[i].id ^ key) == want_id)
        {
            found = &entries[i];
            break;
        }
    }
    if (!found)
        return locator->str;

    const uint32_t body_rva = found->rva ^ key;
    const uint32_t body_size = found->size ^ key;
    const char* cipher_addr = reinterpret_cast<const char*>(image_base + body_rva);

    dvm_ensure_lock();
    EnterCriticalSection(&g_dvm_lock);

    // already decrypted? bump refcount, return cached plaintext.
    int free_slot = -1;
    for (int i = 0; i < static_cast<int>(k_max_cached); i++)
    {
        if (g_dvm_cache[i].plain && g_dvm_cache[i].cipher_addr == cipher_addr)
        {
            g_dvm_cache[i].use_count++;
            const char* out = reinterpret_cast<const char*>(g_dvm_cache[i].plain);
            LeaveCriticalSection(&g_dvm_lock);
            return out;
        }
        if (free_slot < 0 && !g_dvm_cache[i].plain)
            free_slot = i;
    }

    if (free_slot < 0)
    {
        // cache full - fall back to leaving the ciphertext (fail open)
        LeaveCriticalSection(&g_dvm_lock);
        return locator->str;
    }

    auto* plain = static_cast<uint8_t*>(malloc(body_size));
    if (!plain)
    {
        LeaveCriticalSection(&g_dvm_lock);
        return locator->str;
    }

    for (uint32_t i = 0; i < body_size; i++)
        plain[i] = static_cast<uint8_t>(cipher_addr[i]) ^ dvm_keystream(key, i);

    g_dvm_cache[free_slot].cipher_addr = cipher_addr;
    g_dvm_cache[free_slot].plain = plain;
    g_dvm_cache[free_slot].size = body_size;
    g_dvm_cache[free_slot].use_count = 1;

    LeaveCriticalSection(&g_dvm_lock);
    return reinterpret_cast<const char*>(plain);
}

extern "C" bool dvm_free_string(const char* decrypted)
{
    if (!__dvm_dir_locator.protected_flag || !decrypted)
        return false;

    dvm_ensure_lock();
    EnterCriticalSection(&g_dvm_lock);

    for (int i = 0; i < static_cast<int>(k_max_cached); i++)
    {
        if (g_dvm_cache[i].plain == reinterpret_cast<const uint8_t*>(decrypted))
        {
            if (--g_dvm_cache[i].use_count == 0)
            {
                // secure-zero the plaintext before freeing so a later memory dump finds nothing.
                volatile uint8_t* p = g_dvm_cache[i].plain;
                for (uint32_t j = 0; j < g_dvm_cache[i].size; j++)
                    p[j] = 0;
                free(g_dvm_cache[i].plain);
                g_dvm_cache[i] = {};
            }
            LeaveCriticalSection(&g_dvm_lock);
            return true;
        }
    }

    LeaveCriticalSection(&g_dvm_lock);
    return false;
}
