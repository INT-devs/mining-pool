# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Repository:** `git@github.com:INT-devs/mining-pool.git`

INTcoin Mining Pool - A standalone mining pool server for the INTcoin cryptocurrency. This repository contains all the code for setting up and running a mining pool. Based on the pool components from the INTcoin core project at `/Users/neiladamson/Desktop/intcoin`.

### Key Features
- Stratum Protocol v1 with VarDiff (Variable Difficulty)
- Multiple payout methods: PPLNS, PPS, Proportional
- HTTP REST API for statistics and dashboard
- RocksDB persistent storage for shares, blocks, payments

## Build Commands

### macOS
```bash
mkdir build && cd build
cmake -DBUILD_POOL_SERVER=ON -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
```

### Linux (Ubuntu/Debian)
```bash
sudo apt install build-essential cmake libboost-all-dev libssl-dev librocksdb-dev libzmq3-dev libevent-dev
mkdir build && cd build
cmake -DBUILD_POOL_SERVER=ON -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### FreeBSD
```bash
pkg install cmake boost-all openssl rocksdb zeromq libevent
mkdir build && cd build
cmake -DBUILD_POOL_SERVER=ON -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
```

### Windows (.exe)
```powershell
# Using Visual Studio 2022 Developer Command Prompt
# Dependencies via vcpkg: boost, openssl, rocksdb, zeromq, libevent
vcpkg install boost:x64-windows openssl:x64-windows rocksdb:x64-windows zeromq:x64-windows libevent:x64-windows

cmake -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_POOL_SERVER=ON -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Output: build/Release/intcoin-pool-server.exe
```

### Run Tests
```bash
cd build
ctest --output-on-failure
./tests/pool_tests
```

## Architecture

```
src/pool/
├── intcoin-pool-server.cpp  # Main entry point
├── mining_pool_server.cpp   # Pool manager (workers, shares, blocks)
├── stratum_server.cpp       # Stratum protocol implementation
├── http_api.cpp             # REST API endpoints
├── pool_database.cpp        # RocksDB share/payment storage
└── pool.cpp                 # Core pool logic
```

### Network Ports
- **3333**: Stratum (miner connections)
- **8080**: HTTP API (statistics, dashboard)
- **2211**: RPC connection to intcoind

### API Endpoints
- `GET /api/pool/stats` - Pool hashrate, miners, blocks
- `GET /api/pool/blocks?limit=N` - Recent blocks found
- `GET /api/pool/payments?limit=N` - Recent payouts
- `GET /api/pool/topminers?limit=N` - Leaderboard
- `GET /api/pool/worker?address=int1...` - Worker stats

## Configuration

Pool configuration file (`pool.conf`):
```conf
# Network
stratum-port=3333
http-port=8080

# intcoind RPC
rpc-host=127.0.0.1
rpc-port=2211
rpc-user=pooloperator
rpc-password=password

# Pool settings
pool-name=MyINTPool
pool-fee=1.5
payout-method=pplns
pplns-window=1000000
payout-threshold=1000000000

# VarDiff
vardiff-enabled=true
vardiff-target-time=10
vardiff-min-difficulty=1000
vardiff-max-difficulty=1000000000

# Database
db-path=/var/lib/intcoin-pool/pooldb
```

## Key Technical Details

### Dependencies
- **C++23** compiler (GCC 13+, Clang 16+, MSVC 2022+)
- **CMake** 3.28+
- **Boost** 1.87+
- **RocksDB** 10.7+
- **OpenSSL** 3.5+
- **ZeroMQ** 4.3+
- **libevent** 2.1+

### Stratum Protocol Messages
- `mining.subscribe` - Worker subscription
- `mining.authorize` - Worker authentication
- `mining.submit` - Share submission
- `mining.notify` - Job distribution
- `mining.set_difficulty` - VarDiff adjustment

### Payout Calculation (PPLNS)
```
Miner Reward = (Miner Shares in Window / Total Shares) × Block Reward × (1 - Pool Fee)
```

## Development Notes

- Do NOT add Co-Authored-By footer to git commits
- Base source from `/Users/neiladamson/Desktop/intcoin/src/pool/`
- Follow C++23 conventions and use RAII patterns
- All cryptographic signatures use Dilithium3 (post-quantum)
- Block template fetched via `getblocktemplate` RPC from intcoind
