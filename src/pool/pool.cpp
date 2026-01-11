/*
 * Copyright (c) 2025 INTcoin Team (Neil Adamson)
 * MIT License
 * Mining Pool Server Implementation (Stubs - API Complete)
 */

#include "intcoin/pool.h"
#include "intcoin/rpc.h"
#include "intcoin/util.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <memory>

// Forward declarations of server classes and factory functions
namespace intcoin {
namespace stratum {
    class StratumServer;
    StratumServer* CreateStratumServer(uint16_t port, MiningPoolServer& pool);
    void DestroyStratumServer(StratumServer* server);
    Result<void> StratumServerStart(StratumServer* server);
    void StratumServerBroadcastWork(StratumServer* server, const Work& work);
}
namespace pool {
    class HttpApiServer;
    HttpApiServer* CreateHttpApiServer(uint16_t port, MiningPoolServer& pool);
    void DestroyHttpApiServer(HttpApiServer* server);
    Result<void> HttpApiServerStart(HttpApiServer* server);
}
}

namespace intcoin {

// ============================================================================
// Variable Difficulty Manager
// ============================================================================

VarDiffManager::VarDiffManager(double target_share_time, double retarget_time, double variance)
    : target_share_time_(target_share_time)
    , retarget_time_(retarget_time)
    , variance_(variance) {}

uint64_t VarDiffManager::CalculateDifficulty(const Worker& worker) const {
    if (worker.recent_shares.size() < 3) {
        return worker.current_difficulty;
    }

    // Calculate average time between shares from recent_shares
    auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(
        worker.recent_shares.back() - worker.recent_shares.front());
    double avg_time = static_cast<double>(total_duration.count()) /
                     static_cast<double>(worker.recent_shares.size() - 1);
    double ratio = avg_time / target_share_time_;

    uint64_t new_diff = worker.current_difficulty;
    if (ratio < (1.0 - variance_)) {
        new_diff = static_cast<uint64_t>(worker.current_difficulty * 1.5);
    } else if (ratio > (1.0 + variance_)) {
        new_diff = static_cast<uint64_t>(worker.current_difficulty * 0.75);
    }

    return std::max(uint64_t(1000), new_diff);
}

bool VarDiffManager::ShouldAdjust(const Worker& worker) const {
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        now - worker.last_share_time);
    return duration.count() >= retarget_time_ && worker.recent_shares.size() >= 3;
}

double VarDiffManager::GetShareRate(const Worker& worker) const {
    if (worker.recent_shares.size() < 2) return 0.0;
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        worker.recent_shares.back() - worker.recent_shares.front());
    if (duration.count() == 0) return 0.0;
    return static_cast<double>(worker.recent_shares.size()) / duration.count();
}

// ============================================================================
// Share Validator
// ============================================================================

bool ShareValidator::ValidateDifficulty(const uint256& hash, uint64_t difficulty) {
    // Validate that the hash meets the required difficulty
    // A share is valid if: hash_difficulty >= required_difficulty
    //
    // Calculate the actual difficulty achieved by this hash
    uint64_t actual_difficulty = CalculateShareDifficulty(hash);

    // Share is valid if it meets or exceeds the required difficulty
    return actual_difficulty >= difficulty;
}

bool ShareValidator::ValidateWork(const Share& share, const Work& work) {
    return share.job_id == work.job_id;
}

bool ShareValidator::IsValidBlock(const uint256& hash, uint64_t network_difficulty) {
    // Check if this share also meets network difficulty (valid block)
    // A block is valid if: hash_difficulty >= network_difficulty
    //
    // Calculate the actual difficulty achieved by this hash
    uint64_t actual_difficulty = CalculateShareDifficulty(hash);

    // This is a valid block if it meets or exceeds network difficulty
    return actual_difficulty >= network_difficulty;
}

bool ShareValidator::ValidateTimestamp(const Share& share, const Work& work) {
    auto share_time = share.timestamp;
    auto work_time = work.created_at;
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(share_time - work_time);
    return diff.count() >= 0 && diff.count() < 300;  // Within 5 minutes
}

