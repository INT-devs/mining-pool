/*
 * Copyright (c) 2025 INTcoin Team (Neil Adamson)
 * MIT License
 * Mining Pool Database (Stub Implementation)
 */

#include "intcoin/pool.h"
#include "intcoin/storage.h"
#include <map>
#include <vector>
#include <algorithm>

namespace intcoin {
namespace pool {

/**
 * Pool database for persistent storage of pool statistics
 *
 * This is a minimal in-memory implementation.
 * Full implementation would use RocksDB or SQLite for persistence.
 *
 * Database Schema:
 * - workers: worker_id -> Worker (serialized)
 * - shares: share_id -> Share (serialized)
 * - blocks: block_hash -> BlockRecord (height, finder, reward, status)
 * - payments: payment_id -> Payment (address, amount, txid, timestamp)
 */
class PoolDatabase {
public:
    explicit PoolDatabase(const std::string& db_path)
        : db_path_(db_path), next_share_id_(1), next_payment_id_(1) {}

    ~PoolDatabase() {}

    // ========================================================================
    // Worker Management
    // ========================================================================

    Result<void> SaveWorker(const Worker& worker) {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_[worker.worker_id] = worker;
        return Result<void>::Ok();
    }

    Result<Worker> LoadWorker(uint64_t worker_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = workers_.find(worker_id);
        if (it == workers_.end()) {
            return Result<Worker>::Error("Worker not found");
        }

        return Result<Worker>::Ok(it->second);
    }

    // ========================================================================
    // Share Tracking
    // ========================================================================

    Result<void> RecordShare(const Share& share) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Store share with auto-generated ID if needed
        Share stored_share = share;
        if (stored_share.share_id == 0) {
            stored_share.share_id = next_share_id_++;
        }

        shares_.push_back(stored_share);

        // Keep only recent shares (last 10,000)
        if (shares_.size() > 10000) {
            shares_.erase(shares_.begin());
        }

        return Result<void>::Ok();
    }

    std::vector<Share> GetRecentShares(int limit) {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t start = shares_.size() > static_cast<size_t>(limit) ?
                       shares_.size() - limit : 0;

        return std::vector<Share>(shares_.begin() + start, shares_.end());
    }

    uint64_t GetTotalShares24h() {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::system_clock::now();
        auto cutoff = now - std::chrono::hours(24);

        uint64_t count = 0;
        for (const auto& share : shares_) {
            if (share.timestamp >= cutoff && share.valid) {
                count++;
            }
        }

        return count;
    }

    // ========================================================================
    // Block Tracking
    // ========================================================================

    struct BlockRecord {
        uint64_t height;
        uint256 hash;
        std::string finder_address;
        uint64_t reward;
        std::string status;  // "pending", "confirmed", "orphaned"
        std::chrono::system_clock::time_point timestamp;
    };

    Result<void> RecordBlock(uint64_t height, const uint256& hash,
                            const std::string& finder, uint64_t reward) {
        std::lock_guard<std::mutex> lock(mutex_);

        BlockRecord record;
        record.height = height;
        record.hash = hash;
        record.finder_address = finder;
        record.reward = reward;
        record.status = "pending";
        record.timestamp = std::chrono::system_clock::now();

        blocks_.push_back(record);

        return Result<void>::Ok();
    }

    std::vector<BlockRecord> GetRecentBlocks(int limit) {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t start = blocks_.size() > static_cast<size_t>(limit) ?
                       blocks_.size() - limit : 0;

        return std::vector<BlockRecord>(blocks_.begin() + start, blocks_.end());
    }

    // ========================================================================
    // Payment Tracking
    // ========================================================================

    struct Payment {
        uint64_t payment_id;
        std::string address;
        uint64_t amount;
        std::string txid;
        std::chrono::system_clock::time_point timestamp;
    };

    Result<void> RecordPayment(const std::string& address, uint64_t amount,
                               const std::string& txid) {
        std::lock_guard<std::mutex> lock(mutex_);

        Payment payment;
        payment.payment_id = next_payment_id_++;
        payment.address = address;
        payment.amount = amount;
        payment.txid = txid;
        payment.timestamp = std::chrono::system_clock::now();

        payments_.push_back(payment);

        return Result<void>::Ok();
    }

