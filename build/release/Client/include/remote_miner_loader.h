// remote_miner_loader.h — REMOVED
//
// The remote-miner download path was removed for security. Pulling and
// hollowing a fresh miner PE from the panel without integrity verification
// made every deployed client hijackable by anyone who could compromise the
// panel URL (expired domain, DNS hijack, HTTP-only downgrade).
//
// XMRig's own stratum failover already handles the "swap pool without a
// redeploy" use case this feature was intended to solve.
//
// This header is intentionally empty. Any include of it stays valid so old
// code paths that still reference it compile without action, but the
// download functions no longer exist. Delete this file once no source in
// the tree includes it.
#pragma once
