/*
 * Copyright (c) 2025 INTcoin Team (Neil Adamson)
 * MIT License
 * Mining Pool Server Implementation
 */

#include "intcoin/pool.h"
#include "intcoin/consensus.h"
#include "intcoin/util.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <thread>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace intcoin {

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
        , miner_(miner)
        , is_running_(false)
        , next_miner_id_(1)
        , next_worker_id_(1)
        , next_share_id_(1)
        , current_round_id_(1)
        , vardiff_manager_(config.target_share_time, config.vardiff_retarget_time, config.vardiff_variance)
    {
        current_round_.round_id = current_round_id_;
        current_round_.started_at = std::chrono::system_clock::now();
        current_round_.is_complete = false;
    }

    // Configuration
    PoolConfig config_;

    // Blockchain connection
    std::shared_ptr<Blockchain> blockchain_;
    std::shared_ptr<Miner> miner_;

    // Server state
    std::atomic<bool> is_running_;
    std::mutex mutex_;

    // Miners and workers
    std::map<uint64_t, intcoin::Miner> miners_;                // miner_id -> Miner
    std::map<std::string, uint64_t> username_to_id_;           // username -> miner_id
    std::map<uint64_t, Worker> workers_;                       // worker_id -> Worker
    std::map<uint64_t, std::vector<uint64_t>> miner_workers_;  // miner_id -> [worker_ids]

    // ID generators
    std::atomic<uint64_t> next_miner_id_;
    std::atomic<uint64_t> next_worker_id_;
    std::atomic<uint64_t> next_share_id_;

    // Work and shares
    std::optional<Work> current_work_;
    std::vector<Share> recent_shares_;
    std::map<uint64_t, std::vector<Share>> miner_shares_;  // miner_id -> shares

    // Round tracking (for PPLNS)
    std::atomic<uint64_t> current_round_id_;
    RoundStatistics current_round_;
    std::vector<RoundStatistics> round_history_;

    // VarDiff manager
    VarDiffManager vardiff_manager_;

    // Statistics
    std::atomic<uint64_t> total_shares_submitted_{0};
    std::atomic<uint64_t> total_blocks_found_{0};
    std::atomic<uint64_t> total_paid_{0};
    std::atomic<uint64_t> pool_revenue_{0};
    std::chrono::system_clock::time_point server_start_time_;

    // Payment tracking
    std::atomic<uint64_t> next_payment_id_{1};
    std::vector<Payment> payment_history_;

    // Helper methods
    uint64_t GenerateMinerId() { return next_miner_id_++; }
    uint64_t GenerateWorkerId() { return next_worker_id_++; }
    uint64_t GenerateShareId() { return next_share_id_++; }
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

MiningPoolServer::MiningPoolServer(const PoolConfig& config,
                                   std::shared_ptr<Blockchain> blockchain,
                                   std::shared_ptr<Miner> miner)
    : impl_(std::make_unique<Impl>(config, blockchain, miner)) {
    impl_->server_start_time_ = std::chrono::system_clock::now();
}

MiningPoolServer::~MiningPoolServer() {
    Stop();
}

// ============================================================================
// Server Control
// ============================================================================

Result<void> MiningPoolServer::Start() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    if (impl_->is_running_) {
        return Result<void>::Error("Pool server already running");
    }

    // Create initial work
    auto work_result = CreateWork(true);
    if (work_result.IsError()) {
        return Result<void>::Error("Failed to create initial work: " + work_result.error);
    }

    impl_->is_running_ = true;
    return Result<void>::Ok();
}

void MiningPoolServer::Stop() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->is_running_ = false;
}

bool MiningPoolServer::IsRunning() const {
    return impl_->is_running_;
}

// ============================================================================
// Miner Management
// ============================================================================

Result<uint64_t> MiningPoolServer::RegisterMiner(const std::string& username,
                                                  const std::string& payout_address,
                                                  const std::string& email) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Check if username already exists
    if (impl_->username_to_id_.count(username) > 0) {
        return Result<uint64_t>::Error("Username already registered");
    }

    // Create new miner
    intcoin::Miner new_miner;
    new_miner.miner_id = impl_->GenerateMinerId();
    new_miner.username = username;
    new_miner.payout_address = payout_address;
    new_miner.email = email;
    new_miner.total_shares_submitted = 0;
    new_miner.total_shares_accepted = 0;
    new_miner.total_shares_rejected = 0;
    new_miner.total_blocks_found = 0;
    new_miner.total_hashrate = 0.0;
    new_miner.unpaid_balance = 0;
    new_miner.paid_balance = 0;
    new_miner.estimated_earnings = 0;
    new_miner.invalid_share_count = 0;
    new_miner.is_banned = false;
    new_miner.registered_at = std::chrono::system_clock::now();
    new_miner.last_seen = std::chrono::system_clock::now();

    impl_->miners_[new_miner.miner_id] = new_miner;
    impl_->username_to_id_[username] = new_miner.miner_id;

    return Result<uint64_t>::Ok(new_miner.miner_id);
}