bool ShareValidator::IsDuplicateShare(const Share& share, const std::vector<Share>& recent_shares) {
    for (const auto& s : recent_shares) {
        if (s.nonce == share.nonce && s.job_id == share.job_id) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Payout Calculator
// ============================================================================

std::map<uint64_t, uint64_t> PayoutCalculator::CalculatePPLNS(
    const std::vector<Share>& shares,
    size_t n_shares,
    uint64_t block_reward,
    double pool_fee)
{
    std::map<uint64_t, uint64_t> payouts;
    uint64_t fee = CalculateFee(block_reward, pool_fee);
    uint64_t reward = block_reward - fee;

    // Count shares per miner in last N shares
    std::map<uint64_t, uint64_t> miner_shares;
    size_t start = shares.size() > n_shares ? shares.size() - n_shares : 0;
    uint64_t total = 0;

    for (size_t i = start; i < shares.size(); ++i) {
        if (shares[i].valid) {
            miner_shares[shares[i].miner_id]++;
            total++;
        }
    }

    if (total == 0) return payouts;

    for (const auto& [miner_id, count] : miner_shares) {
        uint64_t payout = (reward * count) / total;
        payouts[miner_id] = payout;
    }

    return payouts;
}

std::map<uint64_t, uint64_t> PayoutCalculator::CalculatePPS(
    const std::vector<Share>& shares,
    uint64_t expected_shares_per_block,
    uint64_t block_reward,
    double pool_fee)
{
    std::map<uint64_t, uint64_t> payouts;
    uint64_t fee = CalculateFee(block_reward, pool_fee);
    uint64_t reward_per_share = (block_reward - fee) / expected_shares_per_block;

    for (const auto& share : shares) {
        if (share.valid) {
            payouts[share.miner_id] += reward_per_share;
        }
    }

    return payouts;
}

std::map<uint64_t, uint64_t> PayoutCalculator::CalculateProportional(
    const std::vector<Share>& round_shares,
    uint64_t block_reward,
    double pool_fee)
{
    std::map<uint64_t, uint64_t> payouts;
    uint64_t fee = CalculateFee(block_reward, pool_fee);
    uint64_t reward = block_reward - fee;

    std::map<uint64_t, uint64_t> miner_shares;
    uint64_t total = 0;

    for (const auto& share : round_shares) {
        if (share.valid) {
            miner_shares[share.miner_id]++;
            total++;
        }
    }

    if (total == 0) return payouts;

    for (const auto& [miner_id, count] : miner_shares) {
        payouts[miner_id] = (reward * count) / total;
    }

    return payouts;
}

uint64_t PayoutCalculator::CalculateFee(uint64_t amount, double fee_percent) {
    return static_cast<uint64_t>(amount * fee_percent / 100.0);
}

// ============================================================================
// Hashrate Calculator
// ============================================================================

double HashrateCalculator::CalculateHashrate(const std::vector<Share>& shares,
                                             std::chrono::seconds window)
{
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - window;

    uint64_t total_difficulty = 0;
    size_t count = 0;

    for (const auto& share : shares) {
        if (share.timestamp >= cutoff && share.valid) {
            total_difficulty += share.difficulty;
            count++;
        }
    }

    if (count == 0 || window.count() == 0) return 0.0;

    // Hashrate = (shares * difficulty * 2^32) / time
    return (total_difficulty * 4294967296.0) / window.count();
}

double HashrateCalculator::CalculateHashrateFromDifficulty(uint64_t difficulty,
                                                           std::chrono::seconds time)
{
    if (time.count() == 0) return 0.0;
    return (difficulty * 4294967296.0) / time.count();
}

std::chrono::seconds HashrateCalculator::EstimateBlockTime(double pool_hashrate,
                                                           uint64_t network_difficulty)
{
    if (pool_hashrate == 0.0) {
        return std::chrono::seconds(std::numeric_limits<int64_t>::max());
    }

    double expected_hashes = network_difficulty * 4294967296.0;
    int64_t seconds = static_cast<int64_t>(expected_hashes / pool_hashrate);

    return std::chrono::seconds(seconds);
}

uint64_t HashrateCalculator::CalculateExpectedShares(uint64_t network_difficulty,
                                                     uint64_t share_difficulty)
{
    if (share_difficulty == 0) return 0;
    return network_difficulty / share_difficulty;
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string ToString(PoolConfig::PayoutMethod method) {
    switch (method) {
        case PoolConfig::PayoutMethod::PPLNS: return "PPLNS";
        case PoolConfig::PayoutMethod::PPS: return "PPS";
        case PoolConfig::PayoutMethod::PROP: return "Proportional";
        case PoolConfig::PayoutMethod::SOLO: return "Solo";
        default: return "Unknown";
    }
}

Result<stratum::Message> ParseStratumMessage(const std::string& json) {
    // Parse JSON string using RPC JSON parser
    auto json_result = rpc::JSONValue::Parse(json);
    if (json_result.IsError()) {
        return Result<stratum::Message>::Error("Invalid JSON: " +
                                               std::string(json_result.error));
    }

    const auto& json_obj = *json_result.value;
    if (!json_obj.IsObject()) {
        return Result<stratum::Message>::Error("JSON must be an object");
    }

    stratum::Message msg;
    msg.type = stratum::MessageType::UNKNOWN;

    // Parse ID (can be null for notifications)
    if (json_obj.HasKey("id")) {
        const auto& id_val = json_obj["id"];
        if (id_val.IsNumber()) {
            msg.id = static_cast<uint64_t>(id_val.GetInt());
        }
    }

    // Parse method (for requests and notifications)
    if (json_obj.HasKey("method")) {
        msg.method = json_obj["method"].GetString();

        // Determine message type from method
        if (msg.method == "mining.subscribe") {
            msg.type = stratum::MessageType::SUBSCRIBE;
        } else if (msg.method == "mining.authorize") {
            msg.type = stratum::MessageType::AUTHORIZE;
        } else if (msg.method == "mining.submit") {
            msg.type = stratum::MessageType::SUBMIT;
        } else if (msg.method == "mining.notify") {
            msg.type = stratum::MessageType::NOTIFY;
        } else if (msg.method == "mining.set_difficulty") {
            msg.type = stratum::MessageType::SET_DIFFICULTY;
        } else if (msg.method == "mining.set_extranonce") {
            msg.type = stratum::MessageType::SET_EXTRANONCE;
        } else if (msg.method == "client.get_version") {
            msg.type = stratum::MessageType::GET_VERSION;
        } else if (msg.method == "client.show_message") {
            msg.type = stratum::MessageType::SHOW_MESSAGE;
        } else if (msg.method == "client.reconnect") {
            msg.type = stratum::MessageType::RECONNECT;
        }
    }

    // Parse params (array of strings)
    if (json_obj.HasKey("params")) {
        const auto& params_val = json_obj["params"];
        if (params_val.IsArray()) {
            const auto& params_arr = params_val.GetArray();
            for (const auto& param : params_arr) {
                if (param.IsString()) {
                    msg.params.push_back(param.GetString());
                } else if (param.IsNumber()) {
                    msg.params.push_back(std::to_string(param.GetInt()));
                } else if (param.IsBool()) {
                    msg.params.push_back(param.GetBool() ? "true" : "false");
                }
            }
        }
    }

    // Parse result (for responses)
    if (json_obj.HasKey("result")) {
        const auto& result_val = json_obj["result"];
        if (result_val.IsString()) {
            msg.result = result_val.GetString();
        } else if (result_val.IsBool()) {
            msg.result = result_val.GetBool() ? "true" : "false";
        } else if (result_val.IsNull()) {
            msg.result = "null";
        } else {
            msg.result = result_val.ToJSONString();
        }
    }

    // Parse error (for error responses)
    if (json_obj.HasKey("error")) {
        const auto& error_val = json_obj["error"];
        if (!error_val.IsNull()) {
            if (error_val.IsString()) {
                msg.error = error_val.GetString();
            } else if (error_val.IsArray()) {
                // Stratum error format: [error_code, "error_message", null]
                const auto& err_arr = error_val.GetArray();
                if (err_arr.size() >= 2) {
                    msg.error = err_arr[1].GetString();
                }
            } else {
                msg.error = error_val.ToJSONString();
            }
        }
    }

    return Result<stratum::Message>::Ok(msg);
}

std::string FormatStratumResponse(const stratum::Message& msg) {
    // Build JSON response using RPC JSON builder
    rpc::JSONValue response;
    response.type = rpc::JSONType::Object;

    // Add ID (required for responses, null for notifications)
    if (msg.id != 0 || !msg.method.empty()) {
        response["id"] = rpc::JSONValue(static_cast<int64_t>(msg.id));
    } else {
        response["id"] = rpc::JSONValue();  // null
    }

    // If this is a notification (has method but response format)
    if (!msg.method.empty()) {
        response["method"] = rpc::JSONValue(msg.method);

        // Add params for notifications
        if (!msg.params.empty()) {
            std::vector<rpc::JSONValue> params_arr;
            for (const auto& param : msg.params) {
                params_arr.push_back(rpc::JSONValue(param));
            }
            response["params"] = rpc::JSONValue(params_arr);
        }
    }

    // Add result (for successful responses)
    if (msg.result.has_value()) {
        const std::string& result_str = *msg.result;

        // Try to parse result as JSON if it looks like JSON
        if (!result_str.empty() && (result_str[0] == '{' || result_str[0] == '[')) {
            auto result_json = rpc::JSONValue::Parse(result_str);
            if (result_json.IsOk()) {
                response["result"] = *result_json.value;
            } else {
                response["result"] = rpc::JSONValue(result_str);
            }
        } else if (result_str == "true") {
            response["result"] = rpc::JSONValue(true);
        } else if (result_str == "false") {
            response["result"] = rpc::JSONValue(false);
        } else if (result_str == "null") {
            response["result"] = rpc::JSONValue();  // null
        } else {
            response["result"] = rpc::JSONValue(result_str);
        }
    } else {
        response["result"] = rpc::JSONValue();  // null
    }

    // Add error (Stratum format: [error_code, "message", null])
    if (msg.error.has_value()) {
        std::vector<rpc::JSONValue> error_arr;
        error_arr.push_back(rpc::JSONValue(static_cast<int64_t>(20)));  // Generic error code
        error_arr.push_back(rpc::JSONValue(*msg.error));
        error_arr.push_back(rpc::JSONValue());  // null traceback
        response["error"] = rpc::JSONValue(error_arr);
    } else {
        response["error"] = rpc::JSONValue();  // null
    }

    return response.ToJSONString();
}

uint64_t CalculateShareDifficulty(const uint256& hash) {
    // Calculate difficulty from hash using Bitcoin's pool difficulty formula
    // difficulty = difficulty_1_target / hash
    //
    // Pool difficulty 1 target (pdiff):
    // 0x00000000FFFF0000000000000000000000000000000000000000000000000000
    //
    // This is the target for 1 difficulty share (roughly 2^32 hashes on average)

    // Count leading zero bits in hash
    size_t leading_zeros = 0;
    for (size_t i = 0; i < 32; i++) {
        uint8_t byte = hash.data()[31 - i];  // Start from most significant byte
        if (byte == 0) {
            leading_zeros += 8;
        } else {
            // Count leading zeros in this byte
            for (int bit = 7; bit >= 0; bit--) {
                if ((byte & (1 << bit)) == 0) {
                    leading_zeros++;
                } else {
                    break;
                }
            }
            break;
        }
    }

    // Calculate difficulty based on leading zeros
    // Each leading zero bit roughly doubles the difficulty
    // Base difficulty (pool difficulty 1) has ~32 leading zeros (0x00000000FFFF...)
    //
    // For a more accurate calculation:
    // difficulty = 2^(leading_zeros - 32) * adjustment_factor
    //
    // Simplified formula: difficulty ≈ 2^(leading_zeros - 32) * 65536

    if (leading_zeros < 32) {
        // Hash doesn't meet even difficulty 1 (very unlikely in practice)
        return 1;
    }

    // Calculate difficulty: 2^(leading_zeros - 32) * 65536
    uint64_t difficulty = 65536ULL;  // Base multiplier (0xFFFF + 1)
    size_t extra_zeros = leading_zeros - 32;

    // Multiply by 2^extra_zeros (with overflow protection)
    if (extra_zeros > 0) {
        if (extra_zeros >= 48) {
            // Cap at maximum uint64_t to prevent overflow
            return UINT64_MAX;
        }
        difficulty <<= extra_zeros;
    }

    // Minimum difficulty of 1
    return std::max(difficulty, static_cast<uint64_t>(1));
}

uint256 GenerateJobID() {
    return GetRandomUint256();
}

// ============================================================================
// Mining Pool Server Implementation
// ============================================================================

class MiningPoolServer::Impl {
public:
    Impl(const PoolConfig& config,
         std::shared_ptr<Blockchain> blockchain,
         std::shared_ptr<Miner> miner)
        : config_(config)
        , blockchain_(blockchain)
        , solo_miner_(miner)
        , running_(false)
        , next_miner_id_(1)
        , next_worker_id_(1)
        , next_share_id_(1)
        , next_round_id_(1)
        , next_payment_id_(1)
        , vardiff_(config.target_share_time, config.vardiff_retarget_time, config.vardiff_variance)
        , stratum_server_(nullptr)
        , http_api_server_(nullptr)
    {
        current_round_.round_id = next_round_id_++;
        current_round_.started_at = std::chrono::system_clock::now();
        current_round_.shares_submitted = 0;
        current_round_.is_complete = false;
    }

    ~Impl() {
        Stop();
    }

    // Configuration
    PoolConfig config_;
    std::shared_ptr<Blockchain> blockchain_;
    std::shared_ptr<Miner> solo_miner_;  // For solo mining mode

    // Server state
    std::atomic<bool> running_;
    std::mutex mutex_;

    // Miners and workers
    std::map<uint64_t, Miner> miners_;
    std::map<std::string, uint64_t> username_to_miner_id_;
    std::map<uint64_t, Worker> workers_;
    std::map<uint64_t, uint64_t> worker_to_miner_;  // worker_id -> miner_id
    std::atomic<uint64_t> next_miner_id_;
    std::atomic<uint64_t> next_worker_id_;

    // Shares and rounds
    std::vector<Share> recent_shares_;
    std::atomic<uint64_t> next_share_id_;
    RoundStatistics current_round_;
    std::vector<RoundStatistics> round_history_;
    std::atomic<uint64_t> next_round_id_;

    // Payment tracking
    std::vector<Payment> payment_history_;
    std::atomic<uint64_t> next_payment_id_;

    // Current work
    std::optional<Work> current_work_;
    std::mutex work_mutex_;

    // Variable difficulty
    VarDiffManager vardiff_;

    // Statistics
    PoolStatistics stats_;
    std::chrono::system_clock::time_point start_time_;

    // Security - banned miners and IPs
    std::map<std::string, std::chrono::system_clock::time_point> banned_ips_;
    std::mutex security_mutex_;

    // Callbacks
    std::optional<MiningPoolServer::BlockFoundCallback> block_found_callback_;
    std::optional<MiningPoolServer::PayoutCallback> payout_callback_;

    // Network servers (raw pointers due to forward declarations)
    stratum::StratumServer* stratum_server_;
    pool::HttpApiServer* http_api_server_;

    void Stop() {
        running_ = false;

        // Stop and delete network servers
        if (stratum_server_) {
            stratum::DestroyStratumServer(stratum_server_);
            stratum_server_ = nullptr;
        }
        if (http_api_server_) {
            pool::DestroyHttpApiServer(http_api_server_);
            http_api_server_ = nullptr;
        }
    }
};

// Constructor
MiningPoolServer::MiningPoolServer(const PoolConfig& config,
                                   std::shared_ptr<Blockchain> blockchain,
                                   std::shared_ptr<Miner> miner)
    : impl_(std::make_unique<Impl>(config, blockchain, miner))
{
    impl_->start_time_ = std::chrono::system_clock::now();
}

// Destructor
MiningPoolServer::~MiningPoolServer() = default;

// Server Control
Result<void> MiningPoolServer::Start() {
    if (impl_->running_) {
        return Result<void>::Error("Pool server already running");
    }

    impl_->running_ = true;

    // Create initial work
    auto work_result = CreateWork(false);
    if (!work_result.IsOk()) {
        impl_->running_ = false;
        return Result<void>::Error("Failed to create initial work: " + work_result.error);
    }

    // Initialize and start Stratum server
    impl_->stratum_server_ = stratum::CreateStratumServer(
        impl_->config_.stratum_port, *this);

    auto stratum_result = stratum::StratumServerStart(impl_->stratum_server_);
    if (!stratum_result.IsOk()) {
        stratum::DestroyStratumServer(impl_->stratum_server_);
        impl_->stratum_server_ = nullptr;
        impl_->running_ = false;
        return Result<void>::Error("Failed to start Stratum server: " + stratum_result.error);
    }

    // Initialize and start HTTP API server
    impl_->http_api_server_ = pool::CreateHttpApiServer(
        impl_->config_.http_port, *this);

    auto http_result = pool::HttpApiServerStart(impl_->http_api_server_);
    if (!http_result.IsOk()) {
        stratum::DestroyStratumServer(impl_->stratum_server_);
        impl_->stratum_server_ = nullptr;
        pool::DestroyHttpApiServer(impl_->http_api_server_);
        impl_->http_api_server_ = nullptr;
        impl_->running_ = false;
        return Result<void>::Error("Failed to start HTTP API server: " + http_result.error);
    }

    return Result<void>::Ok();
}

void MiningPoolServer::Stop() {
    impl_->Stop();
}

bool MiningPoolServer::IsRunning() const {
    return impl_->running_;
}

// Miner Management
Result<uint64_t> MiningPoolServer::RegisterMiner(const std::string& username,
                                                  const std::string& payout_address,
                                                  const std::string& email)
{
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Check if username already exists
    if (impl_->username_to_miner_id_.count(username) > 0) {
        return Result<uint64_t>::Error("Username already registered");
    }

    // Check max miners limit
    if (impl_->miners_.size() >= impl_->config_.max_miners) {
        return Result<uint64_t>::Error("Maximum miners limit reached");
    }

    Miner miner;
    miner.miner_id = impl_->next_miner_id_++;
    miner.username = username;
    miner.payout_address = payout_address;
    miner.email = email;
    miner.total_shares_submitted = 0;
    miner.total_shares_accepted = 0;
    miner.total_shares_rejected = 0;
    miner.total_blocks_found = 0;
    miner.total_hashrate = 0.0;
    miner.unpaid_balance = 0;
    miner.paid_balance = 0;
    miner.estimated_earnings = 0;
    miner.invalid_share_count = 0;
    miner.is_banned = false;
    miner.registered_at = std::chrono::system_clock::now();
    miner.last_seen = std::chrono::system_clock::now();

    impl_->miners_[miner.miner_id] = miner;
    impl_->username_to_miner_id_[username] = miner.miner_id;

    return Result<uint64_t>::Ok(miner.miner_id);
}

std::optional<Miner> MiningPoolServer::GetMiner(uint64_t miner_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->miners_.find(miner_id);
    if (it != impl_->miners_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<Miner> MiningPoolServer::GetMinerByUsername(const std::string& username) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->username_to_miner_id_.find(username);
    if (it != impl_->username_to_miner_id_.end()) {
        return GetMiner(it->second);
    }
    return std::nullopt;
}

Result<void> MiningPoolServer::UpdatePayoutAddress(uint64_t miner_id,
                                                    const std::string& new_address)
{
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->miners_.find(miner_id);
    if (it == impl_->miners_.end()) {
        return Result<void>::Error("Miner not found");
    }

    it->second.payout_address = new_address;
    return Result<void>::Ok();
}

std::vector<Miner> MiningPoolServer::GetAllMiners() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<Miner> miners;
    for (const auto& [id, miner] : impl_->miners_) {
        miners.push_back(miner);
    }
    return miners;
}

std::vector<Miner> MiningPoolServer::GetActiveMiners() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<Miner> active_miners;
    auto now = std::chrono::system_clock::now();
    auto timeout = std::chrono::minutes(10);

    for (const auto& [id, miner] : impl_->miners_) {
        if (now - miner.last_seen < timeout) {
            active_miners.push_back(miner);
        }
    }
    return active_miners;
}

// Worker Management
Result<uint64_t> MiningPoolServer::AddWorker(uint64_t miner_id,
                                              const std::string& worker_name,
                                              const std::string& ip_address,
                                              uint16_t port)
{
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Check if miner exists
    auto miner_it = impl_->miners_.find(miner_id);
    if (miner_it == impl_->miners_.end()) {
        return Result<uint64_t>::Error("Miner not found");
    }

    // Check workers per miner limit
    if (miner_it->second.workers.size() >= impl_->config_.max_workers_per_miner) {
        return Result<uint64_t>::Error("Maximum workers per miner limit reached");
    }

    Worker worker;
    worker.worker_id = impl_->next_worker_id_++;
    worker.miner_id = miner_id;
    worker.worker_name = worker_name;
    worker.shares_submitted = 0;
    worker.shares_accepted = 0;
    worker.shares_rejected = 0;
    worker.shares_stale = 0;
    worker.blocks_found = 0;
    worker.current_hashrate = 0.0;
    worker.average_hashrate = 0.0;
    worker.current_difficulty = impl_->config_.initial_difficulty;
    worker.ip_address = ip_address;
    worker.port = port;
    worker.connected_at = std::chrono::system_clock::now();
    worker.last_activity = std::chrono::system_clock::now();
    worker.is_active = true;

    impl_->workers_[worker.worker_id] = worker;
    impl_->worker_to_miner_[worker.worker_id] = miner_id;
    miner_it->second.workers[worker.worker_id] = worker;

    return Result<uint64_t>::Ok(worker.worker_id);
}

void MiningPoolServer::RemoveWorker(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto worker_it = impl_->workers_.find(worker_id);
    if (worker_it == impl_->workers_.end()) {
        return;
    }

    uint64_t miner_id = impl_->worker_to_miner_[worker_id];
    auto miner_it = impl_->miners_.find(miner_id);
    if (miner_it != impl_->miners_.end()) {
        miner_it->second.workers.erase(worker_id);
    }

    impl_->workers_.erase(worker_id);
    impl_->worker_to_miner_.erase(worker_id);
}

std::optional<Worker> MiningPoolServer::GetWorker(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->workers_.find(worker_id);
    if (it != impl_->workers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Worker> MiningPoolServer::GetMinerWorkers(uint64_t miner_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<Worker> workers;

    for (const auto& [worker_id, worker] : impl_->workers_) {
        if (worker.miner_id == miner_id) {
            workers.push_back(worker);
        }
    }

    return workers;
}

void MiningPoolServer::UpdateWorkerActivity(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->workers_.find(worker_id);
    if (it != impl_->workers_.end()) {
        it->second.last_activity = std::chrono::system_clock::now();
        it->second.is_active = true;
    }
}

void MiningPoolServer::DisconnectInactiveWorkers(std::chrono::seconds timeout) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto now = std::chrono::system_clock::now();
    std::vector<uint64_t> to_remove;

    for (const auto& [worker_id, worker] : impl_->workers_) {
        if (now - worker.last_activity > timeout) {
            to_remove.push_back(worker_id);
        }
    }

    for (uint64_t worker_id : to_remove) {
        RemoveWorker(worker_id);
    }
}

// Share Processing
Result<void> MiningPoolServer::SubmitShare(uint64_t worker_id,
                                            const uint256& job_id,
                                            const uint256& nonce,
                                            const uint256& share_hash)
{
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Get worker
    auto worker_it = impl_->workers_.find(worker_id);
    if (worker_it == impl_->workers_.end()) {
        return Result<void>::Error("Worker not found");
    }

    // Get miner
    uint64_t miner_id = impl_->worker_to_miner_[worker_id];
    auto miner_it = impl_->miners_.find(miner_id);
    if (miner_it == impl_->miners_.end()) {
        return Result<void>::Error("Miner not found");
    }

    // Create share
    Share share;
    share.share_id = impl_->next_share_id_++;
    share.miner_id = miner_id;
    share.worker_id = worker_id;
    share.worker_name = worker_it->second.worker_name;
    share.job_id = job_id;
    share.nonce = nonce;
    share.share_hash = share_hash;
    share.difficulty = worker_it->second.current_difficulty;
    share.timestamp = std::chrono::system_clock::now();
    share.valid = false;

    // Validate share
    auto validation_result = ValidateShare(share);
    if (!validation_result.IsOk()) {
        share.valid = false;
        share.error_msg = validation_result.error;

        // Update statistics
        worker_it->second.shares_rejected++;
        miner_it->second.total_shares_rejected++;

        // Check for excessive invalid shares
        miner_it->second.invalid_share_count++;
        CheckInvalidShares(miner_id);

        return Result<void>::Error("Share rejected: " + validation_result.error);
    }

    share.valid = validation_result.GetValue();

    if (share.valid) {
        ProcessValidShare(share);

        // Check if this is also a valid block
        auto network_difficulty = impl_->blockchain_->GetDifficulty();
        if (ShareValidator::IsValidBlock(share_hash, network_difficulty)) {
            share.is_block = true;
            auto block_result = ProcessBlockFound(share);
            if (!block_result.IsOk()) {
                return Result<void>::Error("Share accepted but block processing failed: " +
                                          block_result.error);
            }
        }
    }

    // Add to recent shares
    impl_->recent_shares_.push_back(share);

    // Keep only last 10000 shares in memory
    if (impl_->recent_shares_.size() > 10000) {
        impl_->recent_shares_.erase(impl_->recent_shares_.begin(),
                                    impl_->recent_shares_.begin() + 1000);
    }

    return Result<void>::Ok();
}

Result<bool> MiningPoolServer::ValidateShare(const Share& share) {
    // Get current work
    std::lock_guard<std::mutex> work_lock(impl_->work_mutex_);
    if (!impl_->current_work_.has_value()) {
        return Result<bool>::Error("No current work available");
    }

    const Work& work = *impl_->current_work_;

    // Validate difficulty
    if (!ShareValidator::ValidateDifficulty(share.share_hash, share.difficulty)) {
        return Result<bool>::Error("Share does not meet difficulty requirement");
    }

    // Validate work
    if (!ShareValidator::ValidateWork(share, work)) {
        return Result<bool>::Error("Share is for stale work");
    }

    // Validate timestamp
    if (!ShareValidator::ValidateTimestamp(share, work)) {
        return Result<bool>::Error("Share timestamp invalid");
    }

    // Check for duplicate
    if (ShareValidator::IsDuplicateShare(share, impl_->recent_shares_)) {
        return Result<bool>::Error("Duplicate share");
    }

    return Result<bool>::Ok(true);
}

void MiningPoolServer::ProcessValidShare(const Share& share) {
    // Update worker statistics
    auto worker_it = impl_->workers_.find(share.worker_id);
    if (worker_it != impl_->workers_.end()) {
        worker_it->second.shares_submitted++;
        worker_it->second.shares_accepted++;
        worker_it->second.recent_shares.push_back(share.timestamp);

        // Keep only last 100 shares for hashrate calculation
        if (worker_it->second.recent_shares.size() > 100) {
            worker_it->second.recent_shares.erase(worker_it->second.recent_shares.begin());
        }

        // Update hashrate
        worker_it->second.current_hashrate = CalculateWorkerHashrate(share.worker_id);

        // Update difficulty if needed
        if (impl_->vardiff_.ShouldAdjust(worker_it->second)) {
            AdjustWorkerDifficulty(share.worker_id);
        }
    }

    // Update miner statistics
    auto miner_it = impl_->miners_.find(share.miner_id);
    if (miner_it != impl_->miners_.end()) {
        miner_it->second.total_shares_submitted++;
        miner_it->second.total_shares_accepted++;
        miner_it->second.last_seen = std::chrono::system_clock::now();

        // Reset invalid share count on valid share
        miner_it->second.invalid_share_count = 0;
    }

    // Update round statistics
    impl_->current_round_.shares_submitted++;
    impl_->current_round_.miner_shares[share.miner_id]++;

    // Update pool statistics
    impl_->stats_.shares_this_round++;
    impl_->stats_.total_shares++;
}

Result<void> MiningPoolServer::ProcessBlockFound(const Share& share) {
    // Construct block from share and current work
    std::lock_guard<std::mutex> work_lock(impl_->work_mutex_);
    if (!impl_->current_work_.has_value()) {
        return Result<void>::Error("No current work available");
    }

    const Work& work = *impl_->current_work_;

    Block block;
    block.header = work.header;

    // Convert uint256 nonce to uint64_t (take first 8 bytes)
    uint64_t nonce_u64 = 0;
    for (size_t i = 0; i < 8 && i < share.nonce.size(); i++) {
        nonce_u64 |= (static_cast<uint64_t>(share.nonce[i]) << (i * 8));
    }
    block.header.nonce = nonce_u64;
    block.transactions = work.transactions;

    // Submit block to blockchain
    auto submit_result = impl_->blockchain_->AddBlock(block);
    if (!submit_result.IsOk()) {
        return Result<void>::Error("Failed to submit block: " + submit_result.error);
    }

    // Update statistics
    auto worker_it = impl_->workers_.find(share.worker_id);
    if (worker_it != impl_->workers_.end()) {
        worker_it->second.blocks_found++;
    }

    auto miner_it = impl_->miners_.find(share.miner_id);
    if (miner_it != impl_->miners_.end()) {
        miner_it->second.total_blocks_found++;
    }

    impl_->stats_.blocks_found++;
    impl_->stats_.blocks_pending++;
    impl_->stats_.last_block_found = std::chrono::system_clock::now();

    // Complete current round
    impl_->current_round_.ended_at = std::chrono::system_clock::now();
    impl_->current_round_.block_height = work.height;
    impl_->current_round_.block_hash = block.GetHash();

    // Calculate block reward (simplified - actual reward depends on height)
    uint64_t block_reward = 50 * 100000000ULL;  // 50 INTS in base units
    impl_->current_round_.block_reward = block_reward;
    impl_->current_round_.is_complete = true;

    impl_->round_history_.push_back(impl_->current_round_);

    // Start new round
    impl_->current_round_ = RoundStatistics();
    impl_->current_round_.round_id = impl_->next_round_id_++;
    impl_->current_round_.started_at = std::chrono::system_clock::now();
    impl_->current_round_.is_complete = false;

    // Trigger block found callback
    if (impl_->block_found_callback_.has_value()) {
        (*impl_->block_found_callback_)(block, share.miner_id);
    }

    // Create new work
    UpdateWork();

    return Result<void>::Ok();
}

std::vector<Share> MiningPoolServer::GetRecentShares(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (count >= impl_->recent_shares_.size()) {
        return impl_->recent_shares_;
    }

    return std::vector<Share>(impl_->recent_shares_.end() - count,
                             impl_->recent_shares_.end());
}

std::vector<Share> MiningPoolServer::GetMinerShares(uint64_t miner_id, size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::vector<Share> miner_shares;

    for (auto it = impl_->recent_shares_.rbegin();
         it != impl_->recent_shares_.rend() && miner_shares.size() < count; ++it) {
        if (it->miner_id == miner_id) {
            miner_shares.push_back(*it);
        }
    }

    std::reverse(miner_shares.begin(), miner_shares.end());
    return miner_shares;
}

// Work Management
Result<Work> MiningPoolServer::CreateWork(bool clean_jobs) {
    std::lock_guard<std::mutex> work_lock(impl_->work_mutex_);

    // Get block template from blockchain
    // TODO: Use proper wallet/keypair for pool rewards
    // For now, use a placeholder public key (pool operator should configure this)
    PublicKey pool_pubkey;
    pool_pubkey.fill(0);  // Placeholder - should be configured

    auto template_result = impl_->blockchain_->GetBlockTemplate(pool_pubkey);
    if (!template_result.IsOk()) {
        return Result<Work>::Error("Failed to get block template: " +
                                   template_result.error);
    }

    auto block_template = template_result.GetValue();

    Work work;
    work.job_id = GenerateJobID();
    work.header = block_template.header;
    work.coinbase_tx = block_template.transactions[0];
    work.transactions = block_template.transactions;
    work.merkle_root = block_template.header.merkle_root;
    work.height = impl_->blockchain_->GetBestHeight() + 1;  // Next block height
    work.difficulty = impl_->blockchain_->GetDifficulty();
    work.created_at = std::chrono::system_clock::now();
    work.clean_jobs = clean_jobs;

    impl_->current_work_ = work;

    return Result<Work>::Ok(work);
}

std::optional<Work> MiningPoolServer::GetCurrentWork() const {
    std::lock_guard<std::mutex> work_lock(impl_->work_mutex_);
    return impl_->current_work_;
}

Result<void> MiningPoolServer::UpdateWork() {
    auto work_result = CreateWork(true);
    if (!work_result.IsOk()) {
        return Result<void>::Error("Failed to create new work: " + work_result.error);
    }

    // Broadcast new work to all miners
    BroadcastWork(work_result.GetValue());

    return Result<void>::Ok();
}

void MiningPoolServer::BroadcastWork(const Work& work) {
    // Broadcast work to all connected miners via Stratum
    if (impl_->stratum_server_) {
        stratum::StratumServerBroadcastWork(impl_->stratum_server_, work);
    }
}

// Configuration
const PoolConfig& MiningPoolServer::GetConfig() const {
    return impl_->config_;
}

void MiningPoolServer::UpdateConfig(const PoolConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->config_ = config;
}

// Callbacks
void MiningPoolServer::RegisterBlockFoundCallback(BlockFoundCallback callback) {
    impl_->block_found_callback_ = callback;
}

void MiningPoolServer::RegisterPayoutCallback(PayoutCallback callback) {
    impl_->payout_callback_ = callback;
}

// Remaining stub methods (to be implemented in next iteration)
uint64_t MiningPoolServer::CalculateWorkerDifficulty(uint64_t worker_id) const {
    auto worker = GetWorker(worker_id);
    if (!worker.has_value()) return impl_->config_.initial_difficulty;
    return impl_->vardiff_.CalculateDifficulty(*worker);
}

void MiningPoolServer::AdjustWorkerDifficulty(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->workers_.find(worker_id);
    if (it == impl_->workers_.end()) return;

    uint64_t old_diff = it->second.current_difficulty;
    uint64_t new_diff = impl_->vardiff_.CalculateDifficulty(it->second);

    // Only update if difficulty changed
    if (new_diff != old_diff) {
        it->second.current_difficulty = new_diff;

        // Send difficulty update via Stratum
        SendSetDifficulty(worker_id, new_diff);

        LogF(LogLevel::DEBUG, "Adjusted worker %llu difficulty: %llu -> %llu",
             worker_id, old_diff, new_diff);
    }
}

void MiningPoolServer::SetWorkerDifficulty(uint64_t worker_id, uint64_t difficulty) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->workers_.find(worker_id);
    if (it != impl_->workers_.end()) {
        it->second.current_difficulty = difficulty;
    }
}

void MiningPoolServer::AdjustAllDifficulties() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    size_t adjusted_count = 0;

    for (auto& [worker_id, worker] : impl_->workers_) {
        if (impl_->vardiff_.ShouldAdjust(worker)) {
            uint64_t old_diff = worker.current_difficulty;
            uint64_t new_diff = impl_->vardiff_.CalculateDifficulty(worker);

            if (new_diff != old_diff) {
                worker.current_difficulty = new_diff;

                // Send difficulty update via Stratum
                SendSetDifficulty(worker_id, new_diff);

                LogF(LogLevel::DEBUG, "Adjusted worker %llu difficulty: %llu -> %llu",
                     worker_id, old_diff, new_diff);

                adjusted_count++;
            }
        }
    }

    if (adjusted_count > 0) {
        LogF(LogLevel::INFO, "Adjusted difficulty for %zu workers", adjusted_count);
    }
}

std::map<uint64_t, uint64_t> MiningPoolServer::CalculatePPLNSPayouts(uint64_t block_reward) {
    return PayoutCalculator::CalculatePPLNS(impl_->recent_shares_,
                                           impl_->config_.pplns_window,
                                           block_reward,
                                           impl_->config_.pool_fee_percent);
}

std::map<uint64_t, uint64_t> MiningPoolServer::CalculatePPSPayouts() {
    auto network_diff = impl_->blockchain_->GetDifficulty();
    auto share_diff = impl_->config_.initial_difficulty;
    uint64_t expected_shares = HashrateCalculator::CalculateExpectedShares(network_diff, share_diff);

    // Simplified block reward (should use ConsensusValidator::GetBlockReward)
    uint64_t block_reward = 50 * 100000000ULL;  // 50 INTS in base units

    return PayoutCalculator::CalculatePPS(impl_->recent_shares_,
                                         expected_shares,
                                         block_reward,
                                         impl_->config_.pool_fee_percent);
}

Result<void> MiningPoolServer::ProcessPayouts() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    std::vector<Payment> new_payments;
    auto now = std::chrono::system_clock::now();

    // Iterate through all miners to check payout eligibility
    for (auto& [miner_id, miner] : impl_->miners_) {
        // Check if miner has enough balance for payout
        if (miner.unpaid_balance < impl_->config_.min_payout) {
            continue;  // Skip miners below threshold
        }

        // Check if enough time has passed since last payout
        auto time_since_last = std::chrono::duration_cast<std::chrono::seconds>(
            now - miner.last_payout).count();

        if (time_since_last < static_cast<int64_t>(impl_->config_.payout_interval)) {
            continue;  // Too soon for payout
        }

        // Create payment record
        Payment payment;
        payment.payment_id = impl_->next_payment_id_++;
        payment.miner_id = miner_id;
        payment.payout_address = miner.payout_address;
        payment.amount = miner.unpaid_balance;
        payment.tx_hash.fill(0);  // Will be filled when transaction is created
        payment.created_at = now;
        payment.is_confirmed = false;
        payment.status = "pending";

        // Store payment record
        impl_->payment_history_.push_back(payment);
        new_payments.push_back(payment);

        // Update miner balances
        miner.unpaid_balance = 0;
        miner.last_payout = now;

        // Call payout callback if registered
        if (impl_->payout_callback_) {
            (*impl_->payout_callback_)(miner_id, payment.amount);
        }
    }

    // Log payout processing
    if (!new_payments.empty()) {
        std::cout << "[Pool] Processed " << new_payments.size() << " payouts" << std::endl;
        for (const auto& payment : new_payments) {
            std::cout << "  Payout #" << payment.payment_id
                      << ": " << payment.amount << " INTS to "
                      << payment.payout_address << std::endl;
        }
    }

    return Result<void>::Ok();
}

