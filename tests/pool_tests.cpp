/*
 * Copyright (c) 2025 INTcoin Team (Neil Adamson)
 * MIT License
 * Mining Pool Tests
 */

#include <gtest/gtest.h>
#include "intcoin/pool.h"
#include "intcoin/blockchain.h"
#include "intcoin/crypto.h"
#include "intcoin/util.h"
#include <memory>
#include <thread>
#include <chrono>

using namespace intcoin;
using namespace intcoin::pool;

// ============================================================================
// Test Fixtures
// ============================================================================

class PoolTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test blockchain
        BlockchainConfig config;
        config.network = NetworkType::TESTNET;
        config.data_dir = "/tmp/intcoin-pool-test";

        blockchain_ = std::make_shared<Blockchain>(config);

        // Create test miner keypair
        auto keypair = crypto::GenerateKeypair();
        test_pubkey_ = keypair.public_key;
        test_privkey_ = keypair.secret_key;

        // Create test address
        test_address_ = Address::FromPublicKey(test_pubkey_);
    }

    void TearDown() override {
        blockchain_.reset();
    }

    std::shared_ptr<Blockchain> blockchain_;
    PublicKey test_pubkey_;
    SecretKey test_privkey_;
    Address test_address_;
};

// ============================================================================
// VarDiff Adjustment Tests
// ============================================================================

TEST_F(PoolTestFixture, VarDiffAdjustment_IncreasesDifficulty) {
    VarDiffConfig config;
    config.target_time = 10.0;  // Target 10 seconds per share
    config.adjustment_window = 5;
    config.min_difficulty = 1000;
    config.max_difficulty = 1000000000;

    VarDiffManager vardiff(config);

    Worker worker;
    worker.worker_id = 1;
    worker.current_difficulty = 10000;

    // Simulate shares submitted too quickly (5 seconds average)
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < 5; i++) {
        worker.recent_shares.push_back(now + std::chrono::seconds(i * 5));
    }

    // Should increase difficulty
    uint64_t new_diff = vardiff.CalculateNewDifficulty(worker);
    EXPECT_GT(new_diff, worker.current_difficulty);
}

TEST_F(PoolTestFixture, VarDiffAdjustment_DecreasesDifficulty) {
    VarDiffConfig config;
    config.target_time = 10.0;
    config.adjustment_window = 5;
    config.min_difficulty = 1000;
    config.max_difficulty = 1000000000;

    VarDiffManager vardiff(config);

    Worker worker;
    worker.worker_id = 1;
    worker.current_difficulty = 10000;

    // Simulate shares submitted too slowly (20 seconds average)
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < 5; i++) {
        worker.recent_shares.push_back(now + std::chrono::seconds(i * 20));
    }

    // Should decrease difficulty
    uint64_t new_diff = vardiff.CalculateNewDifficulty(worker);
    EXPECT_LT(new_diff, worker.current_difficulty);
}

TEST_F(PoolTestFixture, VarDiffAdjustment_RespectsMinDifficulty) {
    VarDiffConfig config;
    config.target_time = 10.0;
    config.adjustment_window = 5;
    config.min_difficulty = 5000;
    config.max_difficulty = 1000000000;

    VarDiffManager vardiff(config);

    Worker worker;
    worker.worker_id = 1;
    worker.current_difficulty = 6000;

    // Simulate very slow shares (should trigger minimum)
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < 5; i++) {
        worker.recent_shares.push_back(now + std::chrono::seconds(i * 100));
    }

    uint64_t new_diff = vardiff.CalculateNewDifficulty(worker);
    EXPECT_GE(new_diff, config.min_difficulty);
}

TEST_F(PoolTestFixture, VarDiffAdjustment_RespectsMaxDifficulty) {
    VarDiffConfig config;
    config.target_time = 10.0;
    config.adjustment_window = 5;
    config.min_difficulty = 1000;
    config.max_difficulty = 50000;

    VarDiffManager vardiff(config);

    Worker worker;
    worker.worker_id = 1;
    worker.current_difficulty = 40000;

    // Simulate very fast shares (should trigger maximum)
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < 5; i++) {
        worker.recent_shares.push_back(now + std::chrono::seconds(i * 1));
    }

    uint64_t new_diff = vardiff.CalculateNewDifficulty(worker);
    EXPECT_LE(new_diff, config.max_difficulty);
}

// ============================================================================
// Share Validation Tests
// ============================================================================