std::optional<intcoin::Miner> MiningPoolServer::GetMiner(uint64_t miner_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->miners_.find(miner_id);
    if (it == impl_->miners_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<intcoin::Miner> MiningPoolServer::GetMinerByUsername(const std::string& username) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->username_to_id_.find(username);
    if (it == impl_->username_to_id_.end()) {
        return std::nullopt;
    }

    return GetMiner(it->second);
}

Result<void> MiningPoolServer::UpdatePayoutAddress(uint64_t miner_id,
                                                    const std::string& new_address) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->miners_.find(miner_id);
    if (it == impl_->miners_.end()) {
        return Result<void>::Error("Miner not found");
    }

    it->second.payout_address = new_address;
    return Result<void>::Ok();
}

std::vector<intcoin::Miner> MiningPoolServer::GetAllMiners() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    std::vector<intcoin::Miner> miners;
    for (const auto& [id, miner] : impl_->miners_) {
        miners.push_back(miner);
    }
    return miners;
}

std::vector<intcoin::Miner> MiningPoolServer::GetActiveMiners() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    std::vector<intcoin::Miner> active_miners;
    auto now = std::chrono::system_clock::now();

    for (const auto& [id, miner] : impl_->miners_) {
        auto inactive_duration = std::chrono::duration_cast<std::chrono::minutes>(
            now - miner.last_seen);

        if (inactive_duration.count() < 30) {  // Active in last 30 minutes
            active_miners.push_back(miner);
        }
    }

    return active_miners;
}

// ============================================================================
// Worker Management
// ============================================================================

Result<uint64_t> MiningPoolServer::AddWorker(uint64_t miner_id,
                                              const std::string& worker_name,
                                              const std::string& ip_address,
                                              uint16_t port) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Verify miner exists
    if (impl_->miners_.find(miner_id) == impl_->miners_.end()) {
        return Result<uint64_t>::Error("Miner not found");
    }

    // Create new worker
    Worker new_worker;
    new_worker.worker_id = impl_->GenerateWorkerId();
    new_worker.miner_id = miner_id;
    new_worker.worker_name = worker_name;
    new_worker.shares_submitted = 0;
    new_worker.shares_accepted = 0;
    new_worker.shares_rejected = 0;
    new_worker.shares_stale = 0;
    new_worker.blocks_found = 0;
    new_worker.current_hashrate = 0.0;
    new_worker.average_hashrate = 0.0;
    new_worker.current_difficulty = impl_->config_.initial_difficulty;
    new_worker.last_share_time = std::chrono::system_clock::now();
    new_worker.ip_address = ip_address;
    new_worker.port = port;
    new_worker.connected_at = std::chrono::system_clock::now();
    new_worker.last_activity = std::chrono::system_clock::now();
    new_worker.is_active = true;

    impl_->workers_[new_worker.worker_id] = new_worker;
    impl_->miner_workers_[miner_id].push_back(new_worker.worker_id);

    return Result<uint64_t>::Ok(new_worker.worker_id);
}

void MiningPoolServer::RemoveWorker(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->workers_.find(worker_id);
    if (it == impl_->workers_.end()) {
        return;
    }

    uint64_t miner_id = it->second.miner_id;

    // Remove from miner's worker list
    auto& miner_worker_list = impl_->miner_workers_[miner_id];
    miner_worker_list.erase(
        std::remove(miner_worker_list.begin(), miner_worker_list.end(), worker_id),
        miner_worker_list.end()
    );

    // Remove worker
    impl_->workers_.erase(it);
}

