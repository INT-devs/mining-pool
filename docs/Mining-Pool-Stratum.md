# INTcoin Mining Pool - Full Stratum Protocol Implementation

**Version**: 1.0
**Last Updated**: December 17, 2025
**Status**: Production Ready

---

## Table of Contents

- [Overview](#overview)
- [Stratum Protocol](#stratum-protocol)
- [Server Architecture](#server-architecture)
- [Connection Management](#connection-management)
- [Share Processing](#share-processing)
- [Difficulty Adjustment](#difficulty-adjustment)
- [Payout System](#payout-system)
- [Security](#security)
- [API Reference](#api-reference)
- [Configuration](#configuration)

---

## Overview

INTcoin's mining pool implements the complete Stratum mining protocol (v1), providing enterprise-grade pool mining capabilities with:

- **Full Stratum v1 Protocol**: Compatible with all Stratum miners
- **Variable Difficulty (VarDiff)**: Automatic difficulty adjustment per worker
- **Multiple Payout Methods**: PPLNS, PPS, Proportional, Solo
- **High Performance**: Multi-threaded TCP server with connection pooling
- **Real-time Statistics**: Comprehensive pool and miner statistics
- **Post-Quantum Security**: Dilithium3 signatures for payouts
- **RandomX Mining**: Full support for CPU-optimized mining

---

## Stratum Protocol

### Supported Methods

#### Client → Server

1. **mining.subscribe**
   ```json
   {"id": 1, "method": "mining.subscribe", "params": ["intcoin-miner/1.0", null]}
   ```
   Response:
   ```json
   {
     "id": 1,
     "result": [
       [["mining.set_difficulty", "subscription_id"], ["mining.notify", "subscription_id"]],
       "extranonce1_hex",
       4
     ],
     "error": null
   }
   ```

2. **mining.authorize**
   ```json
   {"id": 2, "method": "mining.authorize", "params": ["username.worker", "password"]}
   ```
   Response:
   ```json
   {"id": 2, "result": true, "error": null}
   ```

3. **mining.submit**
   ```json
   {
     "id": 4,
     "method": "mining.submit",
     "params": ["username.worker", "job_id", "extranonce2", "ntime", "nonce"]
   }
   ```
   Response:
   ```json
   {"id": 4, "result": true, "error": null}
   ```

#### Server → Client

1. **mining.set_difficulty**
   ```json
   {"id": null, "method": "mining.set_difficulty", "params": [16384]}
   ```

2. **mining.notify**
   ```json
   {
     "id": null,
     "method": "mining.notify",
     "params": [
       "job_id",
       "prev_hash",
       "coinbase1",
       "coinbase2",
       ["merkle_branch1", "merkle_branch2"],
       "version",
       "nbits",
       "ntime",
       true
     ]
   }
   ```

3. **client.reconnect**
   ```json
   {"id": null, "method": "client.reconnect", "params": ["pool.example.com", 3333, 30]}
   ```

4. **client.show_message**
   ```json
   {"id": null, "method": "client.show_message", "params": ["Pool will restart in 5 minutes"]}
   ```

### Protocol Flow

```
Client                                  Server
  |                                       |
  |-------- mining.subscribe ----------->|
  |<------- subscription response -------|
  |                                       |
  |-------- mining.authorize ----------->|
  |<------- authorize response ----------|
  |                                       |
  |<------- mining.set_difficulty -------|
  |<------- mining.notify ---------------|
  |                                       |
  |-------- mining.submit -------------->|
  |<------- submit response -------------|
  |                                       |
  |<------- mining.set_difficulty -------| (vardiff)
  |<------- mining.notify ---------------| (new block)
  |                                       |
```

---

## Server Architecture

### Components

```
┌─────────────────────────────────────────────────────────┐
│                 MiningPoolServer                        │
├─────────────────────────────────────────────────────────┤
│  TCP Server (Port 2215)                                 │
│  ├── Connection Manager                                 │
│  ├── Message Parser/Formatter                          │
│  └── Protocol Handler                                   │
├─────────────────────────────────────────────────────────┤
│  Worker Management                                       │
│  ├── Miner Registry                                     │
│  ├── Worker Tracker                                     │
│  ├── Session Manager                                    │
│  └── Statistics Collector                               │
├─────────────────────────────────────────────────────────┤
│  Mining Engine                                           │
│  ├── Work Generator                                      │
│  ├── Share Validator                                     │
│  ├── Block Finder                                        │
│  └── Blockchain Integration                              │
├─────────────────────────────────────────────────────────┤
│  Difficulty Manager                                      │
│  ├── VarDiff Calculator                                  │
│  ├── Per-Worker Adjustment                              │
│  └── Network Difficulty Tracker                         │
├─────────────────────────────────────────────────────────┤
│  Payout System                                           │
│  ├── PPLNS Calculator                                    │
│  ├── PPS Calculator                                      │
│  ├── Payment Processor                                   │
│  └── Balance Tracker                                     │
├─────────────────────────────────────────────────────────┤
│  Security                                                │
│  ├── IP Blocker                                          │
│  ├── Rate Limiter                                        │
│  ├── Spam Filter                                         │
│  └── Ban Manager                                         │
└─────────────────────────────────────────────────────────┘
```

### Thread Model

- **Main Thread**: Accept connections, configuration
- **Network Thread**: Poll connections, read/write data
- **Worker Threads**: Process shares, validate, update stats (thread pool)
- **Difficulty Thread**: Adjust worker difficulties periodically
- **Payout Thread**: Process payouts on schedule
- **Cleanup Thread**: Disconnect inactive workers, garbage collection

---

## Connection Management

### Connection States

```cpp
enum class ConnectionState {
    CONNECTED,      // TCP connected, waiting for subscribe
    SUBSCRIBED,     // Subscribed, waiting for authorize
    AUTHORIZED,     // Authorized, ready to mine
    ACTIVE,         // Actively submitting shares
    DISCONNECTING,  // Graceful disconnect in progress
    BANNED          // Banned for violations
};
```

### Connection Lifecycle

1. **Accept**: TCP connection accepted
2. **Subscribe**: Client sends `mining.subscribe`
3. **Authorize**: Client sends `mining.authorize`
4. **Mining**: Client receives work and submits shares
5. **Disconnect**: Clean shutdown or timeout

### Connection Limits

- **Max connections per IP**: 10 (configurable)
- **Max workers per miner**: 100 (configurable)
- **Total pool connections**: 10,000 (configurable)
- **Inactive timeout**: 300 seconds (5 minutes)

---

## Share Processing

### Share Validation Pipeline

```
Share Received
     │
     ├──> Validate job exists
     │
     ├──> Check duplicate nonce
     │
     ├──> Validate timestamp
     │
     ├──> Calculate hash
     │
     ├──> Verify difficulty
     │         │
     │         ├──> Meets share difficulty? → Accept share
     │         └──> Below share difficulty? → Reject share
     │
     └──> Meets network difficulty? → Submit block!
```

### Share Accounting

```cpp
struct ShareAccounting {
    uint64_t total_submitted;
    uint64_t total_accepted;
    uint64_t total_rejected;
    uint64_t total_stale;
    uint64_t total_duplicate;
    uint64_t total_low_difficulty;

    double acceptance_rate;  // accepted / submitted
    double efficiency;       // accepted / (accepted + rejected)
};
```

### Block Finding

When a share meets network difficulty:

1. Construct full block from template
2. Submit to blockchain
3. Broadcast to network
4. Update pool statistics
5. Trigger payout calculation
6. Notify all miners

---

## Difficulty Adjustment

### VarDiff Algorithm

```cpp
// Target: 1 share every 10 seconds per worker
double target_share_time = 10.0;   // seconds
double retarget_time = 60.0;        // adjust every 60s
double variance = 0.3;              // ±30% variance allowed

// Calculate average time between shares
double avg_time = total_time / share_count;
double ratio = avg_time / target_share_time;

if (ratio < 0.7) {
    // Shares too frequent, increase difficulty
    new_difficulty = current_difficulty * 1.5;
} else if (ratio > 1.3) {
    // Shares too slow, decrease difficulty
    new_difficulty = current_difficulty * 0.75;
}

// Clamp to pool limits
new_difficulty = clamp(new_difficulty, min_difficulty, max_difficulty);
```

### Difficulty Bounds

- **Minimum**: 1,000 (pool minimum)
- **Maximum**: Network difficulty (no higher than network)
- **Initial**: 16,384 (starts at diff 16K)

### Adjustment Frequency

- **Check interval**: Every 60 seconds
- **Minimum shares**: 3 shares required before adjustment
- **Notification**: `mining.set_difficulty` sent before next job

---

## Payout System

### PPLNS (Pay Per Last N Shares)

```cpp
// Pay based on last N shares when block found
uint64_t N = config.pplns_window;  // e.g., 1,000,000 shares

// Count miner shares in last N
std::map<uint64_t, uint64_t> miner_shares;
for (size_t i = total_shares - N; i < total_shares; ++i) {
    miner_shares[shares[i].miner_id]++;
}

// Calculate payout
uint64_t total_payout = block_reward * (1.0 - pool_fee);
for (const auto& [miner_id, count] : miner_shares) {
    uint64_t payout = (total_payout * count) / N;
    AddToBalance(miner_id, payout);
}
```

### PPS (Pay Per Share)

```cpp
// Pay for each valid share immediately
uint64_t expected_shares = network_difficulty / share_difficulty;
uint64_t reward_per_share = (block_reward * (1.0 - pool_fee)) / expected_shares;

// Add to balance for each share
void ProcessShare(const Share& share) {
    if (share.valid) {
        AddToBalance(share.miner_id, reward_per_share);
    }
}
```

### Proportional

```cpp
// Pay based on shares in current round
std::map<uint64_t, uint64_t> round_shares;
for (const auto& share : current_round) {
    round_shares[share.miner_id]++;
}

uint64_t total_payout = block_reward * (1.0 - pool_fee);
uint64_t total_shares = current_round.size();

for (const auto& [miner_id, count] : round_shares) {
    uint64_t payout = (total_payout * count) / total_shares;
    AddToBalance(miner_id, payout);
}
```

### Payout Processing

```cpp
// Process payouts every 6 hours
void ProcessPayouts() {
    for (const auto& [miner_id, balance] : miner_balances) {
        if (balance >= min_payout) {
            CreatePayoutTransaction(miner_id, balance);
            miner_balances[miner_id] = 0;
        }
    }
}
```

---

## Security

### Rate Limiting

```cpp
struct RateLimiter {
    size_t max_shares_per_minute = 120;
    size_t max_connections_per_ip = 10;
    size_t max_invalid_shares = 50;

    std::chrono::seconds ban_duration = std::chrono::hours(24);
};
```

### Share Validation

- **Duplicate detection**: Track last 1000 nonces per worker
- **Timestamp validation**: Must be within 5 minutes of job creation
- **Difficulty validation**: Hash must meet worker's current difficulty
- **Job validation**: Job ID must be current or recent (last 10 jobs)

### Auto-Ban Triggers

1. **Too many invalid shares**: >50 in 1 hour
2. **Spam detection**: >120 shares/minute
3. **Duplicate shares**: >10 duplicates in 5 minutes
4. **Stale shares**: >90% stale rate
5. **Low difficulty**: Repeatedly submitting below required difficulty

### IP Blocking

```cpp
// Block abusive IPs
std::unordered_map<std::string, BanInfo> ip_bans;

struct BanInfo {
    std::chrono::system_clock::time_point banned_at;
    std::chrono::system_clock::time_point expires_at;
    std::string reason;
    size_t offense_count;
};
```

---

## API Reference

### Pool Configuration

```cpp
PoolConfig config = {
    .pool_name = "INTcoin Pool",
    .pool_address = "int1qpooladdress...",
    .stratum_port = 2215,
    .http_port = 2216,

    .min_difficulty = 1000,
    .initial_difficulty = 16384,
    .target_share_time = 10.0,
    .vardiff_retarget_time = 60.0,
    .vardiff_variance = 0.3,

    .payout_method = PoolConfig::PayoutMethod::PPLNS,
    .pplns_window = 1000000,
    .pool_fee_percent = 1.0,
    .min_payout = 100000000,  // 1 INT
    .payout_interval = 21600,  // 6 hours

    .max_workers_per_miner = 100,
    .max_miners = 10000,
    .max_connections_per_ip = 10,

    .require_password = false,
    .ban_on_invalid_share = true,
    .max_invalid_shares = 50,
    .ban_duration = std::chrono::hours(24)
};
```

### Starting the Pool

```cpp
#include <intcoin/pool.h>

int main() {
    // Create blockchain
    auto blockchain = std::make_shared<Blockchain>();

    // Create miner (for block submission)
    auto miner = std::make_shared<Miner>();

    // Configure pool
    PoolConfig config = /* ... */;

    // Create and start pool
    MiningPoolServer pool(config, blockchain, miner);

    auto result = pool.Start();
    if (result.IsError()) {
        std::cerr << "Failed to start pool: " << result.error << std::endl;
        return 1;
    }

    std::cout << "Pool started on port " << config.stratum_port << std::endl;

    // Run until shutdown signal
    while (pool.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Print stats
        auto stats = pool.GetStatistics();
        std::cout << "Hashrate: " << (stats.pool_hashrate / 1e6) << " MH/s" << std::endl;
        std::cout << "Miners: " << stats.active_miners << std::endl;
        std::cout << "Workers: " << stats.active_workers << std::endl;
    }

    pool.Stop();
    return 0;
}
```

---

## Configuration

### Pool Configuration File

```ini
# INTcoin Pool Configuration
[pool]
name = INTcoin Pool
address = int1qpooladdresshere...
stratum_port = 2215
http_port = 2216

[mining]
min_difficulty = 1000
initial_difficulty = 16384
target_share_time = 10.0
vardiff_retarget = 60.0
vardiff_variance = 0.3

[payout]
method = PPLNS
pplns_window = 1000000
pool_fee = 1.0
min_payout = 100000000
payout_interval = 21600

[limits]
max_workers_per_miner = 100
max_miners = 10000
max_connections_per_ip = 10

[security]
require_password = false
ban_on_invalid_share = true
max_invalid_shares = 50
ban_duration = 86400
```

---

## Next Steps

- [Mining Guide](Mining.md) - How to connect miners to pool
- [API Documentation](API-Reference.md) - HTTP API reference
- [RPC Commands](RPC-Commands.md) - Pool RPC methods

---

*Last Updated: December 17, 2025*