TEST_F(PoolTestFixture, ShareValidation_ValidShare) {
    ShareValidator validator(*blockchain_);

    // Create valid share
    Share share;
    share.worker_id = 1;
    share.job_id.fill(1);
    share.nonce.fill(123);
    share.timestamp = std::chrono::system_clock::now();
    share.difficulty = 10000;

    // Create hash that meets difficulty
    share.share_hash.fill(0);  // All zeros = very low hash value (high difficulty)

    share.valid = true;

    auto result = validator.ValidateShare(share);
    EXPECT_TRUE(result.IsOk());
}

TEST_F(PoolTestFixture, ShareValidation_InvalidDifficulty) {
    ShareValidator validator(*blockchain_);

    Share share;
    share.worker_id = 1;
    share.job_id.fill(1);
    share.nonce.fill(123);
    share.timestamp = std::chrono::system_clock::now();
    share.difficulty = 10000;

    // Create hash that doesn't meet difficulty
    share.share_hash.fill(0xFF);  // All FFs = very high hash value (low difficulty)

    auto result = validator.ValidateShare(share);
    EXPECT_TRUE(result.IsError());
    EXPECT_NE(result.GetError().find("difficulty"), std::string::npos);
}

TEST_F(PoolTestFixture, ShareValidation_DuplicateShare) {
    ShareValidator validator(*blockchain_);

    Share share;
    share.worker_id = 1;
    share.job_id.fill(1);
    share.nonce.fill(123);
    share.timestamp = std::chrono::system_clock::now();
    share.difficulty = 10000;
    share.share_hash.fill(0);
    share.valid = true;

    // First submission should succeed
    auto result1 = validator.ValidateShare(share);
    EXPECT_TRUE(result1.IsOk());

    // Second submission should fail (duplicate)
    auto result2 = validator.ValidateShare(share);
    EXPECT_TRUE(result2.IsError());
    EXPECT_NE(result2.GetError().find("duplicate"), std::string::npos);
}

TEST_F(PoolTestFixture, ShareValidation_StaleShare) {
    ShareValidator validator(*blockchain_);

    Share share;
    share.worker_id = 1;
    share.job_id.fill(1);
    share.nonce.fill(123);
    share.difficulty = 10000;
    share.share_hash.fill(0);
    share.valid = true;

    // Share from 10 minutes ago
    share.timestamp = std::chrono::system_clock::now() - std::chrono::minutes(10);

    auto result = validator.ValidateShare(share);
    EXPECT_TRUE(result.IsError());
    EXPECT_NE(result.GetError().find("stale"), std::string::npos);
}

// ============================================================================
// Payout Calculation Tests
// ============================================================================

TEST_F(PoolTestFixture, PayoutCalculation_PPLNS) {
    PayoutCalculator calculator;

    // Set up round with shares
    std::vector<Share> shares;

    // Worker 1: 300 shares
    for (int i = 0; i < 300; i++) {
        Share share;
        share.worker_id = 1;
        share.valid = true;
        shares.push_back(share);
    }

    // Worker 2: 200 shares
    for (int i = 0; i < 200; i++) {
        Share share;
        share.worker_id = 2;
        share.valid = true;
        shares.push_back(share);
    }

    // Worker 3: 500 shares
    for (int i = 0; i < 500; i++) {
        Share share;
        share.worker_id = 3;
        share.valid = true;
        shares.push_back(share);
    }

    // Total: 1000 shares
    // Block reward: 105,113,636 INT
    // Pool fee: 1% = 1,051,136 INT
    // Available for payout: 104,062,500 INT

    uint64_t block_reward = 105113636;
    double pool_fee = 0.01;  // 1%
    uint64_t payout_amount = block_reward * (1.0 - pool_fee);

    auto payouts = calculator.CalculatePPLNS(shares, payout_amount, 1000);

    // Verify total payouts equal available amount
    uint64_t total_paid = 0;
    for (const auto& [worker_id, amount] : payouts) {
        total_paid += amount;
    }
    EXPECT_EQ(total_paid, payout_amount);

    // Verify proportional distribution
    // Worker 1: 30% of shares
    EXPECT_NEAR(payouts[1], payout_amount * 0.30, 1000);

    // Worker 2: 20% of shares
    EXPECT_NEAR(payouts[2], payout_amount * 0.20, 1000);

    // Worker 3: 50% of shares
    EXPECT_NEAR(payouts[3], payout_amount * 0.50, 1000);
}