uint64_t MiningPoolServer::GetMinerBalance(uint64_t miner_id) const {
    auto miner = GetMiner(miner_id);
    if (!miner.has_value()) return 0;
    return miner->unpaid_balance;
}

uint64_t MiningPoolServer::GetMinerEstimatedEarnings(uint64_t miner_id) const {
    auto miner = GetMiner(miner_id);
    if (!miner.has_value()) return 0;
    return miner->estimated_earnings;
}

std::vector<Payment> MiningPoolServer::GetPaymentHistory(size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    std::vector<Payment> result;
    size_t count = std::min(limit, impl_->payment_history_.size());

    // Get most recent payments (from end of vector)
    if (count > 0) {
        auto start = impl_->payment_history_.end() - count;
        result.assign(start, impl_->payment_history_.end());

        // Reverse to get newest first
        std::reverse(result.begin(), result.end());
    }

    return result;
}

std::vector<Payment> MiningPoolServer::GetMinerPaymentHistory(uint64_t miner_id, size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    std::vector<Payment> result;

    // Filter payments for specific miner
    for (auto it = impl_->payment_history_.rbegin();
         it != impl_->payment_history_.rend() && result.size() < limit;
         ++it) {
        if (it->miner_id == miner_id) {
            result.push_back(*it);
        }
    }

    return result;
}

