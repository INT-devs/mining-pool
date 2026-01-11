/*
 * Copyright (c) 2025 INTcoin Team (Neil Adamson)
 * MIT License
 * Mining Pool Server with Stratum Protocol
 */

#ifndef INTCOIN_POOL_H
#define INTCOIN_POOL_H

#include "types.h"
#include "block.h"
#include "transaction.h"
#include "blockchain.h"
#include "mining.h"
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <optional>
#include <atomic>
#include <mutex>
#include <functional>

namespace intcoin {

// ============================================================================
// Pool Configuration
// ============================================================================

struct PoolConfig {
    std::string pool_name;
    std::string pool_address;         // Pool's payout address
    uint16_t stratum_port;            // Stratum server port (default: 2215)
    uint16_t http_port;               // HTTP API port (default: 2216)

    // Mining parameters
    uint64_t min_difficulty;          // Minimum share difficulty
    uint64_t initial_difficulty;      // Initial worker difficulty
    double target_share_time;         // Target time between shares (seconds)
    double vardiff_retarget_time;     // Time between difficulty adjustments
    double vardiff_variance;          // Allowed variance for vardiff

    // Payout parameters
    enum PayoutMethod {
        PPLNS,                        // Pay Per Last N Shares
        PPS,                          // Pay Per Share
        PROP,                         // Proportional
        SOLO                          // Solo mining (winner takes all)
    };
    PayoutMethod payout_method;
    uint64_t pplns_window;            // N shares for PPLNS
    double pool_fee_percent;          // Pool fee (0-100)
    uint64_t min_payout;              // Minimum payout threshold
    uint64_t payout_interval;         // Seconds between payouts

    // Connection limits
    size_t max_workers_per_miner;
    size_t max_miners;
    size_t max_connections_per_ip;

    // Security
    bool require_password;
    bool ban_on_invalid_share;
    size_t max_invalid_shares;
    std::chrono::seconds ban_duration;
};

// ============================================================================
// Share and Work
// ============================================================================

struct Share {
    uint64_t share_id;
    uint64_t miner_id;
    uint64_t worker_id;
    std::string worker_name;
    uint256 job_id;
    uint256 nonce;
    uint256 share_hash;
    uint64_t difficulty;
    bool is_block;                    // True if share is also a valid block
    std::chrono::system_clock::time_point timestamp;
    bool valid;
    std::string error_msg;
};

struct Work {
    uint256 job_id;
    BlockHeader header;
    Transaction coinbase_tx;
    std::vector<Transaction> transactions;
    uint256 merkle_root;
    uint64_t height;
    uint64_t difficulty;
    std::chrono::system_clock::time_point created_at;
    bool clean_jobs;                  // Should miners abandon previous work
};

struct Payment {
    uint64_t payment_id;
    uint64_t miner_id;
    std::string payout_address;
    uint64_t amount;                  // Amount in base units (INTS)
    uint256 tx_hash;                  // Transaction hash
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point confirmed_at;
    bool is_confirmed;
    std::string status;               // "pending", "confirmed", "failed"
};

// ============================================================================
// Miner and Worker
// ============================================================================

struct Worker {
    uint64_t worker_id;
    uint64_t miner_id;
    std::string worker_name;
    std::string user_agent;

    // Statistics
    uint64_t shares_submitted;
    uint64_t shares_accepted;
    uint64_t shares_rejected;
    uint64_t shares_stale;
    uint64_t blocks_found;
    double current_hashrate;          // Hashes per second
    double average_hashrate;          // Average over session

    // Difficulty management
    uint64_t current_difficulty;
    std::chrono::system_clock::time_point last_share_time;
    std::vector<std::chrono::system_clock::time_point> recent_shares;

    // Connection
    std::string ip_address;
    uint16_t port;
    std::chrono::system_clock::time_point connected_at;
    std::chrono::system_clock::time_point last_activity;
    bool is_active;
};

struct Miner {
    uint64_t miner_id;
    std::string username;
    std::string payout_address;
    std::string email;

    // Workers
    std::map<uint64_t, Worker> workers;

    // Statistics (aggregate of all workers)
    uint64_t total_shares_submitted;
    uint64_t total_shares_accepted;
    uint64_t total_shares_rejected;
    uint64_t total_blocks_found;
    double total_hashrate;

    // Earnings
    uint64_t unpaid_balance;          // INTS
    uint64_t paid_balance;            // Total paid out
    uint64_t estimated_earnings;      // Estimated for current round
    std::chrono::system_clock::time_point last_payout;

