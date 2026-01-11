# Grafana Dashboards for INTcoin

**Version**: 1.2.0-beta
**Last Updated**: January 2, 2026
**Status**: Production

---

## Table of Contents

1. [Introduction](#introduction)
2. [Prerequisites](#prerequisites)
3. [Dashboard Installation](#dashboard-installation)
4. [Dashboard Panels](#dashboard-panels)
5. [Alert Configuration](#alert-configuration)
6. [Custom Dashboards](#custom-dashboards)
7. [Troubleshooting](#troubleshooting)

---

## Introduction

Grafana provides powerful visualization for INTcoin node metrics collected by Prometheus. This guide includes pre-built dashboards for monitoring blockchain health, network performance, mempool activity, and system resources.

### Available Dashboards

1. **INTcoin Overview** - High-level node health
2. **Blockchain Metrics** - Block processing, difficulty, height
3. **Mempool Dashboard** - Transaction pool monitoring
4. **Network Dashboard** - Peer connections, bandwidth
5. **Mining Dashboard** - Hashrate, blocks mined
6. **System Resources** - CPU, memory, disk usage

---

## Prerequisites

### Required Software

**Prometheus** (v2.30+):
```bash
# macOS
brew install prometheus

# Ubuntu/Debian
sudo apt-get install prometheus

# Manual install
wget https://github.com/prometheus/prometheus/releases/download/v2.40.0/prometheus-2.40.0.linux-amd64.tar.gz
tar xvfz prometheus-*.tar.gz
cd prometheus-*
```

**Grafana** (v9.0+):
```bash
# macOS
brew install grafana

# Ubuntu/Debian
sudo apt-get install grafana

# Manual install
wget https://dl.grafana.com/oss/release/grafana-9.3.2.linux-amd64.tar.gz
tar -zxvf grafana-*.tar.gz
```

### INTcoin Configuration

Enable metrics in `intcoin.conf`:

```conf
# Enable Prometheus metrics endpoint
metrics.enabled=1
metrics.bind=127.0.0.1
metrics.port=9090
metrics.threads=2
```

### Prometheus Configuration

Configure Prometheus to scrape INTcoin (`prometheus.yml`):

```yaml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

scrape_configs:
  - job_name: 'intcoin-mainnet'
    static_configs:
      - targets: ['localhost:9090']
        labels:
          instance: 'node-1'
          network: 'mainnet'
```

Start Prometheus:
```bash
prometheus --config.file=prometheus.yml
```

### Grafana Setup

Start Grafana:
```bash
# macOS
brew services start grafana

# Linux
sudo systemctl start grafana-server
```

Access Grafana:
- URL: http://localhost:3000
- Default login: admin / admin (change on first login)

Add Prometheus data source:
1. Configuration → Data Sources → Add data source
2. Select "Prometheus"
3. URL: `http://localhost:9090`
4. Click "Save & Test"

---

## Dashboard Installation

### Method 1: Import from File

1. Download dashboard JSON from: [grafana/intcoin-overview.json](../grafana/intcoin-overview.json)
2. In Grafana: Dashboards → Import
3. Upload JSON file or paste JSON content
4. Select Prometheus data source
5. Click "Import"

### Method 2: Import by ID

1. Dashboards → Import
2. Enter Grafana.com dashboard ID: **XXXXX** (when published)
3. Select Prometheus data source
4. Click "Load" → "Import"

### Method 3: Manual Creation

See [Custom Dashboards](#custom-dashboards) section.

---

## Dashboard Panels

### 1. INTcoin Overview Dashboard

**Purpose**: High-level node health monitoring

#### Panel: Blockchain Height

**Query**:
```promql
intcoin_blockchain_height
```

**Visualization**: Graph (time series)
**Description**: Current blockchain height over time

#### Panel: Blocks Per Hour

**Query**:
```promql
rate(intcoin_blocks_processed_total[1h]) * 3600
```

**Visualization**: Stat
**Description**: Blocks processed in last hour

#### Panel: Transactions Per Second

**Query**:
```promql
rate(intcoin_transactions_processed_total[5m])
```

**Visualization**: Graph
**Description**: Transaction processing rate

#### Panel: Mempool Size

**Query**:
```promql
intcoin_mempool_size
```

**Visualization**: Graph
**Thresholds**:
- Green: < 1000
- Yellow: 1000-5000
- Red: > 5000

#### Panel: Peer Count

**Query**:
```promql
intcoin_peer_count
```

**Visualization**: Stat
**Thresholds**:
- Red: < 3
- Yellow: 3-8
- Green: > 8

#### Panel: Node Uptime

**Query**:
```promql
time() - process_start_time_seconds{job="intcoin-mainnet"}
```

**Visualization**: Stat
**Unit**: Duration (seconds)

### 2. Blockchain Metrics Dashboard

#### Panel: Block Processing Duration

**Query**:
```promql
rate(intcoin_block_processing_duration_sum[5m]) / rate(intcoin_block_processing_duration_count[5m])
```

**Visualization**: Graph
**Description**: Average block processing time

#### Panel: Block Processing Duration (Histogram)

**Query**:
```promql
histogram_quantile(0.95, rate(intcoin_block_processing_duration_bucket[5m]))
```

**Visualization**: Graph
**Description**: 95th percentile block processing time

#### Panel: Difficulty

**Query**:
```promql
intcoin_blockchain_difficulty
```

**Visualization**: Graph
**Y-axis**: Logarithmic scale

#### Panel: Block Size Distribution

**Query**:
```promql
histogram_quantile(0.50, rate(intcoin_block_size_bucket[5m]))
histogram_quantile(0.95, rate(intcoin_block_size_bucket[5m]))
histogram_quantile(0.99, rate(intcoin_block_size_bucket[5m]))
```

**Visualization**: Graph (multiple series)
**Description**: Median, 95th, and 99th percentile block sizes

### 3. Mempool Dashboard

#### Panel: Mempool Transactions by Priority

**Query**:
```promql
intcoin_mempool_priority_low
intcoin_mempool_priority_normal
intcoin_mempool_priority_high
intcoin_mempool_priority_htlc
intcoin_mempool_priority_bridge
intcoin_mempool_priority_critical
```

**Visualization**: Stacked graph
**Description**: Transaction count by priority level

#### Panel: Mempool Size (Bytes)

**Query**:
```promql
intcoin_mempool_bytes
```

**Visualization**: Graph
**Unit**: Bytes (IEC)

#### Panel: Mempool Accept/Reject Rate

**Query**:
```promql
rate(intcoin_mempool_accepted_total[5m])
rate(intcoin_mempool_rejected_total[5m])
```

**Visualization**: Graph (two series)
**Description**: Transactions accepted vs rejected per second

#### Panel: Transaction Fee Distribution

**Query**:
```promql
histogram_quantile(0.50, rate(intcoin_mempool_tx_fee_bucket[5m]))
histogram_quantile(0.95, rate(intcoin_mempool_tx_fee_bucket[5m]))
```

**Visualization**: Graph
**Unit**: Satoshis
**Description**: Median and 95th percentile fees

#### Panel: Top Priority Transactions

**Table Columns**:
- Priority level
- Count
- Total fees
- Average fee

**Queries**:
```promql
sum(intcoin_mempool_priority_low) by (priority)
sum(intcoin_mempool_priority_normal) by (priority)
sum(intcoin_mempool_priority_high) by (priority)
```

### 4. Network Dashboard

#### Panel: Network Bandwidth

**Query**:
```promql
rate(intcoin_bytes_sent_total[5m])
rate(intcoin_bytes_received_total[5m])
```

**Visualization**: Graph (two series)
**Unit**: Bytes per second

#### Panel: Messages Sent/Received

**Query**:
```promql
rate(intcoin_messages_sent_total[5m])
rate(intcoin_messages_received_total[5m])
```

**Visualization**: Graph

#### Panel: Message Processing Latency

**Query**:
```promql
histogram_quantile(0.95, rate(intcoin_message_processing_duration_bucket[5m]))
```

**Visualization**: Graph
**Unit**: Seconds
**Description**: 95th percentile message processing time

#### Panel: Peer Connections (Geographic)

Requires additional labels for peer location.

**Query**:
```promql
sum(intcoin_peer_count) by (country)
```

**Visualization**: Pie chart or world map

### 5. Mining Dashboard

#### Panel: Hashrate

**Query**:
```promql
intcoin_hashrate
```

**Visualization**: Graph
**Unit**: Hashes per second

#### Panel: Blocks Mined

**Query**:
```promql
intcoin_blocks_mined_total
```

**Visualization**: Counter
**Description**: Cumulative blocks mined by this node

#### Panel: Blocks Per Day

**Query**:
```promql
increase(intcoin_blocks_mined_total[24h])
```

**Visualization**: Stat

#### Panel: Mining Duration

**Query**:
```promql
histogram_quantile(0.50, rate(intcoin_mining_duration_bucket[1h]))
```

**Visualization**: Graph
**Description**: Median time to mine a block

#### Panel: Total Hashes Computed

**Query**:
```promql
intcoin_hashes_computed_total
```

**Visualization**: Counter

---

## Alert Configuration

### Prometheus Alert Rules

Create `alerts.yml`:

```yaml
groups:
  - name: intcoin_alerts
    interval: 30s
    rules:
      # Critical Alerts
      - alert: BlockchainStuck
        expr: rate(intcoin_blocks_processed_total[10m]) == 0
        for: 15m
        labels:
          severity: critical
        annotations:
          summary: "Blockchain not processing blocks"
          description: "No blocks processed in 15 minutes on {{ $labels.instance }}"

      - alert: NoActivePeers
        expr: intcoin_peer_count == 0
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "Node has no peer connections"
          description: "{{ $labels.instance }} has 0 peers for 5 minutes"

      - alert: HighBlockProcessingTime
        expr: rate(intcoin_block_processing_duration_sum[5m]) / rate(intcoin_block_processing_duration_count[5m]) > 10
        for: 10m
        labels:
          severity: warning
        annotations:
          summary: "Slow block processing"
          description: "Average block processing time is {{ $value }}s on {{ $labels.instance }}"

      # Warning Alerts
      - alert: MempoolCongestion
        expr: intcoin_mempool_size > 10000
        for: 15m
        labels:
          severity: warning
        annotations:
          summary: "Mempool highly congested"
          description: "Mempool has {{ $value }} transactions on {{ $labels.instance }}"

      - alert: LowPeerCount
        expr: intcoin_peer_count < 3
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Low peer count"
          description: "Only {{ $value }} peers connected on {{ $labels.instance }}"

      - alert: HighMemoryUsage
        expr: process_resident_memory_bytes > 4000000000
        for: 10m
        labels:
          severity: warning
        annotations:
          summary: "High memory usage"
          description: "Node using {{ $value | humanize }}B of memory on {{ $labels.instance }}"

      # Informational
      - alert: NewBlockMined
        expr: increase(intcoin_blocks_mined_total[1m]) > 0
        labels:
          severity: info
        annotations:
          summary: "Block mined"
          description: "{{ $labels.instance }} mined a new block!"
```

Load alerts in Prometheus:
```yaml
# prometheus.yml
rule_files:
  - 'alerts.yml'
```

### Grafana Alert Configuration

#### Configure in Grafana UI

1. Edit panel → Alert tab
2. Create alert rule:

**Example: Mempool Size Alert**
- **Name**: High Mempool Size
- **Condition**: WHEN avg() OF query(A, 5m, now) IS ABOVE 5000
- **Frequency**: Evaluate every 1m for 5m
- **Notification**: Send to "intcoin-alerts" channel

#### Alert Notification Channels

**Email**:
```
Type: Email
Addresses: ops@example.com
```

**Slack**:
```
Type: Slack
Webhook URL: https://hooks.slack.com/services/XXX/YYY/ZZZ
Channel: #intcoin-alerts
```

**PagerDuty**:
```
Type: PagerDuty
Integration Key: <your-key>
```

---

## Custom Dashboards

### Creating a Custom Panel

1. **Add Panel**:
   - Dashboard → Add panel → Add new panel

2. **Configure Query**:
   ```promql
   intcoin_mempool_size{instance="node-1"}
   ```

3. **Choose Visualization**:
   - Graph, Stat, Gauge, Table, Pie chart, etc.

4. **Set Panel Options**:
   - Title: "Mempool Size"
   - Description: "Current mempool transaction count"
   - Unit: "short" (for counts) or "bytes" (for sizes)

5. **Configure Thresholds** (optional):
   - Green: 0-1000
   - Yellow: 1000-5000
   - Red: 5000+

6. **Save** dashboard

### Example: Custom Network Health Panel

**Query**:
```promql
(
  (intcoin_peer_count > 5) * 100 +
  (rate(intcoin_blocks_processed_total[10m]) > 0) * 100 +
  (intcoin_mempool_size < 10000) * 100
) / 3
```

**Description**: Combined health score (0-100)
- 100 = Excellent (many peers, processing blocks, low mempool)
- 0 = Poor (no peers, no blocks, high mempool)

**Visualization**: Gauge
**Thresholds**:
- Red: 0-50
- Yellow: 50-75
- Green: 75-100

### Template Variables

Create dashboard variables for dynamic filtering:

1. **Settings** → Variables → Add variable

**Network Variable**:
```
Name: network
Label: Network
Type: Query
Query: label_values(intcoin_blockchain_height, network)
```

**Instance Variable**:
```
Name: instance
Label: Instance
Type: Query
Query: label_values(intcoin_blockchain_height{network="$network"}, instance)
```

Use in queries:
```promql
intcoin_blockchain_height{network="$network", instance="$instance"}
```

---

## Example Dashboard JSON

### Simple Overview Dashboard

```json
{
  "dashboard": {
    "title": "INTcoin Overview",
    "panels": [
      {
        "id": 1,
        "title": "Blockchain Height",
        "type": "graph",
        "targets": [
          {
            "expr": "intcoin_blockchain_height",
            "legendFormat": "{{instance}}"
          }
        ],
        "gridPos": {"h": 8, "w": 12, "x": 0, "y": 0}
      },
      {
        "id": 2,
        "title": "Mempool Size",
        "type": "stat",
        "targets": [
          {
            "expr": "intcoin_mempool_size"
          }
        ],
        "gridPos": {"h": 8, "w": 12, "x": 12, "y": 0},
        "options": {
          "colorMode": "background",
          "graphMode": "area",
          "orientation": "auto"
        },
        "fieldConfig": {
          "defaults": {
            "thresholds": {
              "mode": "absolute",
              "steps": [
                {"value": 0, "color": "green"},
                {"value": 1000, "color": "yellow"},
                {"value": 5000, "color": "red"}
              ]
            }
          }
        }
      },
      {
        "id": 3,
        "title": "Peer Count",
        "type": "stat",
        "targets": [
          {
            "expr": "intcoin_peer_count"
          }
        ],
        "gridPos": {"h": 4, "w": 6, "x": 0, "y": 8}
      },
      {
        "id": 4,
        "title": "Transactions/sec",
        "type": "graph",
        "targets": [
          {
            "expr": "rate(intcoin_transactions_processed_total[5m])",
            "legendFormat": "TPS"
          }
        ],
        "gridPos": {"h": 8, "w": 18, "x": 6, "y": 8}
      }
    ],
    "time": {"from": "now-6h", "to": "now"},
    "refresh": "30s"
  }
}
```

Save as `intcoin-overview.json` and import into Grafana.

---

## Troubleshooting

### No Data in Panels

**Check**:
1. Prometheus is scraping INTcoin:
   ```
   http://localhost:9090/targets
   ```
   Should show "UP" status for intcoin job

2. Metrics are exposed:
   ```bash
   curl http://localhost:9090/metrics | grep intcoin
   ```

3. Grafana data source is configured:
   - Configuration → Data Sources → Prometheus
   - Click "Test" button (should show "Data source is working")

### Metrics Not Updating

**Solutions**:
1. Check INTcoin metrics enabled:
   ```bash
   grep "metrics.enabled" ~/.intcoin/intcoin.conf
   ```

2. Restart Prometheus:
   ```bash
   sudo systemctl restart prometheus
   ```

3. Check Prometheus logs:
   ```bash
   journalctl -u prometheus -f
   ```

### Grafana Panels Show "No Data"

**Check query syntax**:
- Go to Explore tab
- Enter query
- Check for errors

**Verify time range**:
- Ensure time range includes data
- Try "Last 24 hours"

### High Memory Usage (Grafana/Prometheus)

**Prometheus storage retention**:
```yaml
# prometheus.yml
global:
  scrape_interval: 30s  # Increase interval
storage:
  retention.time: 15d  # Reduce retention from 30d
```

**Reduce resolution**:
- Dashboard settings → Time options
- Min interval: 1m (instead of 15s)

---

## Resources

- **Dashboard Repository**: [grafana/](../grafana/)
- **Prometheus Metrics**: [PROMETHEUS_METRICS.md](PROMETHEUS_METRICS.md)
- **Grafana Documentation**: https://grafana.com/docs
- **PromQL Guide**: https://prometheus.io/docs/prometheus/latest/querying/basics/
- **Dashboard Examples**: https://grafana.com/grafana/dashboards

---

**Maintained by**: INTcoin Core Development Team
**Last Updated**: January 2, 2026
**Version**: 1.2.0-beta
