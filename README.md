# INTcoin Mining Pool

Official mining pool software for the INTcoin cryptocurrency.

**Repository**: https://github.com/INT-devs/mining-pool

## Overview

This repository contains everything needed to set up and operate an INTcoin mining pool:

- **Pool Server**: Full Stratum v1 protocol implementation
- **Web Dashboard**: Real-time pool statistics and miner interface
- **Monitoring**: Grafana dashboards and Prometheus metrics
- **Deployment**: Apache/nginx configuration and webhook automation

## Directory Structure

```
mining-pool/
├── src/pool/           # Pool server source code (C++)
├── include/intcoin/    # Pool header files
├── web/pool-dashboard/ # Web dashboard (HTML/CSS/JS)
├── docs/               # Documentation
│   ├── POOL_SETUP.md           # Complete setup guide
│   ├── Mining-Pool-Stratum.md  # Stratum protocol reference
│   └── GRAFANA_DASHBOARDS.md   # Monitoring setup
├── tests/              # Pool test suites
└── deploy/apache/      # Deployment configurations
```

## Quick Start

### Prerequisites

- INTcoin full node (`intcoind`) running and synced
- C++23 compiler (GCC 13+, Clang 16+)
- CMake 4.2.0+
- RocksDB 10.7+
- Boost 1.83.0+

### Build

```bash
git clone https://github.com/INT-devs/mining-pool.git
cd mining-pool

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Run

```bash
# Configure pool (see docs/POOL_SETUP.md for full options)
cp pool.conf.example /etc/intcoin/pool.conf
# Edit configuration...

# Start pool server
./intcoin-pool-server --config=/etc/intcoin/pool.conf
```

## Documentation

| Document | Description |
|----------|-------------|
| [POOL_SETUP.md](docs/POOL_SETUP.md) | Complete installation and configuration guide |
| [Mining-Pool-Stratum.md](docs/Mining-Pool-Stratum.md) | Stratum protocol specification |
| [GRAFANA_DASHBOARDS.md](docs/GRAFANA_DASHBOARDS.md) | Prometheus/Grafana monitoring setup |

## Features

- **Stratum v1 Protocol**: Compatible with all Stratum miners
- **Variable Difficulty (VarDiff)**: Automatic per-worker difficulty adjustment
- **Multiple Payout Methods**: PPLNS, PPS, Proportional
- **HTTP API**: RESTful API for pool statistics
- **Web Dashboard**: Real-time pool monitoring
- **Post-Quantum Security**: Dilithium3 signatures for payouts
- **RandomX Mining**: Full CPU-optimized mining support

## API Endpoints

| Endpoint | Description |
|----------|-------------|
| `GET /api/pool/stats` | Pool-wide statistics |
| `GET /api/pool/blocks` | Recent blocks found |
| `GET /api/pool/payments` | Recent payments |
| `GET /api/pool/topminers` | Top miners leaderboard |
| `GET /api/pool/worker?address=...` | Worker-specific stats |

## Web Dashboard

The pool dashboard is located in `web/pool-dashboard/`. To deploy:

```bash
# Copy to web server
cp -r web/pool-dashboard/* /var/www/html/

# Configure nginx/Apache proxy for API
# See deploy/apache/ for example configurations
```

## Related Repositories

- [INT-devs/intcoin](https://github.com/INT-devs/intcoin) - INTcoin core node
- [INT-devs/intcoin-miner](https://github.com/INT-devs/intcoin-miner) - Solo/pool miner

## Support

- **Discord**: https://discord.gg/Y7dX4Ps2Ha
- **Website**: https://international-coin.org
- **Email**: pool-support@international-coin.org

## License

MIT License - See LICENSE file for details.

---

**Version**: 1.0.0-beta
**Last Updated**: January 11, 2026