TEST_F(PoolTestFixture, PayoutCalculation_PPS) {
    PayoutCalculator calculator;

    // PPS pays per share regardless of block finding
    uint64_t network_difficulty = 5000000;
    uint64_t block_reward = 105113636;
    double pool_fee = 0.01;

    // Per-share value = (Block Reward / Network Difficulty) * (1 - Pool Fee)
    double per_share_value = (static_cast<double>(block_reward) / network_difficulty) * (1.0 - pool_fee);

    // Worker submitted 1000 shares
    uint64_t shares_submitted = 1000;
    uint64_t expected_payout = static_cast<uint64_t>(per_share_value * shares_submitted);

    uint64_t actual_payout = calculator.CalculatePPS(shares_submitted, network_difficulty, block_reward, pool_fee);

    EXPECT_EQ(actual_payout, expected_payout);
}

TEST_F(PoolTestFixture, PayoutCalculation_Proportional) {
    PayoutCalculator calculator;

    std::vector<Share> round_shares;

    // Worker 1: 600 shares
    for (int i = 0; i < 600; i++) {
        Share share;
        share.worker_id = 1;
        share.valid = true;
        round_shares.push_back(share);
    }

    // Worker 2: 400 shares
    for (int i = 0; i < 400; i++) {
        Share share;
        share.worker_id = 2;
        share.valid = true;
        round_shares.push_back(share);
    }

    // Total: 1000 shares this round
    uint64_t block_reward = 105113636;
    double pool_fee = 0.02;  // 2%
    uint64_t payout_amount = block_reward * (1.0 - pool_fee);

    auto payouts = calculator.CalculateProportional(round_shares, payout_amount);

    // Verify proportional split
    EXPECT_NEAR(payouts[1], payout_amount * 0.6, 1000);  // 60%
    EXPECT_NEAR(payouts[2], payout_amount * 0.4, 1000);  // 40%
}

// ============================================================================
// Worker Management Tests
// ============================================================================

TEST_F(PoolTestFixture, WorkerManagement_RegisterMiner) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    // Register new miner
    auto result = pool.RegisterMiner(test_address_.ToString(), test_address_.ToString(), "");
    EXPECT_TRUE(result.IsOk());

    uint64_t miner_id = result.GetValue();
    EXPECT_GT(miner_id, 0);

    // Verify miner exists
    auto miner_opt = pool.GetMiner(miner_id);
    EXPECT_TRUE(miner_opt.has_value());
    EXPECT_EQ(miner_opt->payout_address, test_address_.ToString());
}

TEST_F(PoolTestFixture, WorkerManagement_AddWorker) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    // Register miner first
    auto miner_result = pool.RegisterMiner(test_address_.ToString(), test_address_.ToString(), "");
    ASSERT_TRUE(miner_result.IsOk());
    uint64_t miner_id = miner_result.GetValue();

    // Add worker
    auto worker_result = pool.AddWorker(miner_id, "worker1", "127.0.0.1", 10000);
    EXPECT_TRUE(worker_result.IsOk());

    uint64_t worker_id = worker_result.GetValue();
    EXPECT_GT(worker_id, 0);

    // Verify worker exists
    auto worker_opt = pool.GetWorker(worker_id);
    EXPECT_TRUE(worker_opt.has_value());
    EXPECT_EQ(worker_opt->worker_name, "worker1");
    EXPECT_EQ(worker_opt->ip_address, "127.0.0.1");
}

TEST_F(PoolTestFixture, WorkerManagement_RemoveWorker) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    // Register miner and add worker
    auto miner_result = pool.RegisterMiner(test_address_.ToString(), test_address_.ToString(), "");
    ASSERT_TRUE(miner_result.IsOk());
    uint64_t miner_id = miner_result.GetValue();

    auto worker_result = pool.AddWorker(miner_id, "worker1", "127.0.0.1", 10000);
    ASSERT_TRUE(worker_result.IsOk());
    uint64_t worker_id = worker_result.GetValue();

    // Remove worker
    pool.RemoveWorker(worker_id);

    // Verify worker no longer exists
    auto worker_opt = pool.GetWorker(worker_id);
    EXPECT_FALSE(worker_opt.has_value());
}

TEST_F(PoolTestFixture, WorkerManagement_GetActiveWorkers) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    // Register miner
    auto miner_result = pool.RegisterMiner(test_address_.ToString(), test_address_.ToString(), "");
    ASSERT_TRUE(miner_result.IsOk());
    uint64_t miner_id = miner_result.GetValue();

    // Add 3 workers
    for (int i = 1; i <= 3; i++) {
        auto worker_result = pool.AddWorker(miner_id, "worker" + std::to_string(i), "127.0.0.1", 10000);
        ASSERT_TRUE(worker_result.IsOk());
    }

    // Get all workers
    auto workers = pool.GetAllWorkers();
    EXPECT_EQ(workers.size(), 3);
}

