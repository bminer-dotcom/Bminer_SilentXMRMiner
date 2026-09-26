#pragma once

#include <string>
#include <vector>
#include <windows.h>

/*
 * Foreign Miner Killer.
 *
 * Neutralizes cryptominers that are not ours, so the colony does not have to
 * share CPU/GPU. Safety is the whole point of this module:
 *   - A named mutex (derived from the device hash) marks ownership; when
 *     another instance already holds it, this killer stays passive. In
 *     addition, any candidate whose parent process runs our own client image
 *     is treated as ours, so two clients can never neutralize each other's
 *     miners even though only one of them owns the killer.
 *   - Every candidate is checked against an exclusion set (our own PIDs +
 *     the panel's watched-process list) immediately before action.
 *
 * Neutralization is suspend-then-kill: a foreign miner is suspended
 * (NtSuspendProcess) rather than terminated. A suspended process stays in
 * the system's process list, so a watchdog that relaunches a dead miner sees
 * it still running and never spawns a replacement, while the miner's threads
 * are frozen and cannot mine. Kill is only a fallback when the process denies
 * suspend access.
 *
 * Detection is deliberately simple: a process with an outbound TCP connection
 * to a known stratum port is a candidate, and it is acted upon unless it is
 * one of ours, a watched process, or a legitimate Windows / installed-software
 * binary (System32, SysWOW64, Program Files). Loopback peers are ignored, so
 * local dev tools are left alone.
 */
namespace ForeignMinerKiller {

// Creates the ownership mutex. Returns false when another instance already
// owns it; in that case ScanAndKill() is a no-op.
bool Initialize(const std::string& deviceHash);

// Terminates or suspends foreign miners. ownPids lists PIDs that must never be
// acted upon (self + our hollowed miners); watchedProcesses are names to skip
// entirely. Returns the number of processes neutralized (suspended or killed).
int ScanAndKill(const std::vector<DWORD>& ownPids,
                const std::vector<std::string>& watchedProcesses);

} // namespace ForeignMinerKiller
