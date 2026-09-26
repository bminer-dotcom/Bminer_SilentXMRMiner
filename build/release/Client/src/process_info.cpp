#include "../include/process_info.h"
#include <iostream>

std::map<DWORD, PROCESS_INFORMATION> ProcessStorage::processes_;
std::map<DWORD, PVOID> ProcessStorage::veh_blocks_;
std::mutex ProcessStorage::mutex_;

void ProcessStorage::AddProcess(DWORD pid, PROCESS_INFORMATION pi) {
    std::lock_guard<std::mutex> lock(mutex_);
    processes_[pid] = pi;
}

void ProcessStorage::RemoveProcess(DWORD pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = processes_.find(pid);
    if (it != processes_.end()) {
        processes_.erase(it);
    }
}

std::optional<PROCESS_INFORMATION> ProcessStorage::GetProcess(DWORD pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = processes_.find(pid);
    if (it != processes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ProcessStorage::SetVehBlock(DWORD pid, PVOID base) {
    std::lock_guard<std::mutex> lock(mutex_);
    veh_blocks_[pid] = base;
}

PVOID ProcessStorage::TakeVehBlock(DWORD pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = veh_blocks_.find(pid);
    if (it == veh_blocks_.end()) return nullptr;
    PVOID base = it->second;
    veh_blocks_.erase(it);
    return base;
}