    // Security
    uint64_t invalid_share_count;
    bool is_banned;
    std::chrono::system_clock::time_point ban_expires;

    // Timestamps
    std::chrono::system_clock::time_point registered_at;
    std::chrono::system_clock::time_point last_seen;
};

// ============================================================================
// Pool Statistics
// ============================================================================

struct PoolStatistics {
    // Network
    uint64_t network_height;
    uint64_t network_difficulty;
    uint64_t network_hashrate;

    // Pool stats
    size_t active_miners;
    size_t active_workers;
    size_t total_connections;
    double pool_hashrate;             // Hashes per second
    double pool_hashrate_percentage;  // % of network hashrate

    // Shares
    uint64_t shares_this_round;
    uint64_t shares_last_hour;
    uint64_t shares_last_day;
    uint64_t total_shares;

    // Blocks
    uint64_t blocks_found;
    uint64_t blocks_pending;          // Awaiting confirmation
    uint64_t blocks_confirmed;
    uint64_t blocks_orphaned;
    std::chrono::system_clock::time_point last_block_found;
    double average_block_time;        // Seconds

    // Earnings
    uint64_t total_paid;              // Total INTS paid to miners
    uint64_t total_unpaid;            // Total unpaid balance
    uint64_t pool_revenue;            // Pool fees collected

    // Performance
    double uptime_hours;
    double efficiency;                // % of valid shares
    double luck;                      // Actual blocks / expected blocks
};

struct RoundStatistics {
    uint64_t round_id;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point ended_at;
    uint64_t shares_submitted;
    uint64_t block_height;
    uint256 block_hash;
    uint64_t block_reward;
    std::map<uint64_t, uint64_t> miner_shares;  // miner_id -> share count
    bool is_complete;
};

// ============================================================================
// Stratum Protocol
// ============================================================================

namespace stratum {

// Stratum message types
enum class MessageType {
    SUBSCRIBE,
    AUTHORIZE,
    SUBMIT,
    NOTIFY,
    SET_DIFFICULTY,
    SET_EXTRANONCE,
    GET_VERSION,
    SHOW_MESSAGE,
    RECONNECT,
    UNKNOWN
};

struct Message {
    MessageType type;
    uint64_t id;
    std::string method;
    std::vector<std::string> params;
    std::optional<std::string> result;
    std::optional<std::string> error;
};

// Stratum responses
struct SubscribeResponse {
    std::vector<std::vector<std::string>> subscriptions;
    std::string extranonce1;
    size_t extranonce2_size;
};

struct NotifyParams {
    std::string job_id;
    std::string prev_hash;
    std::string coinbase1;
    std::string coinbase2;
    std::vector<std::string> merkle_branches;
    std::string version;
    std::string nbits;
    std::string ntime;
    bool clean_jobs;
};

} // namespace stratum

// ============================================================================
// Mining Pool Server
// ============================================================================

class MiningPoolServer {
public:
    /// Constructor
    MiningPoolServer(const PoolConfig& config,
                     std::shared_ptr<Blockchain> blockchain,
                     std::shared_ptr<Miner> miner);

    /// Destructor
    ~MiningPoolServer();

    // ------------------------------------------------------------------------
    // Server Control
    // ------------------------------------------------------------------------

    /// Start pool server
    Result<void> Start();

    /// Stop pool server
    void Stop();

    /// Check if running
    bool IsRunning() const;

    // ------------------------------------------------------------------------
    // Miner Management
    // ------------------------------------------------------------------------

    /// Register new miner
    Result<uint64_t> RegisterMiner(const std::string& username,
                                    const std::string& payout_address,
                                    const std::string& email);

    /// Get miner by ID
    std::optional<Miner> GetMiner(uint64_t miner_id) const;

    /// Get miner by username
    std::optional<Miner> GetMinerByUsername(const std::string& username) const;

    /// Update miner payout address
    Result<void> UpdatePayoutAddress(uint64_t miner_id,
                                     const std::string& new_address);

    /// Get all miners
    std::vector<Miner> GetAllMiners() const;

    /// Get active miners
    std::vector<Miner> GetActiveMiners() const;

    // ------------------------------------------------------------------------
    // Worker Management
    // ------------------------------------------------------------------------

    /// Add worker
    Result<uint64_t> AddWorker(uint64_t miner_id,
                               const std::string& worker_name,
                               const std::string& ip_address,
                               uint16_t port);

    /// Remove worker
    void RemoveWorker(uint64_t worker_id);