std::optional<Worker> MiningPoolServer::GetWorker(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->workers_.find(worker_id);
    if (it == impl_->workers_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::vector<Worker> MiningPoolServer::GetMinerWorkers(uint64_t miner_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    std::vector<Worker> workers;

    auto it = impl_->miner_workers_.find(miner_id);
    if (it == impl_->miner_workers_.end()) {
        return workers;
    }

    for (uint64_t worker_id : it->second) {
        auto worker_it = impl_->workers_.find(worker_id);
        if (worker_it != impl_->workers_.end()) {
            workers.push_back(worker_it->second);
        }
    }

    return workers;
}

void MiningPoolServer::UpdateWorkerActivity(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->workers_.find(worker_id);
    if (it != impl_->workers_.end()) {
        it->second.last_activity = std::chrono::system_clock::now();
    }
}

void MiningPoolServer::DisconnectInactiveWorkers(std::chrono::seconds timeout) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto now = std::chrono::system_clock::now();
    std::vector<uint64_t> to_remove;

    for (auto& [worker_id, worker] : impl_->workers_) {
        auto inactive_duration = std::chrono::duration_cast<std::chrono::seconds>(
            now - worker.last_activity);

        if (inactive_duration >= timeout) {
            to_remove.push_back(worker_id);
        }
    }

    for (uint64_t worker_id : to_remove) {
        RemoveWorker(worker_id);
    }
}

// ============================================================================
// Share Processing
// ============================================================================

Result<void> MiningPoolServer::SubmitShare(uint64_t worker_id,
                                            const uint256& job_id,
                                            const uint256& nonce,
                                            const uint256& share_hash) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Get worker
    auto worker_it = impl_->workers_.find(worker_id);
    if (worker_it == impl_->workers_.end()) {
        return Result<void>::Error("Worker not found");
    }

    Worker& worker = worker_it->second;

    // Create share
    Share share;
    share.share_id = impl_->GenerateShareId();
    share.miner_id = worker.miner_id;
    share.worker_id = worker_id;
    share.worker_name = worker.worker_name;
    share.job_id = job_id;
    share.nonce = nonce;
    share.share_hash = share_hash;
    share.difficulty = worker.current_difficulty;
    share.timestamp = std::chrono::system_clock::now();
    share.valid = false;
    share.is_block = false;

    // Validate share
    auto validate_result = ValidateShare(share);
    if (validate_result.IsError()) {
        share.valid = false;
        share.error_msg = validate_result.error;
        worker.shares_rejected++;

        // Update miner's invalid share count
        auto& miner = impl_->miners_[worker.miner_id];
        miner.invalid_share_count++;

        // Ban if too many invalid shares
        if (impl_->config_.ban_on_invalid_share &&
            miner.invalid_share_count >= impl_->config_.max_invalid_shares) {
            BanMiner(worker.miner_id, impl_->config_.ban_duration);
        }

        return Result<void>::Error(share.error_msg);
    }

    share.valid = validate_result.GetValue();

    if (share.valid) {
        ProcessValidShare(share);
    }

    // Store share
    impl_->recent_shares_.push_back(share);
    impl_->miner_shares_[worker.miner_id].push_back(share);

    // Keep only recent shares (last 1000)
    if (impl_->recent_shares_.size() > 1000) {
        impl_->recent_shares_.erase(impl_->recent_shares_.begin());
    }

    impl_->total_shares_submitted_++;

    return Result<void>::Ok();
}

Result<bool> MiningPoolServer::ValidateShare(const Share& share) {
    // Validate share difficulty
    if (!ShareValidator::ValidateDifficulty(share.share_hash, share.difficulty)) {
        return Result<bool>::Error("Share does not meet difficulty requirement");
    }

    // Check if this is a valid block
    if (impl_->current_work_.has_value()) {
        auto network_difficulty = impl_->current_work_->difficulty;
        if (ShareValidator::IsValidBlock(share.share_hash, network_difficulty)) {
            const_cast<Share&>(share).is_block = true;
        }
    }

    return Result<bool>::Ok(true);
}

void MiningPoolServer::ProcessValidShare(const Share& share) {
    // Update worker stats
    auto worker_it = impl_->workers_.find(share.worker_id);
    if (worker_it != impl_->workers_.end()) {
        Worker& worker = worker_it->second;
        worker.shares_accepted++;
        worker.shares_submitted++;
        worker.last_share_time = std::chrono::system_clock::now();
        worker.recent_shares.push_back(share.timestamp);

        // Keep only recent shares (last 100)
        if (worker.recent_shares.size() > 100) {
            worker.recent_shares.erase(worker.recent_shares.begin());
        }

        // Adjust difficulty if needed
        if (impl_->vardiff_manager_.ShouldAdjust(worker)) {
            AdjustWorkerDifficulty(share.worker_id);
        }
    }

    // Update miner stats
    auto& miner = impl_->miners_[share.miner_id];
    miner.total_shares_accepted++;
    miner.total_shares_submitted++;
    miner.last_seen = std::chrono::system_clock::now();

    // Add to current round
    impl_->current_round_.shares_submitted++;
    impl_->current_round_.miner_shares[share.miner_id]++;

    // Process block if found
    if (share.is_block) {
        ProcessBlockFound(share);
    }
}