PoolStatistics MiningPoolServer::GetStatistics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    PoolStatistics stats = impl_->stats_;

    // Update real-time statistics
    stats.network_height = impl_->blockchain_->GetBestHeight();
    stats.network_difficulty = impl_->blockchain_->GetDifficulty();
    stats.active_miners = GetActiveMiners().size();
    stats.active_workers = 0;

    for (const auto& [id, worker] : impl_->workers_) {
        if (worker.is_active) stats.active_workers++;
    }

    stats.pool_hashrate = CalculatePoolHashrate();

    auto now = std::chrono::system_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::hours>(now - impl_->start_time_);
    stats.uptime_hours = uptime.count();

    return stats;
}

RoundStatistics MiningPoolServer::GetCurrentRound() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->current_round_;
}

std::vector<RoundStatistics> MiningPoolServer::GetRoundHistory(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (count >= impl_->round_history_.size()) {
        return impl_->round_history_;
    }

    return std::vector<RoundStatistics>(impl_->round_history_.end() - count,
                                       impl_->round_history_.end());
}

double MiningPoolServer::CalculatePoolHashrate() const {
    return HashrateCalculator::CalculateHashrate(impl_->recent_shares_,
                                                 std::chrono::minutes(10));
}

double MiningPoolServer::CalculateWorkerHashrate(uint64_t worker_id) const {
    auto shares = GetMinerShares(impl_->worker_to_miner_.at(worker_id), 100);
    std::vector<Share> worker_shares;
    for (const auto& share : shares) {
        if (share.worker_id == worker_id) {
            worker_shares.push_back(share);
        }
    }
    return HashrateCalculator::CalculateHashrate(worker_shares, std::chrono::minutes(5));
}