    /// Get worker
    std::optional<Worker> GetWorker(uint64_t worker_id) const;

    /// Get miner's workers
    std::vector<Worker> GetMinerWorkers(uint64_t miner_id) const;

    /// Update worker activity
    void UpdateWorkerActivity(uint64_t worker_id);

    /// Disconnect inactive workers
    void DisconnectInactiveWorkers(std::chrono::seconds timeout);

    // ------------------------------------------------------------------------
    // Share Processing
    // ------------------------------------------------------------------------

    /// Submit share
    Result<void> SubmitShare(uint64_t worker_id,
                            const uint256& job_id,
                            const uint256& nonce,
                            const uint256& share_hash);

    /// Validate share
    Result<bool> ValidateShare(const Share& share);

    /// Process valid share
    void ProcessValidShare(const Share& share);

    /// Process block found
    Result<void> ProcessBlockFound(const Share& share);

    /// Get recent shares
    std::vector<Share> GetRecentShares(size_t count) const;

    /// Get miner shares
    std::vector<Share> GetMinerShares(uint64_t miner_id, size_t count) const;

    // ------------------------------------------------------------------------
    // Work Management
    // ------------------------------------------------------------------------

    /// Create new work
    Result<Work> CreateWork(bool clean_jobs = false);

    /// Get current work
    std::optional<Work> GetCurrentWork() const;

    /// Update work (on new block)
    Result<void> UpdateWork();

    /// Broadcast work to all miners
    void BroadcastWork(const Work& work);

    // ------------------------------------------------------------------------
    // Difficulty Management (VarDiff)
    // ------------------------------------------------------------------------

    /// Calculate worker difficulty
    uint64_t CalculateWorkerDifficulty(uint64_t worker_id) const;

    /// Adjust worker difficulty
    void AdjustWorkerDifficulty(uint64_t worker_id);

    /// Set worker difficulty
    void SetWorkerDifficulty(uint64_t worker_id, uint64_t difficulty);

    /// Adjust all difficulties
    void AdjustAllDifficulties();

    // ------------------------------------------------------------------------
    // Payout System
    // ------------------------------------------------------------------------

    /// Calculate PPLNS payouts
    std::map<uint64_t, uint64_t> CalculatePPLNSPayouts(uint64_t block_reward);

    /// Calculate PPS payouts
    std::map<uint64_t, uint64_t> CalculatePPSPayouts();

    /// Process payouts
    Result<void> ProcessPayouts();

    /// Get miner balance
    uint64_t GetMinerBalance(uint64_t miner_id) const;

    /// Get miner estimated earnings
    uint64_t GetMinerEstimatedEarnings(uint64_t miner_id) const;

    /// Get payment history
    std::vector<Payment> GetPaymentHistory(size_t limit = 100) const;

    /// Get payment history for specific miner
    std::vector<Payment> GetMinerPaymentHistory(uint64_t miner_id, size_t limit = 100) const;

    // ------------------------------------------------------------------------
    // Statistics
    // ------------------------------------------------------------------------

    /// Get pool statistics
    PoolStatistics GetStatistics() const;

    /// Get current round statistics
    RoundStatistics GetCurrentRound() const;

    /// Get round history
    std::vector<RoundStatistics> GetRoundHistory(size_t count) const;

    /// Calculate pool hashrate
    double CalculatePoolHashrate() const;

    /// Calculate worker hashrate
    double CalculateWorkerHashrate(uint64_t worker_id) const;

    /// Calculate miner hashrate
    double CalculateMinerHashrate(uint64_t miner_id) const;

    // ------------------------------------------------------------------------
    // Stratum Protocol
    // ------------------------------------------------------------------------

    /// Handle Stratum message
    Result<stratum::Message> HandleStratumMessage(const std::string& json);

    /// Handle subscribe
    Result<stratum::SubscribeResponse> HandleSubscribe(uint64_t conn_id);

    /// Handle authorize
    Result<bool> HandleAuthorize(uint64_t conn_id,
                                 const std::string& username,
                                 const std::string& password);

    /// Handle submit
    Result<bool> HandleSubmit(uint64_t conn_id,
                             const std::string& job_id,
                             const std::string& nonce,
                             const std::string& result);

    /// Send Stratum notify
    void SendNotify(uint64_t conn_id, const Work& work);

    /// Send difficulty update
    void SendSetDifficulty(uint64_t conn_id, uint64_t difficulty);

    // ------------------------------------------------------------------------
    // Security
    // ------------------------------------------------------------------------