Result<void> MiningPoolServer::ProcessBlockFound(const Share& share) {
    // Update block stats
    auto& worker = impl_->workers_[share.worker_id];
    worker.blocks_found++;

    auto& miner = impl_->miners_[share.miner_id];
    miner.total_blocks_found++;

    impl_->total_blocks_found_++;

    // Finalize current round
    impl_->current_round_.is_complete = true;
    impl_->current_round_.ended_at = std::chrono::system_clock::now();
    impl_->current_round_.block_hash = share.share_hash;

    // Reconstruct full block from work and share
    if (impl_->current_work_.has_value()) {
        Block found_block;
        found_block.header = impl_->current_work_->header;
        found_block.header.nonce = static_cast<uint64_t>(share.nonce[0]);  // Simplified nonce extraction
        found_block.transactions = impl_->current_work_->transactions;
        found_block.transactions.insert(found_block.transactions.begin(), impl_->current_work_->coinbase_tx);

        // Recalculate merkle root with final nonce
        found_block.header.merkle_root = found_block.CalculateMerkleRoot();

        // Submit block to blockchain
        auto submit_result = impl_->blockchain_->SubmitBlock(found_block);
        if (submit_result.IsError()) {
            // Block rejected - likely orphaned or invalid
            impl_->current_round_.block_hash.fill(0);  // Mark as failed
            return Result<void>::Error("Block submission failed: " + submit_result.error);
        }

        impl_->current_round_.block_height = found_block.GetHeight();
        impl_->current_round_.block_reward = found_block.transactions[0].outputs[0].value;
    }

    // Store round in history
    impl_->round_history_.push_back(impl_->current_round_);

    // Start new round
    impl_->current_round_id_++;
    impl_->current_round_ = RoundStatistics{};
    impl_->current_round_.round_id = impl_->current_round_id_;
    impl_->current_round_.started_at = std::chrono::system_clock::now();
    impl_->current_round_.is_complete = false;

    // Create new work for miners
    auto new_work_result = CreateWork(true);
    if (new_work_result.IsOk()) {
        BroadcastWork(new_work_result.GetValue());
    }

    return Result<void>::Ok();
}

std::vector<Share> MiningPoolServer::GetRecentShares(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    size_t start = impl_->recent_shares_.size() > count ?
                   impl_->recent_shares_.size() - count : 0;

    return std::vector<Share>(
        impl_->recent_shares_.begin() + start,
        impl_->recent_shares_.end()
    );
}

std::vector<Share> MiningPoolServer::GetMinerShares(uint64_t miner_id, size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->miner_shares_.find(miner_id);
    if (it == impl_->miner_shares_.end()) {
        return {};
    }

    const auto& shares = it->second;
    size_t start = shares.size() > count ? shares.size() - count : 0;

    return std::vector<Share>(shares.begin() + start, shares.end());
}

// ============================================================================
// Work Management
// ============================================================================

Result<Work> MiningPoolServer::CreateWork(bool clean_jobs) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Get block template from blockchain
    // Use pool address for coinbase payout
    PublicKey pool_pubkey;  // TODO: Parse impl_->config_.pool_address to PublicKey
    auto template_result = impl_->blockchain_->GetBlockTemplate(pool_pubkey);
    if (template_result.IsError()) {
        return Result<Work>::Error("Failed to get block template: " + template_result.error);
    }

    auto block_template = template_result.GetValue();

    // Create work
    Work work;
    work.job_id = GenerateJobID();
    work.header = block_template.header;
    work.coinbase_tx = block_template.transactions[0];
    work.transactions.assign(block_template.transactions.begin() + 1, block_template.transactions.end());
    work.merkle_root = block_template.header.merkle_root;
    work.height = block_template.GetHeight();
    work.difficulty = impl_->blockchain_->GetDifficulty();
    work.created_at = std::chrono::system_clock::now();
    work.clean_jobs = clean_jobs;

    impl_->current_work_ = work;

    return Result<Work>::Ok(work);
}

std::optional<Work> MiningPoolServer::GetCurrentWork() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->current_work_;
}

Result<void> MiningPoolServer::UpdateWork() {
    auto work_result = CreateWork(true);
    if (work_result.IsError()) {
        return Result<void>::Error(work_result.error);
    }

    // Broadcast to all workers (would be implemented in Stratum server)
    // BroadcastWork(work_result.GetValue());

    return Result<void>::Ok();
}

void MiningPoolServer::BroadcastWork(const Work& work) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Send mining.notify to all active workers
    for (const auto& [worker_id, worker] : impl_->workers_) {
        if (worker.is_active) {
            // Send notify message via Stratum
            // Note: In full implementation, this would use the worker's connection ID
            // For now, we use worker_id as connection ID
            SendNotify(worker_id, work);
        }
    }

    LogF(LogLevel::DEBUG, "Broadcast new work (job_id: %s) to %zu active workers",
         ToHex(work.job_id).c_str(), impl_->workers_.size());
}

// ============================================================================
// Difficulty Management
// ============================================================================

uint64_t MiningPoolServer::CalculateWorkerDifficulty(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->workers_.find(worker_id);
    if (it == impl_->workers_.end()) {
        return impl_->config_.initial_difficulty;
    }

    return impl_->vardiff_manager_.CalculateDifficulty(it->second);
}

