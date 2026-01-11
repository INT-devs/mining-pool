# INTcoin Mining Pool Setup Guide

**Version**: 1.0.0-beta
**Last Updated**: December 26, 2025
**Status**: Complete - Production Ready

This guide covers setting up and operating an INTcoin mining pool using the built-in pool server.

---

## Table of Contents

1. [Overview](#overview)
2. [System Requirements](#system-requirements)
3. [Pool Architecture](#pool-architecture)
4. [Installation](#installation)
5. [Configuration](#configuration)
6. [Running the Pool](#running-the-pool)
7. [Pool Dashboard](#pool-dashboard)
8. [Payout System](#payout-system)
9. [Monitoring](#monitoring)
10. [Security](#security)
11. [Troubleshooting](#troubleshooting)

---

## Overview

INTcoin includes a full-featured mining pool server with:

- **Stratum Protocol v1**: Standard mining protocol for efficient work distribution
- **Variable Difficulty (VarDiff)**: Automatic per-worker difficulty adjustment
- **Multiple Payout Methods**: PPLNS, PPS, Proportional
- **HTTP API**: RESTful API for pool statistics and dashboard
- **Database Integration**: Persistent storage for shares, blocks, and payments
- **Real-time Statistics**: Live hashrate, workers, shares tracking

### Features

✅ **Worker Management**: Auto-registration, difficulty adjustment
✅ **Share Validation**: Accurate share difficulty verification
✅ **Block Detection**: Automatic block submission to blockchain
✅ **Payout Calculation**: Fair reward distribution
✅ **Web Dashboard**: Real-time pool statistics
✅ **Multiple Workers**: Support for thousands of concurrent miners

---

## System Requirements

### Minimum Requirements

| Component | Requirement |
|-----------|-------------|
| **OS** | Linux (Ubuntu 24.04+), FreeBSD 13+ |
| **CPU** | 4 cores |
| **RAM** | 8 GB |
| **Storage** | 100 GB SSD (for blockchain + pool database) |
| **Network** | 100 Mbps, static IP address |
| **Ports** | 3333 (Stratum), 8080 (HTTP API), 2211 (RPC) |

### Recommended Specifications

- **CPU**: 8+ cores
- **RAM**: 16 GB
- **Storage**: 500 GB NVMe SSD
- **Network**: 1 Gbps with DDoS protection
- **Backup**: Regular database backups

---

## Pool Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Mining Pool Server                        │
│  ┌────────────────┐  ┌──────────────┐  ┌─────────────────┐ │
│  │ Stratum Server │  │ HTTP API     │  │ Pool Database   │ │
│  │ (Port 3333)    │  │ (Port 8080)  │  │ (RocksDB)       │ │
│  └────────┬───────┘  └──────┬───────┘  └────────┬────────┘ │
│           │                  │                    │          │
│  ┌────────▼──────────────────▼────────────────────▼────────┐ │
│  │           Mining Pool Manager (Core Logic)              │ │
│  │  - Worker management    - Share validation             │ │
│  │  - VarDiff adjustment   - Block detection              │ │
│  │  - Payout calculation   - Statistics tracking          │ │
│  └────────────────────────┬─────────────────────────────────┘ │
└────────────────────────────┼───────────────────────────────────┘
                             │
                             ▼
                    ┌────────────────┐
                    │  intcoind      │
                    │  (Full Node)   │
                    │  Port 2211 RPC │
                    └────────────────┘

                    ┌────────────────┐
                    │  Pool Miners   │
                    │  (Stratum)     │
                    │  Port 3333     │
                    └────────────────┘
```

### Components

1. **Stratum Server**: TCP server accepting miner connections, handling:
   - `mining.subscribe` - Worker subscription
   - `mining.authorize` - Worker authentication
   - `mining.submit` - Share submission
   - `mining.notify` - Job distribution

2. **HTTP API Server**: RESTful API providing:
   - `/api/pool/stats` - Pool statistics
   - `/api/pool/blocks` - Recent blocks found
   - `/api/pool/payments` - Recent payments
   - `/api/pool/topminers` - Top miners leaderboard
   - `/api/pool/worker?address=...` - Worker-specific stats

3. **Pool Database**: Persistent storage for:
   - Worker records (address, hashrate, shares)
   - Share history (difficulty, timestamp, validity)
   - Block records (height, hash, reward, status)
   - Payment records (address, amount, txid)

4. **VarDiff Manager**: Dynamic difficulty adjustment based on:
   - Worker hashrate
   - Share submission rate
   - Target share time (e.g., 10 seconds)

---

## Installation

### Prerequisites

1. **intcoind**: Full node must be running and synced
2. **RocksDB**: Database for pool storage
3. **Build Tools**: CMake, C++23 compiler

### Build Pool Server

```bash
# Clone repository
git clone https://github.com/INT-devs/intcoin.git intcoin
cd intcoin

# Build with pool support
mkdir build && cd build
cmake -DBUILD_POOL=ON -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Verify pool binary exists
ls -lh bin/intcoin-pool-server
# Expected: ~8 MB executable
```

### Install Pool Server

```bash
# Install to system
sudo make install

# Verify installation
which intcoin-pool-server
# Expected: /usr/local/bin/intcoin-pool-server

# Create pool data directory
sudo mkdir -p /var/lib/intcoin-pool
sudo chown $USER:$USER /var/lib/intcoin-pool
```

---

## Configuration

### Pool Configuration File

Create `/etc/intcoin/pool.conf`:

```conf
# INTcoin Mining Pool Configuration
# Last Updated: December 25, 2025

# ============================================================================
# Network Settings
# ============================================================================

# Stratum server port (default: 3333)
stratum-port=3333

# HTTP API port (default: 8080)
http-port=8080

# Bind address (0.0.0.0 = all interfaces)
bind-address=0.0.0.0

# ============================================================================
# Blockchain Connection
# ============================================================================

# intcoind RPC connection
rpc-host=127.0.0.1
rpc-port=2211
rpc-user=pooloperator
rpc-password=SecurePassword123!

# Block template update interval (seconds)
template-update-interval=5

# ============================================================================
# Pool Settings
# ============================================================================

# Pool name (shown in blocks)
pool-name=MyINTPool

# Pool fee (percentage, 0-100)
pool-fee=1.5

# Minimum payout threshold (INT)
payout-threshold=1000000000

# Payout method: pplns, pps, proportional
payout-method=pplns

# PPLNS window (number of shares)
pplns-window=1000000

# ============================================================================
# VarDiff Settings
# ============================================================================

# Enable variable difficulty
vardiff-enabled=true

# Target share time (seconds)
vardiff-target-time=10

# Difficulty adjustment window (shares)
vardiff-adjustment-window=10

# Minimum difficulty
vardiff-min-difficulty=1000

# Maximum difficulty
vardiff-max-difficulty=1000000000

# ============================================================================
# Database Settings
# ============================================================================

# Database path
db-path=/var/lib/intcoin-pool/pooldb

# Database cache size (MB)
db-cache-size=512

# ============================================================================
# Worker Settings
# ============================================================================

# Allow anonymous workers (auto-register)
allow-anonymous=true

# Worker timeout (seconds)
worker-timeout=300

# Maximum workers per IP
max-workers-per-ip=10

# ============================================================================
# Logging
# ============================================================================

# Log level: debug, info, warning, error
log-level=info

# Log file path
log-file=/var/log/intcoin-pool/pool.log

# Log rotation size (MB)
log-rotation-size=100

# Keep N old log files
log-rotation-count=10
```

### intcoind Configuration

Ensure `intcoin.conf` has RPC enabled:

```conf
# RPC Settings
server=1
rpcuser=pooloperator
rpcpassword=SecurePassword123!
rpcallowip=127.0.0.1
rpcport=2211

# Mining Settings
# (Pool server will use getblocktemplate)
```

---

## Running the Pool

### Start intcoind

```bash
# Start full node
intcoind -daemon

# Wait for sync
intcoin-cli getblockchaininfo

# Verify RPC access
intcoin-cli -rpcuser=pooloperator -rpcpassword=SecurePassword123! getblockcount
```

### Start Pool Server

```bash
# Start pool server
intcoin-pool-server --config=/etc/intcoin/pool.conf

# Expected output:
# [INFO] INTcoin Pool Server v1.0.0-alpha
# [INFO] Loading configuration from /etc/intcoin/pool.conf
# [INFO] Connecting to intcoind at 127.0.0.1:2211
# [INFO] Database opened: /var/lib/intcoin-pool/pooldb
# [INFO] Stratum server listening on 0.0.0.0:3333
# [INFO] HTTP API server listening on 0.0.0.0:8080
# [INFO] Pool ready - accepting connections
```

### Run as Systemd Service

Create `/etc/systemd/system/intcoin-pool.service`:

```ini
[Unit]
Description=INTcoin Mining Pool Server
After=network.target intcoind.service
Requires=intcoind.service

[Service]
Type=simple
User=intcoin-pool
Group=intcoin-pool
ExecStart=/usr/local/bin/intcoin-pool-server --config=/etc/intcoin/pool.conf
Restart=on-failure
RestartSec=10

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=intcoin-pool

# Security
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=full
ProtectHome=true

[Install]
WantedBy=multi-user.target
```

Enable and start service:

```bash
# Create pool user
sudo useradd -r -s /bin/false intcoin-pool
sudo chown -R intcoin-pool:intcoin-pool /var/lib/intcoin-pool

# Enable service
sudo systemctl daemon-reload
sudo systemctl enable intcoin-pool
sudo systemctl start intcoin-pool

# Check status
sudo systemctl status intcoin-pool

# View logs
sudo journalctl -u intcoin-pool -f
```

---

## Pool Dashboard

### API Endpoints

The HTTP API provides JSON endpoints for pool statistics:

#### GET /api/pool/stats

Pool-wide statistics:

```bash
curl http://localhost:8080/api/pool/stats
```

Response:
```json
{
  "hashrate": 1250000000,
  "difficulty": 5000000,
  "miners": 42,
  "blocks_found": 127,
  "total_shares": 1523456,
  "valid_shares_24h": 85234
}
```

#### GET /api/pool/blocks?limit=10

Recent blocks found:

```bash
curl http://localhost:8080/api/pool/blocks?limit=10
```

Response:
```json
[
  {
    "height": 12345,
    "hash": "00000abc123...",
    "timestamp": 1735142400000,
    "finder": "int1qxyz...",
    "reward": 105113636,
    "status": "confirmed"
  }
]
```

#### GET /api/pool/payments?limit=20

Recent payments to miners:

```bash
curl http://localhost:8080/api/pool/payments?limit=20
```

Response:
```json
[
  {
    "payment_id": 1,
    "address": "int1qxyz...",
    "amount": 500000000,
    "txid": "abc123def...",
    "timestamp": 1735142400000
  }
]
```

#### GET /api/pool/topminers?limit=10

Top miners by hashrate:

```bash
curl http://localhost:8080/api/pool/topminers?limit=10
```

Response:
```json
[
  {
    "rank": 1,
    "address": "int1qxyz...",
    "hashrate": 50000000,
    "shares": 12345
  }
]
```

#### GET /api/pool/worker?address=int1...

Worker-specific statistics:

```bash
curl "http://localhost:8080/api/pool/worker?address=int1qxyz..."
```

Response:
```json
{
  "address": "int1qxyz...",
  "hashrate": 5000000,
  "shares": 1234,
  "balance": 250000000,
  "total_paid": 1500000000
}
```

### Web Dashboard Integration

The pool dashboard (in `web/pool-dashboard/`) uses these APIs:

```bash
# Install Node.js dependencies
cd web/pool-dashboard
npm install

# Configure API endpoint
echo "REACT_APP_API_URL=http://pool.example.com:8080" > .env

# Build production bundle
npm run build

# Serve with nginx
sudo cp -r build/* /var/www/pool-dashboard/
```

Nginx configuration:

```nginx
server {
    listen 80;
    server_name pool.example.com;

    # Dashboard frontend
    location / {
        root /var/www/pool-dashboard;
        try_files $uri /index.html;
    }

    # API proxy
    location /api/ {
        proxy_pass http://localhost:8080/api/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

---

## Payout System

### Payout Methods

#### PPLNS (Pay Per Last N Shares)

**Description**: Shares are paid only if found within the last N shares when a block is found.

**Advantages**:
- Discourages pool hopping
- More fair to consistent miners
- Lower variance for pool operator

**Configuration**:
```conf
payout-method=pplns
pplns-window=1000000  # Last 1M shares
```

**Calculation**:
```
Miner Reward = (Miner Shares in Window / Total Shares in Window) × Block Reward × (1 - Pool Fee)
```

#### PPS (Pay Per Share)

**Description**: Each valid share is paid immediately at expected value.

**Advantages**:
- No variance for miners
- Predictable income
- Instant payouts possible

**Risk**: Pool operator absorbs variance (needs reserves)

**Configuration**:
```conf
payout-method=pps
```

**Calculation**:
```
Per Share Value = (Block Reward / Difficulty) × (1 - Pool Fee)
Miner Reward = Shares Submitted × Per Share Value
```

#### Proportional

**Description**: Shares are distributed proportionally when a block is found.

**Advantages**:
- Simple to understand
- Fair for short-term miners

**Disadvantages**:
- Vulnerable to pool hopping

**Configuration**:
```conf
payout-method=proportional
```

**Calculation**:
```
Miner Reward = (Miner Shares This Round / Total Shares This Round) × Block Reward × (1 - Pool Fee)
```

### Payout Execution

```bash
# Manual payout trigger (as pool operator)
intcoin-cli -rpcuser=pooloperator -rpcpassword=SecurePassword123! \
    pool_processpayouts

# Expected: Creates transaction paying all balances above threshold
```

Automatic payouts (add to crontab):

```bash
# Process payouts every 6 hours
0 */6 * * * /usr/local/bin/intcoin-cli pool_processpayouts >> /var/log/intcoin-pool/payouts.log 2>&1
```

---

## Monitoring

### Real-time Statistics

Monitor pool via CLI:

```bash
# Pool statistics
intcoin-cli pool_getstats

# Worker count
intcoin-cli pool_getworkercount

# Recent blocks
intcoin-cli pool_getblocks 10

# Pending payouts
intcoin-cli pool_getpendingpayouts
```

### Log Monitoring

```bash
# Follow pool logs
tail -f /var/log/intcoin-pool/pool.log

# Search for errors
grep ERROR /var/log/intcoin-pool/pool.log

# Monitor connections
grep "Worker connected" /var/log/intcoin-pool/pool.log | wc -l
```

### Prometheus Metrics (Optional)

Export metrics for Prometheus/Grafana:

```bash
# Install prometheus exporter
curl http://localhost:8080/metrics

# Example metrics:
# intcoin_pool_hashrate 1250000000
# intcoin_pool_workers 42
# intcoin_pool_blocks_found 127
```

---

## Security

### Firewall Configuration

```bash
# Allow Stratum (3333)
sudo ufw allow 3333/tcp comment 'INTcoin Pool Stratum'

# Allow HTTP API (8080) - restrict to specific IPs if possible
sudo ufw allow from 203.0.113.0/24 to any port 8080 proto tcp

# Allow SSH (22)
sudo ufw allow 22/tcp

# Enable firewall
sudo ufw enable
```

### DDoS Protection

```bash
# Install fail2ban
sudo apt install fail2ban

# Create pool jail
sudo nano /etc/fail2ban/jail.d/intcoin-pool.conf
```

```ini
[intcoin-pool-stratum]
enabled = true
port = 3333
filter = intcoin-pool
logpath = /var/log/intcoin-pool/pool.log
maxretry = 5
bantime = 3600
findtime = 600
```

### SSL/TLS Support

For secure Stratum connections:

```conf
# In pool.conf
stratum-ssl-enabled=true
stratum-ssl-cert=/etc/letsencrypt/live/pool.example.com/fullchain.pem
stratum-ssl-key=/etc/letsencrypt/live/pool.example.com/privkey.pem
```

Miners connect with:
```bash
./intcoin-miner --pool --pool-host=pool.example.com --pool-port=3334 --pool-ssl
```

---

## Troubleshooting

### Pool Won't Start

**Problem**: Pool server fails to start

**Solutions**:
```bash
# Check intcoind is running
intcoin-cli getblockcount

# Verify RPC credentials
intcoin-cli -rpcuser=pooloperator -rpcpassword=SecurePassword123! getnetworkinfo

# Check port availability
sudo netstat -tlnp | grep 3333

# Review logs
tail -f /var/log/intcoin-pool/pool.log
```

### Workers Can't Connect

**Problem**: Miners cannot connect to pool

**Solutions**:
```bash
# Test Stratum port
telnet pool.example.com 3333

# Check firewall
sudo ufw status

# Verify bind address
grep bind-address /etc/intcoin/pool.conf

# Check worker logs on miner side
./intcoin-miner --pool --pool-host=pool.example.com --pool-port=3333 --verbose
```

### High Share Rejection Rate

**Problem**: Many shares rejected

**Causes**:
- Stale work (template not updated fast enough)
- Incorrect difficulty calculation
- Worker submitting invalid shares

**Solutions**:
```bash
# Reduce template update interval
# In pool.conf:
template-update-interval=3  # 3 seconds instead of 5

# Check share validation logs
grep "Share rejected" /var/log/intcoin-pool/pool.log

# Verify difficulty calculation
intcoin-cli pool_getworkerstats <worker_address>
```

### No Payouts Processing

**Problem**: Balances not being paid out

**Solutions**:
```bash
# Check pending payouts
intcoin-cli pool_getpendingpayouts

# Verify payout threshold
grep payout-threshold /etc/intcoin/pool.conf

# Check wallet balance (pool needs funds for transaction fees)
intcoin-cli getbalance

# Manually trigger payout
intcoin-cli pool_processpayouts

# Check transaction pool
intcoin-cli getmempoolinfo
```

### Database Corruption

**Problem**: Pool database errors

**Solutions**:
```bash
# Stop pool
sudo systemctl stop intcoin-pool

# Backup database
cp -r /var/lib/intcoin-pool/pooldb /var/lib/intcoin-pool/pooldb.backup

# Repair database (if RocksDB)
intcoin-pool-server --db-repair --db-path=/var/lib/intcoin-pool/pooldb

# Restart pool
sudo systemctl start intcoin-pool
```

---

## Best Practices

### Regular Backups

```bash
# Automated daily backup script
#!/bin/bash
DATE=$(date +%Y%m%d)
BACKUP_DIR=/backup/intcoin-pool

# Stop pool temporarily
systemctl stop intcoin-pool

# Backup database
tar -czf $BACKUP_DIR/pooldb-$DATE.tar.gz /var/lib/intcoin-pool/pooldb

# Backup configuration
cp /etc/intcoin/pool.conf $BACKUP_DIR/pool.conf-$DATE

# Restart pool
systemctl start intcoin-pool

# Remove backups older than 30 days
find $BACKUP_DIR -name "pooldb-*.tar.gz" -mtime +30 -delete
```

### Monitoring Alerts

Set up alerts for:
- Pool hashrate drops below threshold
- Worker count sudden changes
- Block found notifications
- Payout failures
- Database errors

### Performance Tuning

```conf
# Optimize for high-traffic pools
# In pool.conf:

# Increase database cache
db-cache-size=2048  # 2 GB

# Reduce update interval for large pools
template-update-interval=2

# Increase max workers
max-workers-per-ip=50

# Optimize VarDiff
vardiff-adjustment-window=20  # More samples
```

---

## See Also

- [MINING.md](MINING.md) - Mining guide for pool miners
- [RPC.md](RPC.md) - Pool RPC commands
- [ARCHITECTURE.md](ARCHITECTURE.md) - Pool architecture details

---

**Last Updated**: December 25, 2025
**Status**: Complete - Mining pool server fully operational

For support, visit: https://discord.gg/Y7dX4Ps2Ha or email pool-support@international-coin.org