    /// Ban miner
    void BanMiner(uint64_t miner_id, std::chrono::seconds duration);

    /// Unban miner
    void UnbanMiner(uint64_t miner_id);

    /// Check if miner is banned
    bool IsMinerBanned(uint64_t miner_id) const;

    /// Block IP address
    void BlockIP(const std::string& ip, std::chrono::seconds duration);

    /// Check if IP is blocked
    bool IsIPBlocked(const std::string& ip) const;

    /// Check invalid shares and auto-ban if needed
    void CheckInvalidShares(uint64_t miner_id);

    // ------------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------------

    /// Get configuration
    const PoolConfig& GetConfig() const;

    /// Update configuration
    void UpdateConfig(const PoolConfig& config);

    // ------------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------------

    /// Callback for new block found
    using BlockFoundCallback = std::function<void(const Block&, uint64_t miner_id)>;

    /// Callback for payout processed
    using PayoutCallback = std::function<void(uint64_t miner_id, uint64_t amount)>;

    /// Register block found callback
    void RegisterBlockFoundCallback(BlockFoundCallback callback);

    /// Register payout callback
    void RegisterPayoutCallback(PayoutCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Share Validator
// ============================================================================

class ShareValidator {
public:
    /// Validate share meets difficulty
    static bool ValidateDifficulty(const uint256& hash, uint64_t difficulty);

    /// Validate share is for current work
    static bool ValidateWork(const Share& share, const Work& work);

    /// Check if share is also a valid block
    static bool IsValidBlock(const uint256& hash, uint64_t network_difficulty);

    /// Validate share timestamps
    static bool ValidateTimestamp(const Share& share, const Work& work);

    /// Detect duplicate share
    static bool IsDuplicateShare(const Share& share,
                                 const std::vector<Share>& recent_shares);
};

// ============================================================================
// Variable Difficulty Manager
// ============================================================================

class VarDiffManager {
public:
    VarDiffManager(double target_share_time, double retarget_time, double variance);

    /// Calculate new difficulty for worker
    uint64_t CalculateDifficulty(const Worker& worker) const;

    /// Check if difficulty adjustment needed
    bool ShouldAdjust(const Worker& worker) const;

    /// Get share rate (shares per second)
    double GetShareRate(const Worker& worker) const;

private:
    double target_share_time_;
    double retarget_time_;
    double variance_;
};

// ============================================================================
// Payout Calculator
// ============================================================================

class PayoutCalculator {
public:
    /// Calculate PPLNS (Pay Per Last N Shares)
    static std::map<uint64_t, uint64_t> CalculatePPLNS(
        const std::vector<Share>& shares,
        size_t n_shares,
        uint64_t block_reward,
        double pool_fee);

    /// Calculate PPS (Pay Per Share)
    static std::map<uint64_t, uint64_t> CalculatePPS(
        const std::vector<Share>& shares,
        uint64_t expected_shares_per_block,
        uint64_t block_reward,
        double pool_fee);

    /// Calculate proportional
    static std::map<uint64_t, uint64_t> CalculateProportional(
        const std::vector<Share>& round_shares,
        uint64_t block_reward,
        double pool_fee);

    /// Calculate pool fee
    static uint64_t CalculateFee(uint64_t amount, double fee_percent);
};

// ============================================================================
// Hashrate Calculator
// ============================================================================

class HashrateCalculator {
public:
    /// Calculate hashrate from shares
    static double CalculateHashrate(const std::vector<Share>& shares,
                                    std::chrono::seconds window);

    /// Calculate hashrate from difficulty and time
    static double CalculateHashrateFromDifficulty(uint64_t difficulty,
                                                   std::chrono::seconds time);

    /// Estimate time to find block
    static std::chrono::seconds EstimateBlockTime(double pool_hashrate,
                                                   uint64_t network_difficulty);

    /// Calculate expected shares for block
    static uint64_t CalculateExpectedShares(uint64_t network_difficulty,
                                            uint64_t share_difficulty);
};

// ============================================================================
// Utility Functions
// ============================================================================

/// Convert payout method to string
std::string ToString(PoolConfig::PayoutMethod method);

/// Parse Stratum message from JSON
Result<stratum::Message> ParseStratumMessage(const std::string& json);

/// Format Stratum response to JSON
std::string FormatStratumResponse(const stratum::Message& msg);

/// Calculate share difficulty from hash
uint64_t CalculateShareDifficulty(const uint256& hash);

/// Generate job ID
uint256 GenerateJobID();

} // namespace intcoin

#endif // INTCOIN_POOL_H
