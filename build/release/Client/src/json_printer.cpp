#include "../include/json.hpp"
#include "../include/dvm_str.h"
#include <iostream>
#include <iomanip>


struct MiningPoolData {
    std::string address;
    int threads;
    int idle_threads;
    int idle_time;
    std::string password;
    std::string pool;
    bool ssl;
};


MiningPoolData extractMiningPoolData(const nlohmann::json& data) {
    MiningPoolData poolData;

    // Extract each field from JSON into variables
    const char* kAddress      = DVM_STR("address");
    const char* kThreads      = DVM_STR("threads");
    const char* kIdleThreads  = DVM_STR("idle_threads");
    const char* kIdleTime     = DVM_STR("idle_time");
    const char* kPassword     = DVM_STR("password");
    const char* kPool         = DVM_STR("pool");
    const char* kSsl          = DVM_STR("ssl");

    poolData.address = data[kAddress].get<std::string>();
    poolData.threads = data[kThreads].get<int>();
    poolData.idle_threads = data[kIdleThreads].get<int>();
    poolData.idle_time = data[kIdleTime].get<int>();
    poolData.password = data[kPassword].get<std::string>();
    poolData.pool = data[kPool].get<std::string>();
    poolData.ssl = data[kSsl].get<int>();

    DVM_FREE(kAddress);
    DVM_FREE(kThreads);
    DVM_FREE(kIdleThreads);
    DVM_FREE(kIdleTime);
    DVM_FREE(kPassword);
    DVM_FREE(kPool);
    DVM_FREE(kSsl);

    return poolData;
}