void MiningPoolServer::AdjustWorkerDifficulty(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->workers_.find(worker_id);
    if (it == impl_->workers_.end()) {
        return;
    }

    uint64_t old_diff = it->second.current_difficulty;
    uint64_t new_diff = impl_->vardiff_manager_.CalculateDifficulty(it->second);

    // Only update if difficulty changed significantly
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
        if (impl_->vardiff_manager_.ShouldAdjust(worker)) {
            uint64_t old_diff = worker.current_difficulty;
            uint64_t new_diff = impl_->vardiff_manager_.CalculateDifficulty(worker);

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

// ============================================================================
// Statistics (Stubs - to be implemented)
// ============================================================================

PoolStatistics MiningPoolServer::GetStatistics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    PoolStatistics stats{};

    // Network statistics
    stats.network_height = impl_->blockchain_->GetBestHeight();
    double network_difficulty_double = impl_->blockchain_->GetDifficulty();
    stats.network_difficulty = static_cast<uint64_t>(network_difficulty_double);

    // Estimate network hashrate from difficulty
    // Network hashrate ≈ difficulty * 2^32 / block_time
    const uint64_t target_block_time = 120; // 2 minutes in seconds
    stats.network_hashrate = static_cast<uint64_t>(
        network_difficulty_double * 4294967296.0 / target_block_time);

    // Pool statistics
    stats.active_miners = GetActiveMiners().size();

    // Count active workers
    stats.active_workers = 0;
    for (const auto& [worker_id, worker] : impl_->workers_) {
        if (worker.is_active) {
            stats.active_workers++;
        }
    }

    stats.total_connections = impl_->workers_.size();
    stats.pool_hashrate = CalculatePoolHashrate();

    // Calculate pool hashrate as percentage of network
    if (stats.network_hashrate > 0) {
        stats.pool_hashrate_percentage = (stats.pool_hashrate / stats.network_hashrate) * 100.0;
    } else {
        stats.pool_hashrate_percentage = 0.0;
    }

    // Share statistics
    auto now = std::chrono::system_clock::now();
    auto cutoff_1h = now - std::chrono::hours(1);
    auto cutoff_24h = now - std::chrono::hours(24);

    stats.shares_this_round = impl_->current_round_.shares_submitted;
    stats.shares_last_hour = 0;
    stats.shares_last_day = 0;

    for (const auto& share : impl_->recent_shares_) {
        if (share.valid) {
            if (share.timestamp >= cutoff_1h) {
                stats.shares_last_hour++;
            }
            if (share.timestamp >= cutoff_24h) {
                stats.shares_last_day++;
            }
        }
    }

    stats.total_shares = impl_->total_shares_submitted_;

    // Block statistics
    stats.blocks_found = impl_->total_blocks_found_;
    stats.blocks_pending = 0;
    stats.blocks_confirmed = 0;
    stats.blocks_orphaned = 0;
    stats.last_block_found = std::chrono::system_clock::time_point{};

    // Count block statuses from round history
    std::chrono::system_clock::time_point latest_block_time;
    uint64_t total_block_time_seconds = 0;
    size_t block_count = 0;

    for (const auto& round : impl_->round_history_) {
        if (round.is_complete && round.block_height > 0) {
            block_count++;

            // Track latest block
            if (round.ended_at > latest_block_time) {
                latest_block_time = round.ended_at;
            }

            // Calculate block time
            auto round_duration = std::chrono::duration_cast<std::chrono::seconds>(
                round.ended_at - round.started_at).count();
            total_block_time_seconds += round_duration;

            // For now, assume all blocks are confirmed (in full implementation,
            // would check blockchain confirmation status)
            stats.blocks_confirmed++;
        }
    }

    stats.last_block_found = latest_block_time;

    // Calculate average block time
    if (block_count > 0) {
        stats.average_block_time = (double)total_block_time_seconds / block_count;
    } else {
        stats.average_block_time = 0.0;
    }

    // Earnings statistics
    stats.total_paid = impl_->total_paid_;
    stats.pool_revenue = impl_->pool_revenue_;

    // Calculate total unpaid balance
    stats.total_unpaid = 0;
    for (const auto& [miner_id, miner] : impl_->miners_) {
        stats.total_unpaid += miner.unpaid_balance;
    }

    // Performance statistics
    auto uptime = std::chrono::duration_cast<std::chrono::hours>(
        now - impl_->server_start_time_).count();
    stats.uptime_hours = static_cast<double>(uptime);

    // Efficiency: valid shares / total shares
    if (impl_->total_shares_submitted_ > 0) {
        uint64_t valid_shares = 0;
        for (const auto& share : impl_->recent_shares_) {
            if (share.valid) {
                valid_shares++;
            }
        }
        // Use recent shares as sample (more accurate than total)
        if (impl_->recent_shares_.size() > 0) {
            stats.efficiency = ((double)valid_shares / impl_->recent_shares_.size()) * 100.0;
        } else {
            stats.efficiency = 100.0; // Assume 100% if no recent data
        }
    } else {
        stats.efficiency = 0.0;
    }

    // Luck: actual blocks found / expected blocks
    // Expected blocks = pool_hashrate / network_hashrate * time_elapsed / block_time
    if (stats.network_hashrate > 0 && stats.pool_hashrate > 0 && uptime > 0) {
        double expected_blocks = (stats.pool_hashrate / stats.network_hashrate) *
                                (uptime * 3600.0 / 120.0); // 120s block time
        if (expected_blocks > 0) {
            stats.luck = ((double)stats.blocks_found / expected_blocks) * 100.0;
        } else {
            stats.luck = 100.0;
        }
    } else {
        stats.luck = 100.0; // Default to 100% if insufficient data
    }

    return stats;
}

RoundStatistics MiningPoolServer::GetCurrentRound() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->current_round_;
}