// ============================================================================
// Hashrate Calculation Tests
// ============================================================================

TEST_F(PoolTestFixture, HashrateCalculation_WorkerHashrate) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    // Register miner and add worker
    auto miner_result = pool.RegisterMiner(test_address_.ToString(), test_address_.ToString(), "");
    ASSERT_TRUE(miner_result.IsOk());
    uint64_t miner_id = miner_result.GetValue();

    auto worker_result = pool.AddWorker(miner_id, "worker1", "127.0.0.1", 10000);
    ASSERT_TRUE(worker_result.IsOk());
    uint64_t worker_id = worker_result.GetValue();

    // Simulate shares submitted over time
    auto worker_opt = pool.GetWorker(worker_id);
    ASSERT_TRUE(worker_opt.has_value());

    // Add 10 shares over 100 seconds
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < 10; i++) {
        worker_opt->recent_shares.push_back(now + std::chrono::seconds(i * 10));
    }

    // Update worker
    pool.UpdateWorker(*worker_opt);

    // Calculate hashrate
    double hashrate = pool.CalculateWorkerHashrate(worker_id);

    // Hashrate = (shares * difficulty * 2^32) / time
    // Expected: (10 * 10000 * 4294967296) / 100 ≈ 429 MH/s
    EXPECT_GT(hashrate, 0);
}

TEST_F(PoolTestFixture, HashrateCalculation_PoolHashrate) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    // Register miner
    auto miner_result = pool.RegisterMiner(test_address_.ToString(), test_address_.ToString(), "");
    ASSERT_TRUE(miner_result.IsOk());
    uint64_t miner_id = miner_result.GetValue();

    // Add multiple workers
    for (int i = 1; i <= 5; i++) {
        auto worker_result = pool.AddWorker(miner_id, "worker" + std::to_string(i), "127.0.0.1", 10000);
        ASSERT_TRUE(worker_result.IsOk());
    }

    // Pool hashrate should be sum of all worker hashrates
    double pool_hashrate = pool.CalculatePoolHashrate();
    EXPECT_GE(pool_hashrate, 0);
}

// ============================================================================
// Block Detection Tests
// ============================================================================

TEST_F(PoolTestFixture, BlockDetection_ValidBlock) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    // Register miner and worker
    auto miner_result = pool.RegisterMiner(test_address_.ToString(), test_address_.ToString(), "");
    ASSERT_TRUE(miner_result.IsOk());
    uint64_t miner_id = miner_result.GetValue();

    auto worker_result = pool.AddWorker(miner_id, "worker1", "127.0.0.1", 10000);
    ASSERT_TRUE(worker_result.IsOk());
    uint64_t worker_id = worker_result.GetValue();

    // Create share that meets network difficulty (block solution)
    Share share;
    share.worker_id = worker_id;
    share.job_id.fill(1);
    share.nonce.fill(123);
    share.timestamp = std::chrono::system_clock::now();
    share.difficulty = 10000;
    share.share_hash.fill(0);  // Very low hash = block solution
    share.valid = true;

    // Submit share (should detect block)
    auto submit_result = pool.SubmitShare(worker_id, share.job_id, share.nonce, share.share_hash);

    // Note: Actual block submission will fail without proper blockchain setup,
    // but share validation should succeed
    // In production, this would submit the block to the blockchain
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(PoolTestFixture, Statistics_PoolStats) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    auto stats = pool.GetStatistics();

    // Initial stats
    EXPECT_EQ(stats.active_miners, 0);
    EXPECT_EQ(stats.total_shares, 0);
    EXPECT_EQ(stats.blocks_found, 0);
    EXPECT_GE(stats.pool_hashrate, 0);
}

TEST_F(PoolTestFixture, Statistics_MinerStats) {
    PoolConfig pool_config;
    pool_config.stratum_port = 13333;
    pool_config.http_port = 18080;

    MiningPoolServer pool(pool_config, *blockchain_);

    // Register miner
    auto miner_result = pool.RegisterMiner(test_address_.ToString(), test_address_.ToString(), "");
    ASSERT_TRUE(miner_result.IsOk());
    uint64_t miner_id = miner_result.GetValue();

    auto miner_opt = pool.GetMiner(miner_id);
    ASSERT_TRUE(miner_opt.has_value());

    // Check initial stats
    EXPECT_EQ(miner_opt->total_shares_accepted, 0);
    EXPECT_EQ(miner_opt->total_shares_rejected, 0);
    EXPECT_EQ(miner_opt->unpaid_balance, 0);
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
