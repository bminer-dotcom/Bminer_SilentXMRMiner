#pragma once

#include <windows.h>

WORD get_pe_architecture(const BYTE *pe_buffer);

DWORD get_entry_point_rva(const BYTE *pe_buffer);

ULONGLONG get_image_base(const BYTE *pe_buffer);

bool pe_is_virtualized(const BYTE *pe_buffer);

bool pe_is64bit(IN const BYTE *pe_buffer);