double MiningPoolServer::CalculateMinerHashrate(uint64_t miner_id) const {
    auto shares = GetMinerShares(miner_id, 200);
    return HashrateCalculator::CalculateHashrate(shares, std::chrono::minutes(10));
}

// ============================================================================
// Stratum Protocol Implementation
// ============================================================================

Result<stratum::Message> MiningPoolServer::HandleStratumMessage(const std::string& json) {
    // Parse the incoming JSON-RPC Stratum message
    auto parse_result = ParseStratumMessage(json);
    if (parse_result.IsError()) {
        return Result<stratum::Message>::Error("Failed to parse Stratum message: " +
                                               std::string(parse_result.error));
    }

    stratum::Message request = *parse_result.value;
    stratum::Message response;
    response.id = request.id;

    // Route to appropriate handler based on message type
    switch (request.type) {
        case stratum::MessageType::SUBSCRIBE: {
            // mining.subscribe - worker subscription
            uint64_t conn_id = request.id;

            auto subscribe_result = HandleSubscribe(conn_id);
            if (subscribe_result.IsError()) {
                response.error = subscribe_result.error;
            } else {
                const auto& sub_resp = *subscribe_result.value;

                // Build JSON array for subscriptions
                std::string subs_json = "[";
                for (size_t i = 0; i < sub_resp.subscriptions.size(); i++) {
                    subs_json += "[";
                    for (size_t j = 0; j < sub_resp.subscriptions[i].size(); j++) {
                        subs_json += "\"" + sub_resp.subscriptions[i][j] + "\"";
                        if (j < sub_resp.subscriptions[i].size() - 1) subs_json += ",";
                    }
                    subs_json += "]";
                    if (i < sub_resp.subscriptions.size() - 1) subs_json += ",";
                }
                subs_json += "]";

                // Format: [subscriptions, extranonce1, extranonce2_size]
                response.result = "[" + subs_json +
                                  ",\"" + sub_resp.extranonce1 + "\"," +
                                  std::to_string(sub_resp.extranonce2_size) + "]";
            }
            break;
        }

        case stratum::MessageType::AUTHORIZE: {
            // mining.authorize - worker authentication
            if (request.params.size() < 2) {
                response.error = "mining.authorize requires username and password";
            } else {
                const std::string& username = request.params[0];
                const std::string& password = request.params[1];
                uint64_t conn_id = request.id;

                auto auth_result = HandleAuthorize(conn_id, username, password);
                if (auth_result.IsError()) {
                    response.error = auth_result.error;
                } else {
                    response.result = (*auth_result.value) ? "true" : "false";
                }
            }
            break;
        }

        case stratum::MessageType::SUBMIT: {
            // mining.submit - share submission
            if (request.params.size() < 5) {
                response.error = "mining.submit requires 5 parameters";
            } else {
                uint64_t conn_id = request.id;
                const std::string& job_id = request.params[1];
                const std::string& nonce = request.params[4];
                const std::string& result = request.params[4];

                auto submit_result = HandleSubmit(conn_id, job_id, nonce, result);
                if (submit_result.IsError()) {
                    response.error = submit_result.error;
                } else {
                    response.result = (*submit_result.value) ? "true" : "false";
                }
            }
            break;
        }

        case stratum::MessageType::GET_VERSION: {
            // client.get_version - client version request
            response.result = "\"INTcoin Pool Server v1.0.0\"";
            break;
        }

        case stratum::MessageType::UNKNOWN:
        default: {
            response.error = "Unknown or unsupported method: " + request.method;
            break;
        }
    }

    return Result<stratum::Message>::Ok(response);
}