std::vector<RoundStatistics> MiningPoolServer::GetRoundHistory(size_t count) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    size_t start = impl_->round_history_.size() > count ?
                   impl_->round_history_.size() - count : 0;

    return std::vector<RoundStatistics>(
        impl_->round_history_.begin() + start,
        impl_->round_history_.end()
    );
}

double MiningPoolServer::CalculatePoolHashrate() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    double total_hashrate = 0.0;

    for (const auto& [worker_id, worker] : impl_->workers_) {
        if (worker.is_active) {
            total_hashrate += CalculateWorkerHashrate(worker_id);
        }
    }

    return total_hashrate;
}

double MiningPoolServer::CalculateWorkerHashrate(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->workers_.find(worker_id);
    if (it == impl_->workers_.end()) {
        return 0.0;
    }

    const Worker& worker = it->second;

    // Calculate hashrate from recent shares
    if (worker.recent_shares.size() < 2) {
        return 0.0;
    }

    auto time_span = std::chrono::duration_cast<std::chrono::seconds>(
        worker.recent_shares.back() - worker.recent_shares.front());

    if (time_span.count() == 0) {
        return 0.0;
    }

    // Hashrate = (shares * difficulty * 2^32) / time_in_seconds
    // This gives an approximate hash rate based on share submissions
    size_t share_count = worker.recent_shares.size();
    double difficulty = static_cast<double>(worker.current_difficulty);
    double time_seconds = static_cast<double>(time_span.count());

    // 2^32 ≈ 4.3 billion (average hashes per difficulty-1 share)
    const double HASHES_PER_SHARE = 4294967296.0;

    double hashrate = (share_count * difficulty * HASHES_PER_SHARE) / time_seconds;

    return hashrate;
}

double MiningPoolServer::CalculateMinerHashrate(uint64_t miner_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    double total_hashrate = 0.0;

    auto it = impl_->miner_workers_.find(miner_id);
    if (it == impl_->miner_workers_.end()) {
        return 0.0;
    }

    for (uint64_t worker_id : it->second) {
        total_hashrate += CalculateWorkerHashrate(worker_id);
    }

    return total_hashrate;
}

// ============================================================================
// Payout System Implementation
// ============================================================================

std::map<uint64_t, uint64_t> MiningPoolServer::CalculatePPLNSPayouts(uint64_t block_reward) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Calculate pool fee
    uint64_t fee = static_cast<uint64_t>(block_reward * impl_->config_.pool_fee_percent / 100.0);
    uint64_t total_payout = block_reward - fee;

    // Get last N shares for PPLNS window
    size_t n_shares = impl_->config_.pplns_window;
    size_t total_shares = impl_->recent_shares_.size();

    if (total_shares == 0) {
        return {};  // No shares to pay out
    }

    // Determine the range of shares to consider
    size_t start_index = (total_shares > n_shares) ? (total_shares - n_shares) : 0;

    // Count shares per miner in the PPLNS window
    std::map<uint64_t, uint64_t> miner_share_count;
    uint64_t window_share_count = 0;

    for (size_t i = start_index; i < total_shares; i++) {
        const Share& share = impl_->recent_shares_[i];
        if (share.valid) {
            miner_share_count[share.miner_id]++;
            window_share_count++;
        }
    }

    if (window_share_count == 0) {
        return {};  // No valid shares in window
    }

    // Calculate payout proportional to share count
    std::map<uint64_t, uint64_t> payouts;
    for (const auto& [miner_id, share_count] : miner_share_count) {
        uint64_t miner_payout = (total_payout * share_count) / window_share_count;
        payouts[miner_id] = miner_payout;

        // Update miner's unpaid balance
        auto it = impl_->miners_.find(miner_id);
        if (it != impl_->miners_.end()) {
            it->second.unpaid_balance += miner_payout;
        }
    }

    // Update pool statistics
    impl_->total_paid_ += total_payout;
    impl_->pool_revenue_ += fee;

    return payouts;
}

