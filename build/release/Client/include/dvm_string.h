#pragma once
#include <cstdint>

// Shared on-disk / in-memory format for DVM_STR string protection. Included by BOTH the
// target program (via the DVM_STR macro below) and the protector (which reads these same
// structs to lay out the encrypted directory). Keep this file dependency-free and packed
// so both sides agree byte-for-byte. Copy of the EagleVM stub format (dvm_string.h).

#pragma pack(push, 1)

// One of these is emitted into the target's .rdata for every DVM_STR("...") use. The
// protector scans for `signature`, follows the linker-filled `str` pointer to the literal,
// encrypts it, and back-patches `id`/`size` so the runtime can find the right directory
// slot.
struct dvm_string_locator_t
{
    uint8_t signature[8];   // "DVMSTR01" - the tag the protector scans for
    const char* str;        // linker-filled pointer to the marked string literal
    uint32_t id;            // directory slot, back-patched by the protector (0 pre-protect)
    uint32_t size;          // byte length including the terminating NUL, back-patched
};

// Directory of all protected strings, appended by the protector as its own PE section.
struct dvm_string_directory_t
{
    uint32_t magic;         // DVM_STRING_MAGIC, so the runtime can sanity-check it found it
    uint32_t count;         // number of entries
    uint32_t key;           // per-build random key for the rolling-XOR keystream
    uint32_t reserved;
};

struct dvm_string_entry_t
{
    uint32_t id;            // matches the locator id (XORed with key on disk)
    uint32_t rva;           // RVA of the encrypted body in the image (XORed with key on disk)
    uint32_t size;          // body length incl NUL (XORed with key on disk)
    uint32_t reserved;
};

#pragma pack(pop)

static constexpr uint8_t DVM_STRING_SIGNATURE[8] = { 'D', 'V', 'M', 'S', 'T', 'R', '0', '1' };
static constexpr uint32_t DVM_STRING_MAGIC = 0x52545344; // 'DSTR'