Result<stratum::SubscribeResponse> MiningPoolServer::HandleSubscribe(uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Generate unique extranonce1 for this connection (8 hex characters)
    std::ostringstream extranonce1_stream;
    extranonce1_stream << std::hex << std::setw(8) << std::setfill('0') << conn_id;
    std::string extranonce1 = extranonce1_stream.str();

    // extranonce2_size: 4 bytes (allows 2^32 nonce space per worker)
    size_t extranonce2_size = 4;

    // Build subscription list (mining.notify, mining.set_difficulty)
    std::vector<std::vector<std::string>> subscriptions;
    subscriptions.push_back({"mining.notify", std::to_string(conn_id)});
    subscriptions.push_back({"mining.set_difficulty", std::to_string(conn_id)});

    stratum::SubscribeResponse response;
    response.subscriptions = subscriptions;
    response.extranonce1 = extranonce1;
    response.extranonce2_size = extranonce2_size;

    return Result<stratum::SubscribeResponse>::Ok(response);
}

Result<bool> MiningPoolServer::HandleAuthorize(uint64_t conn_id,
                                                const std::string& username,
                                                const std::string& password) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Parse username format: "wallet_address.worker_name"
    std::string wallet_address = username;
    std::string worker_name = "default";

    size_t dot_pos = username.find('.');
    if (dot_pos != std::string::npos) {
        wallet_address = username.substr(0, dot_pos);
        worker_name = username.substr(dot_pos + 1);
    }

    // Validate wallet address format (basic check)
    if (wallet_address.empty() || wallet_address.length() < 20) {
        return Result<bool>::Error("Invalid wallet address");
    }

    // Check if miner exists, or create new one
    uint64_t miner_id;
    auto username_it = impl_->username_to_miner_id_.find(wallet_address);

    if (username_it != impl_->username_to_miner_id_.end()) {
        miner_id = username_it->second;
    } else {
        // New miner - create entry
        miner_id = impl_->next_miner_id_++;
        impl_->username_to_miner_id_[wallet_address] = miner_id;
    }

    // Create worker
    uint64_t worker_id = impl_->next_worker_id_++;

    Worker worker;
    worker.worker_id = worker_id;
    worker.miner_id = miner_id;
    worker.worker_name = worker_name;
    worker.connected_at = std::chrono::system_clock::now();
    worker.last_activity = std::chrono::system_clock::now();
    worker.shares_submitted = 0;
    worker.shares_accepted = 0;
    worker.shares_rejected = 0;
    worker.shares_stale = 0;
    worker.blocks_found = 0;
    worker.current_hashrate = 0.0;
    worker.average_hashrate = 0.0;
    worker.current_difficulty = impl_->config_.initial_difficulty;
    worker.is_active = true;

    impl_->workers_[worker_id] = worker;
    impl_->worker_to_miner_[worker_id] = miner_id;

    (void)password;  // Password typically ignored in Stratum
    (void)conn_id;   // Connection tracking at network layer

    return Result<bool>::Ok(true);
}