    std::vector<Payment> GetRecentPayments(int limit) {
        std::lock_guard<std::mutex> lock(mutex_);

        size_t start = payments_.size() > static_cast<size_t>(limit) ?
                       payments_.size() - limit : 0;

        return std::vector<Payment>(payments_.begin() + start, payments_.end());
    }

    // ========================================================================
    // Statistics
    // ========================================================================

    struct WorkerStats {
        std::string address;
        uint64_t hashrate;
        uint64_t shares_24h;
        uint64_t balance;
        uint64_t total_paid;
    };

    std::vector<WorkerStats> GetTopMiners(int limit) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Aggregate statistics for each unique payout address
        std::map<std::string, WorkerStats> stats_map;

        auto now = std::chrono::system_clock::now();
        auto cutoff_24h = now - std::chrono::hours(24);

        // Aggregate shares from last 24h by worker
        std::map<uint64_t, uint64_t> worker_shares_24h;
        std::map<uint64_t, std::vector<std::chrono::system_clock::time_point>> worker_share_times;

        for (const auto& share : shares_) {
            if (share.timestamp >= cutoff_24h && share.valid) {
                worker_shares_24h[share.worker_id]++;
                worker_share_times[share.worker_id].push_back(share.timestamp);
            }
        }

        // Process each worker
        for (const auto& [worker_id, worker] : workers_) {
            // Find corresponding miner to get payout address
            // For now, use worker_name as identifier (in real implementation,
            // would look up miner_id and use actual payout address)
            std::string address = worker.worker_name; // Simplified for stub

            if (stats_map.find(address) == stats_map.end()) {
                stats_map[address] = WorkerStats{};
                stats_map[address].address = address;
                stats_map[address].hashrate = 0;
                stats_map[address].shares_24h = 0;
                stats_map[address].balance = 0;
                stats_map[address].total_paid = 0;
            }

            WorkerStats& stats = stats_map[address];

            // Add shares from this worker
            stats.shares_24h += worker_shares_24h[worker_id];

            // Calculate hashrate from recent shares
            // Hashrate = (shares * difficulty * 2^32) / time_period
            const auto& share_times = worker_share_times[worker_id];
            if (share_times.size() >= 2) {
                auto time_span = std::chrono::duration_cast<std::chrono::seconds>(
                    share_times.back() - share_times.front()).count();

                if (time_span > 0) {
                    // Estimate: each share at difficulty 1 = ~4.3 billion hashes
                    // For now, use simplified calculation
                    double estimated_hashrate = (double)share_times.size() *
                                               worker.current_difficulty *
                                               4294967296.0 / time_span;
                    stats.hashrate += static_cast<uint64_t>(estimated_hashrate);
                }
            }
        }

        // Calculate balances and payments by address
        for (const auto& payment : payments_) {
            if (stats_map.find(payment.address) != stats_map.end()) {
                stats_map[payment.address].total_paid += payment.amount;
            }
        }

        // Convert map to vector
        std::vector<WorkerStats> result;
        for (const auto& [addr, stats] : stats_map) {
            result.push_back(stats);
        }

        // Sort by hashrate (descending)
        std::sort(result.begin(), result.end(),
                  [](const WorkerStats& a, const WorkerStats& b) {
                      return a.hashrate > b.hashrate;
                  });

        // Limit results
        if (result.size() > static_cast<size_t>(limit)) {
            result.resize(limit);
        }

        return result;
    }

private:
    std::string db_path_;
    std::mutex mutex_;

    // In-memory storage (would be replaced with RocksDB/SQLite)
    std::map<uint64_t, Worker> workers_;
    std::vector<Share> shares_;
    std::vector<BlockRecord> blocks_;
    std::vector<Payment> payments_;

    std::atomic<uint64_t> next_share_id_;
    std::atomic<uint64_t> next_payment_id_;
};

} // namespace pool
} // namespace intcoin
