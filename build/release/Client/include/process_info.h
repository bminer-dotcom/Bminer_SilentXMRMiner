#pragma once
#include <windows.h>
#include <map>
#include <mutex>
#include <optional>
#define PID_UTILS_H
class ProcessStorage {
public:
    // Add a process to storage
    static void AddProcess(DWORD pid, PROCESS_INFORMATION pi);

    // Remove a process from storage (closing its handles is the caller's job)
    static void RemoveProcess(DWORD pid);

    // Get process information (returns std::nullopt if not found)
    static std::optional<PROCESS_INFORMATION> GetProcess(DWORD pid);

    // Track the injected VEH block for a PID so it can be freed only once the
    // child exits (the VEH handler memory must outlive the child, so it is never
    // freed inside the injector itself).
    static void SetVehBlock(DWORD pid, PVOID base);
    // Returns and removes the VEH block for a PID (nullptr if none).
    static PVOID TakeVehBlock(DWORD pid);

private:
    static std::map<DWORD, PROCESS_INFORMATION> processes_;
    static std::map<DWORD, PVOID> veh_blocks_;
    static std::mutex mutex_;
};


bool IsPidRunning(DWORD pid);