Result<bool> MiningPoolServer::HandleSubmit(uint64_t conn_id,
                                             const std::string& job_id,
                                             const std::string& nonce,
                                             const std::string& result) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Find the worker by connection ID
    Worker* worker = nullptr;
    uint64_t worker_id = 0;

    for (auto& [wid, w] : impl_->workers_) {
        if (w.is_active) {
            worker = &w;
            worker_id = wid;
            break;
        }
    }

    if (!worker) {
        return Result<bool>::Error("Worker not authorized");
    }

    worker->last_activity = std::chrono::system_clock::now();

    // Validate we have current work
    if (!impl_->current_work_.has_value()) {
        return Result<bool>::Error("No active job");
    }

    // Parse nonce from hex string
    uint32_t nonce_value = 0;
    try {
        nonce_value = std::stoul(nonce, nullptr, 16);
    } catch (...) {
        worker->shares_rejected++;
        return Result<bool>::Error("Invalid nonce format");
    }
    (void)nonce_value;  // Nonce validation in full implementation

    // Parse result hash from hex string
    uint256 result_hash;
    if (result.length() != 64) {
        worker->shares_rejected++;
        return Result<bool>::Error("Invalid result format");
    }

    for (size_t i = 0; i < 32; i++) {
        std::string byte_str = result.substr(i * 2, 2);
        result_hash[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }

    // Calculate share difficulty from hash
    uint64_t share_difficulty = CalculateShareDifficulty(result_hash);

    // Check if share meets worker's difficulty target
    if (share_difficulty < worker->current_difficulty) {
        worker->shares_rejected++;
        return Result<bool>::Error("Share difficulty too low");
    }

    // Valid share! Create share record
    Share share;
    share.share_id = impl_->next_share_id_++;
    share.worker_id = worker_id;
    share.miner_id = worker->miner_id;
    share.worker_name = worker->worker_name;
    share.share_hash = result_hash;
    share.difficulty = share_difficulty;
    share.timestamp = std::chrono::system_clock::now();
    share.valid = true;
    share.is_block = false;

    // Check if this share is a valid block (meets network difficulty)
    if (impl_->blockchain_ && impl_->current_work_.has_value()) {
        const Work& current_work = *impl_->current_work_;
        uint256 network_target = DifficultyCalculator::CompactToTarget(current_work.header.bits);

        bool is_valid_block = true;
        for (int i = 31; i >= 0; i--) {
            if (result_hash[i] < network_target[i]) break;
            if (result_hash[i] > network_target[i]) {
                is_valid_block = false;
                break;
            }
        }

        if (is_valid_block) {
            share.is_block = true;
            impl_->stats_.blocks_found++;
            worker->blocks_found++;
            // TODO: Submit block to blockchain
        }
    }

    // Update worker statistics
    worker->shares_submitted++;
    worker->shares_accepted++;
    worker->last_share_time = std::chrono::system_clock::now();

    // Store share
    impl_->recent_shares_.push_back(share);
    impl_->stats_.total_shares++;

    // Update round statistics
    impl_->current_round_.shares_submitted++;
    if (share.is_block) {
        impl_->current_round_.block_hash = share.share_hash;
    }

    // Check if VarDiff adjustment is needed
    if (impl_->vardiff_.ShouldAdjust(*worker)) {
        uint64_t new_diff = impl_->vardiff_.CalculateDifficulty(*worker);
        worker->current_difficulty = new_diff;
        SendSetDifficulty(conn_id, new_diff);
    }

    (void)job_id;  // Job ID validation in full implementation

    return Result<bool>::Ok(true);
}

