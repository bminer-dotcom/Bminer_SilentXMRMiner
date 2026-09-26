#pragma once

#include "dvm_string.h"

// DVM_STR string protection for the bminer client. Compiler-agnostic (works with both
// MSVC and MinGW). Before the bvm.exe protector runs, dvm_decrypt_string is an identity
// passthrough (protected_flag == 0) so the program runs normally. After protection, the
// marked string bodies are ciphertext in the image and decrypt on first use.
//
// The /include: link directive is MSVC-only; MinGW links the stub automatically because
// DVM_STR references dvm_decrypt_string and the stub's directory locator is a global.

#ifdef _MSC_VER
#pragma comment(linker, "/include:dvm_decrypt_string")
#endif

extern "C" const char* dvm_decrypt_string(const dvm_string_locator_t* locator);
extern "C" bool dvm_free_string(const char* decrypted);

// DVM_STR("secret") - marks a string literal for protection and evaluates to a readable
// C string pointer at runtime. Each use site emits its own static locator record into
// .rdata carrying a scan tag and a linker-filled pointer to the literal; the protector
// finds the tag, encrypts the literal in place, records it in the directory section, and
// back-patches the locator's id/size. Pair with DVM_FREE(p) to wipe the plaintext when
// done. Usable anywhere a const char* is expected.
#define DVM_STR(literal)                                                                                                                            \
    ([]() -> const char* {                                                                                                                          \
        static const char __dvm_lit[] = literal;                                                                                                    \
        static dvm_string_locator_t __dvm_loc = { { 'D', 'V', 'M', 'S', 'T', 'R', '0', '1' }, __dvm_lit, 0, sizeof(__dvm_lit) };                    \
        return dvm_decrypt_string(&__dvm_loc);                                                                                                       \
    }())

#define DVM_FREE(decrypted) dvm_free_string(decrypted)