std::map<uint64_t, uint64_t> MiningPoolServer::CalculatePPSPayouts() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Get network difficulty to calculate expected shares per block
    double network_diff = impl_->blockchain_ ? impl_->blockchain_->GetDifficulty() : 1.0;
    double share_diff = static_cast<double>(impl_->config_.initial_difficulty);
    uint64_t expected_shares = static_cast<uint64_t>(network_diff / share_diff);

    if (expected_shares == 0) {
        expected_shares = 1;  // Prevent division by zero
    }

    // Get block reward (TODO: Should come from blockchain/consensus)
    uint64_t block_reward = 50 * 1000000ULL;  // 50 INTS in base units (1 INT = 1,000,000 INTS)

    // Calculate pool fee
    uint64_t fee = static_cast<uint64_t>(block_reward * impl_->config_.pool_fee_percent / 100.0);
    uint64_t reward_per_share = (block_reward - fee) / expected_shares;

    // Calculate payout for each share submitted
    std::map<uint64_t, uint64_t> payouts;

    for (const auto& share : impl_->recent_shares_) {
        if (share.valid) {
            payouts[share.miner_id] += reward_per_share;

            // Update miner's unpaid balance
            auto it = impl_->miners_.find(share.miner_id);
            if (it != impl_->miners_.end()) {
                it->second.unpaid_balance += reward_per_share;
            }
        }
    }

    // Update pool statistics
    uint64_t total_payout = 0;
    for (const auto& [miner_id, amount] : payouts) {
        total_payout += amount;
    }
    impl_->total_paid_ += total_payout;
    impl_->pool_revenue_ += (reward_per_share * impl_->recent_shares_.size()) - total_payout;

    return payouts;
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
        miner.paid_balance += miner.unpaid_balance;
        miner.unpaid_balance = 0;
        miner.last_payout = now;

        // TODO: Create actual blockchain transaction to send payment
        // This would involve:
        // 1. Creating a transaction with outputs to miner.payout_address
        // 2. Signing the transaction
        // 3. Broadcasting to the network
        // 4. Updating payment.tx_hash when confirmed
        // 5. Setting payment.is_confirmed = true when confirmed
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
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->miners_.find(miner_id);
    if (it == impl_->miners_.end()) {
        return 0;
    }

    return it->second.unpaid_balance;
}

uint64_t MiningPoolServer::GetMinerEstimatedEarnings(uint64_t miner_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->miners_.find(miner_id);
    if (it == impl_->miners_.end()) {
        return 0;
    }

    return it->second.estimated_earnings;
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
            // Expected params: ["user_agent/version", "session_id"] (optional)
            // For now, we'll use conn_id from request.id as the connection identifier
            uint64_t conn_id = request.id;

            auto subscribe_result = HandleSubscribe(conn_id);
            if (subscribe_result.IsError()) {
                response.error = subscribe_result.error;
            } else {
                // Format subscribe response as JSON
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
            // Expected params: ["username", "password"]
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
            // Expected params: ["worker_name", "job_id", "extranonce2", "ntime", "nonce"]
            if (request.params.size() < 5) {
                response.error = "mining.submit requires 5 parameters";
            } else {
                uint64_t conn_id = request.id;
                const std::string& job_id = request.params[1];
                const std::string& nonce = request.params[4];
                // Result is typically the hash calculated from all parameters
                // For now, use nonce as placeholder until full implementation
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
            // Unknown or unsupported method
            response.error = "Unknown or unsupported method: " + request.method;
            break;
        }
    }

    return Result<stratum::Message>::Ok(response);
}

Result<stratum::SubscribeResponse> MiningPoolServer::HandleSubscribe(uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Generate unique extranonce1 for this connection
    // extranonce1 should be unique per worker to prevent share collisions
    // Format: 8 hex characters (4 bytes) derived from conn_id
    std::ostringstream extranonce1_stream;
    extranonce1_stream << std::hex << std::setw(8) << std::setfill('0') << conn_id;
    std::string extranonce1 = extranonce1_stream.str();

    // extranonce2_size determines how many bytes the miner can modify
    // Standard is 4 bytes (allows 2^32 nonce space per worker)
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
    auto username_it = impl_->username_to_id_.find(wallet_address);

    if (username_it != impl_->username_to_id_.end()) {
        // Existing miner
        miner_id = username_it->second;
    } else {
        // New miner - create entry
        miner_id = impl_->GenerateMinerId();
        impl_->username_to_id_[wallet_address] = miner_id;

        // Note: Full miner object creation would happen here if needed
        // For now, we're using a simplified structure
    }

    // Create or update worker
    uint64_t worker_id = impl_->GenerateWorkerId();

    Worker worker;
    worker.worker_id = worker_id;
    worker.miner_id = miner_id;
    worker.worker_name = worker_name;
    worker.user_agent = "";  // Will be populated from mining.subscribe if provided
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
    impl_->miner_workers_[miner_id].push_back(worker_id);

    // Password is typically ignored in most Stratum implementations
    // Could be used for additional authentication if needed
    (void)password;
    (void)conn_id;  // Connection tracking would be done at network layer

    return Result<bool>::Ok(true);
}