void MiningPoolServer::SendNotify(uint64_t conn_id, const Work& work) {
    // Build Stratum mining.notify message
    stratum::NotifyParams notify;

    // Generate job ID from work
    std::ostringstream job_id_stream;
    job_id_stream << std::hex << work.height;
    notify.job_id = job_id_stream.str();

    // Convert previous block hash to hex string (reversed for Stratum)
    notify.prev_hash.resize(64);
    for (int i = 0; i < 32; i++) {
        snprintf(&notify.prev_hash[i * 2], 3, "%02x", work.header.prev_block_hash[31 - i]);
    }

    // Coinbase transaction split (simplified)
    notify.coinbase1 = "";
    notify.coinbase2 = "";
    notify.merkle_branches = {};

    // Block version (4 bytes, hex)
    std::ostringstream version_stream;
    version_stream << std::hex << std::setw(8) << std::setfill('0') << work.header.version;
    notify.version = version_stream.str();

    // nBits (difficulty target, 4 bytes hex)
    std::ostringstream nbits_stream;
    nbits_stream << std::hex << std::setw(8) << std::setfill('0') << work.header.bits;
    notify.nbits = nbits_stream.str();

    // nTime (timestamp, 4 bytes hex)
    std::ostringstream ntime_stream;
    ntime_stream << std::hex << std::setw(8) << std::setfill('0')
                 << static_cast<uint32_t>(work.header.timestamp);
    notify.ntime = ntime_stream.str();

    notify.clean_jobs = true;

    // TODO: Send JSON-RPC message over network connection
    (void)conn_id;
}

void MiningPoolServer::SendSetDifficulty(uint64_t conn_id, uint64_t difficulty) {
    // Build Stratum mining.set_difficulty message
    // TODO: Send JSON-RPC message over network connection
    (void)conn_id;
    (void)difficulty;
}

// Security
void MiningPoolServer::BanMiner(uint64_t miner_id, std::chrono::seconds duration) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->miners_.find(miner_id);
    if (it != impl_->miners_.end()) {
        it->second.is_banned = true;
        it->second.ban_expires = std::chrono::system_clock::now() + duration;
    }
}

void MiningPoolServer::UnbanMiner(uint64_t miner_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->miners_.find(miner_id);
    if (it != impl_->miners_.end()) {
        it->second.is_banned = false;
    }
}

bool MiningPoolServer::IsMinerBanned(uint64_t miner_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->miners_.find(miner_id);
    if (it == impl_->miners_.end()) return false;

    if (!it->second.is_banned) return false;

    // Check if ban expired
    auto now = std::chrono::system_clock::now();
    if (now >= it->second.ban_expires) {
        return false;
    }

    return true;
}

void MiningPoolServer::BlockIP(const std::string& ip, std::chrono::seconds duration) {
    std::lock_guard<std::mutex> lock(impl_->security_mutex_);
    impl_->banned_ips_[ip] = std::chrono::system_clock::now() + duration;
}

bool MiningPoolServer::IsIPBlocked(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(impl_->security_mutex_);
    auto it = impl_->banned_ips_.find(ip);
    if (it == impl_->banned_ips_.end()) return false;

    auto now = std::chrono::system_clock::now();
    return now < it->second;
}

void MiningPoolServer::CheckInvalidShares(uint64_t miner_id) {
    if (!impl_->config_.ban_on_invalid_share) return;

    auto it = impl_->miners_.find(miner_id);
    if (it == impl_->miners_.end()) return;

    if (it->second.invalid_share_count >= impl_->config_.max_invalid_shares) {
        BanMiner(miner_id, impl_->config_.ban_duration);
    }
}

} // namespace intcoin