Result<bool> MiningPoolServer::HandleSubmit(uint64_t conn_id,
                                             const std::string& job_id,
                                             const std::string& nonce,
                                             const std::string& result) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Find the worker by connection ID
    // In a real implementation, we'd maintain a conn_id -> worker_id mapping
    // For now, we'll use the first authorized worker (simplified)
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

    // Update last activity
    worker->last_activity = std::chrono::system_clock::now();

    // Validate we have current work
    if (!impl_->current_work_.has_value()) {
        return Result<bool>::Error("No active job");
    }

    const Work& work = impl_->current_work_.value();
    (void)work;  // Work validation would use this in full implementation

    // Parse nonce from hex string
    uint32_t nonce_value = 0;
    try {
        nonce_value = std::stoul(nonce, nullptr, 16);
    } catch (...) {
        worker->shares_rejected++;
        return Result<bool>::Error("Invalid nonce format");
    }
    (void)nonce_value;  // Nonce validation would use this in full implementation

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
    // Difficulty is inverse of hash value
    uint64_t share_difficulty = CalculateShareDifficulty(result_hash);

    // Check if share meets worker's difficulty target
    if (share_difficulty < worker->current_difficulty) {
        worker->shares_rejected++;
        return Result<bool>::Error("Share difficulty too low");
    }

    // Valid share! Create share record
    Share share;
    share.share_id = impl_->GenerateShareId();
    share.worker_id = worker_id;
    share.miner_id = worker->miner_id;
    share.worker_name = worker->worker_name;
    share.share_hash = result_hash;
    share.difficulty = share_difficulty;
    share.timestamp = std::chrono::system_clock::now();
    share.valid = true;
    share.is_block = false;  // Will check below

    // Check if this share is a valid block (meets network difficulty)
    if (impl_->blockchain_ && impl_->current_work_.has_value()) {
        // Get network target from current work's bits field
        const Work& current_work = *impl_->current_work_;
        uint256 network_target = DifficultyCalculator::CompactToTarget(current_work.header.bits);

        // Compare hash with network target
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
            impl_->total_blocks_found_++;
            worker->blocks_found++;

            // TODO: Submit block to blockchain
            // impl_->blockchain_->SubmitBlock(block);
        }
    }

    // Update worker statistics
    worker->shares_submitted++;
    worker->shares_accepted++;
    worker->last_share_time = std::chrono::system_clock::now();

    // Store share
    impl_->recent_shares_.push_back(share);
    impl_->miner_shares_[worker->miner_id].push_back(share);
    impl_->total_shares_submitted_++;

    // Update round statistics
    impl_->current_round_.shares_submitted++;
    if (share.is_block) {
        // Update round with block information
        impl_->current_round_.block_hash = share.share_hash;
        // Block height will be set when work is created
    }

    // Check if VarDiff adjustment is needed
    if (impl_->vardiff_manager_.ShouldAdjust(*worker)) {
        uint64_t new_diff = impl_->vardiff_manager_.CalculateDifficulty(*worker);
        worker->current_difficulty = new_diff;

        // Send new difficulty to worker
        SendSetDifficulty(conn_id, new_diff);
    }

    (void)job_id;  // Job ID validation would be done in full implementation

    return Result<bool>::Ok(true);
}

void MiningPoolServer::SendNotify(uint64_t conn_id, const Work& work) {
    // Build Stratum mining.notify message
    // Format: mining.notify(job_id, prevhash, coinbase1, coinbase2, merkle_branches, version, nbits, ntime, clean_jobs)

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
    // In full implementation, this would split the coinbase at the extranonce location
    notify.coinbase1 = "";  // First part of coinbase
    notify.coinbase2 = "";  // Second part of coinbase

    // Merkle branches (for merkle root calculation)
    notify.merkle_branches = {};  // Empty for simplified implementation

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

    // clean_jobs flag (true = discard old jobs)
    notify.clean_jobs = true;

    // TODO: Actually send the JSON-RPC message over the network connection
    // This would require network layer integration
    // Format: {"id": null, "method": "mining.notify", "params": [...]}

    (void)conn_id;  // Network sending would use conn_id
}

void MiningPoolServer::SendSetDifficulty(uint64_t conn_id, uint64_t difficulty) {
    // Build Stratum mining.set_difficulty message
    // Format: mining.set_difficulty(difficulty)

    // TODO: Actually send the JSON-RPC message over the network connection
    // Format: {"id": null, "method": "mining.set_difficulty", "params": [difficulty]}

    (void)conn_id;
    (void)difficulty;
}

// ============================================================================
// Security
// ============================================================================

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

} // namespace intcoin
