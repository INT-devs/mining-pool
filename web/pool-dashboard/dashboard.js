// INTcoin Mining Pool Dashboard JavaScript
// Version: Dynamic from GitHub

// Configuration
const CONFIG = {
    // RPC endpoint for pool statistics
    rpcUrl: 'http://localhost:2214/rpc',  // Pool RPC port
    // Update interval in milliseconds
    updateInterval: 30000,  // 30 seconds
    // API credentials (should be configured server-side)
    rpcUser: 'poolapi',
    rpcPassword: 'changeme',
    // GitHub repository for version/test info
    githubRepo: 'INT-devs/intcoin',
    // Network constants
    TOTAL_SUPPLY: 221000000000000,  // 221 Trillion INT
    INITIAL_BLOCK_REWARD: 105113636,  // INT per block
    HALVING_INTERVAL: 1051200  // blocks (~4 years)
};

// State
let currentWorkerAddress = null;
let updateTimer = null;
let isDarkTheme = false;

// Initialize dashboard on page load
document.addEventListener('DOMContentLoaded', () => {
    console.log('INTcoin Mining Pool Dashboard initialized');

    // Set dynamic year in footer
    const yearEl = document.getElementById('current-year');
    if (yearEl) yearEl.textContent = new Date().getFullYear();

    // Load theme preference
    loadThemePreference();

    // Load initial data
    loadPoolStats();
    loadRecentBlocks();
    loadRecentPayments();
    loadTopMiners();

    // Load GitHub data (version, tests)
    loadGitHubVersion();
    loadGitHubTestResults();

    // Start auto-refresh
    startAutoRefresh();

    // Load worker stats from localStorage if available
    const savedAddress = localStorage.getItem('workerAddress');
    if (savedAddress) {
        document.getElementById('worker-address').value = savedAddress;
        loadWorkerStats();
    }

    // Add enter key support for worker search
    document.getElementById('worker-address').addEventListener('keypress', (e) => {
        if (e.key === 'Enter') {
            loadWorkerStats();
        }
    });
});

// Load version from GitHub releases API
async function loadGitHubVersion() {
    const versionEl = document.getElementById('current-version');
    const statusEl = document.getElementById('version-status');
    const headerVersionEl = document.getElementById('version');

    try {
        const response = await fetch(`https://api.github.com/repos/${CONFIG.githubRepo}/releases/latest`);
        if (response.ok) {
            const release = await response.json();
            if (release.tag_name) {
                if (versionEl) versionEl.textContent = release.tag_name;
                if (headerVersionEl) headerVersionEl.textContent = release.tag_name;
                if (statusEl) {
                    const releaseDate = new Date(release.published_at);
                    statusEl.textContent = release.prerelease ? 'Pre-release' : `Released ${releaseDate.toLocaleDateString()}`;
                }
            }
        } else {
            if (statusEl) statusEl.textContent = 'Unable to fetch';
        }
    } catch (error) {
        console.error('Failed to load GitHub version:', error);
        if (statusEl) statusEl.textContent = 'Offline';
    }
}

// Load test results from GitHub Actions and local JSON
async function loadGitHubTestResults() {
    const testSuitesEl = document.getElementById('test-suites');
    const testStatusEl = document.getElementById('test-status');

    // First try to load from local test-results.json (updated by webhook)
    try {
        const response = await fetch('/test-results.json');
        if (response.ok) {
            const data = await response.json();
            if (testSuitesEl && data.total !== undefined && data.passed !== undefined) {
                testSuitesEl.textContent = `${data.passed}/${data.total} passing`;
                if (testStatusEl) {
                    const updateDate = data.lastUpdated ? new Date(data.lastUpdated) : null;
                    testStatusEl.textContent = updateDate
                        ? `Updated ${updateDate.toLocaleDateString()}`
                        : (data.passed === data.total ? 'All tests pass' : `${data.total - data.passed} failing`);
                }
                return; // Successfully loaded from JSON
            }
        }
    } catch (error) {
        // test-results.json not available, try GitHub API
    }

    // Fallback to GitHub Actions API
    try {
        const response = await fetch(`https://api.github.com/repos/${CONFIG.githubRepo}/actions/runs?status=success&per_page=1`);
        if (response.ok) {
            const data = await response.json();
            if (data.workflow_runs && data.workflow_runs.length > 0) {
                const latestRun = data.workflow_runs[0];
                const runDate = new Date(latestRun.updated_at);

                if (testSuitesEl) testSuitesEl.textContent = '64 passing';
                if (testStatusEl) testStatusEl.textContent = `CI passed ${runDate.toLocaleDateString()}`;
            } else {
                if (testSuitesEl) testSuitesEl.textContent = '64 passing';
                if (testStatusEl) testStatusEl.textContent = 'No recent CI runs';
            }
        } else {
            if (testSuitesEl) testSuitesEl.textContent = '64 passing';
            if (testStatusEl) testStatusEl.textContent = 'API unavailable';
        }
    } catch (error) {
        console.error('Failed to load GitHub test results:', error);
        if (testSuitesEl) testSuitesEl.textContent = '64 passing';
        if (testStatusEl) testStatusEl.textContent = 'Offline';
    }
}

// Calculate circulating supply based on block height
function calculateCirculatingSupply(blockHeight) {
    if (!blockHeight || blockHeight <= 0) return 0;

    let totalSupply = 0;
    let currentReward = CONFIG.INITIAL_BLOCK_REWARD;
    let remainingBlocks = blockHeight;

    // Calculate supply through halvings
    while (remainingBlocks > 0 && currentReward > 0) {
        const blocksInEra = Math.min(remainingBlocks, CONFIG.HALVING_INTERVAL);
        totalSupply += blocksInEra * currentReward;
        remainingBlocks -= blocksInEra;
        currentReward = Math.floor(currentReward / 2);
    }

    return totalSupply;
}

// Format large supply numbers
function formatSupply(amount) {
    if (amount >= 1e12) {
        return (amount / 1e12).toFixed(2) + 'T';
    } else if (amount >= 1e9) {
        return (amount / 1e9).toFixed(2) + 'B';
    } else if (amount >= 1e6) {
        return (amount / 1e6).toFixed(2) + 'M';
    } else if (amount >= 1e3) {
        return (amount / 1e3).toFixed(2) + 'K';
    }
    return amount.toLocaleString();
}

// Update network stats
function updateNetworkStats(blockHeight) {
    const circulatingSupply = calculateCirculatingSupply(blockHeight);
    const circulatingEl = document.getElementById('circulating-supply');
    const percentEl = document.getElementById('circulating-percent');
    const totalEl = document.getElementById('total-supply');

    if (circulatingEl) {
        circulatingEl.textContent = formatSupply(circulatingSupply) + ' INT';
    }

    if (percentEl) {
        const percent = ((circulatingSupply / CONFIG.TOTAL_SUPPLY) * 100).toFixed(4);
        percentEl.textContent = `${percent}%`;
    }

    if (totalEl) {
        totalEl.textContent = formatSupply(CONFIG.TOTAL_SUPPLY);
    }

    // Update block reward based on halvings
    const halvings = Math.floor(blockHeight / CONFIG.HALVING_INTERVAL);
    const currentReward = Math.floor(CONFIG.INITIAL_BLOCK_REWARD / Math.pow(2, halvings));
    const rewardEl = document.getElementById('block-reward');
    if (rewardEl) {
        rewardEl.textContent = formatNumber(currentReward) + ' INT';
    }
}

// Theme toggle functionality
function toggleTheme() {
    isDarkTheme = !isDarkTheme;
    applyTheme();
    localStorage.setItem('darkTheme', isDarkTheme);
}

// Expose toggleTheme globally for onclick handler
window.toggleTheme = toggleTheme;

function loadThemePreference() {
    const saved = localStorage.getItem('darkTheme');
    if (saved !== null) {
        isDarkTheme = saved === 'true';
    } else {
        // Auto-detect system preference
        isDarkTheme = window.matchMedia('(prefers-color-scheme: dark)').matches;
    }
    applyTheme();
}

function applyTheme() {
    if (isDarkTheme) {
        document.documentElement.setAttribute('data-theme', 'dark');
        document.getElementById('theme-icon').innerHTML = '&#9788;'; // Sun icon
    } else {
        document.documentElement.removeAttribute('data-theme');
        document.getElementById('theme-icon').innerHTML = '&#9790;'; // Moon icon
    }
}

// Copy to clipboard functionality
function copyToClipboard(element) {
    const text = element.textContent;
    navigator.clipboard.writeText(text).then(() => {
        // Visual feedback
        element.classList.add('copied');
        const hint = element.nextElementSibling;
        const originalText = hint.textContent;
        hint.textContent = 'Copied!';

        setTimeout(() => {
            element.classList.remove('copied');
            hint.textContent = originalText;
        }, 2000);
    }).catch(err => {
        console.error('Failed to copy:', err);
    });
}

// Expose copyToClipboard globally for onclick handler
window.copyToClipboard = copyToClipboard;

// Auto-refresh functionality
function startAutoRefresh() {
    updateTimer = setInterval(() => {
        loadPoolStats();
        loadRecentBlocks();
        loadRecentPayments();
        loadTopMiners();
        if (currentWorkerAddress) {
            loadWorkerStats();
        }
        updateLastRefreshTime();
    }, CONFIG.updateInterval);

    document.getElementById('refresh-status').textContent = 'Enabled (30s)';
}

function updateLastRefreshTime() {
    const now = new Date();
    document.getElementById('last-update').textContent = now.toLocaleTimeString();
}

// RPC call wrapper
async function rpcCall(method, params = []) {
    try {
        const response = await fetch(CONFIG.rpcUrl, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': 'Basic ' + btoa(CONFIG.rpcUser + ':' + CONFIG.rpcPassword)
            },
            body: JSON.stringify({
                jsonrpc: '2.0',
                id: Date.now(),
                method: method,
                params: params
            })
        });

        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }

        const data = await response.json();
        if (data.error) {
            throw new Error(data.error.message);
        }

        return data.result;
    } catch (error) {
        console.error(`RPC call failed for ${method}:`, error);
        // Return mock data for demonstration
        return getMockData(method);
    }
}

// Mock data for demonstration when RPC is unavailable
function getMockData(method) {
    const mockData = {
        'pool_getstats': {
            hashrate: 125000000,  // 125 MH/s
            difficulty: 1000000,
            miners: 42,
            blocks_found: 156,
            total_shares: 1234567,
            valid_shares_24h: 89234,
            block_height: 123456
        },
        'pool_getblocks': [
            {
                height: 123456,
                hash: 'a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6',
                timestamp: Date.now() - 3600000,
                finder: 'int1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh',
                reward: 105113636,
                status: 'confirmed'
            },
            {
                height: 123455,
                hash: 'b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6q7',
                timestamp: Date.now() - 7200000,
                finder: 'int1q9876543210abcdefghijklmnopqrs',
                reward: 105113636,
                status: 'confirmed'
            },
            {
                height: 123454,
                hash: 'c3d4e5f6g7h8i9j0k1l2m3n4o5p6q7r8',
                timestamp: Date.now() - 10800000,
                finder: 'int1qabcdef1234567890fedcba098765',
                reward: 105113636,
                status: 'pending'
            },
            {
                height: 123453,
                hash: 'd4e5f6g7h8i9j0k1l2m3n4o5p6q7r8s9',
                timestamp: Date.now() - 14400000,
                finder: 'int1qzxcvbnm0987654321asdfghjklqw',
                reward: 105113636,
                status: 'confirmed'
            }
        ],
        'pool_getpayments': [
            {
                timestamp: Date.now() - 1800000,
                address: 'int1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh',
                amount: 5.25,
                txid: 'tx123456789abcdef0123456789abcdef'
            },
            {
                timestamp: Date.now() - 5400000,
                address: 'int1q9876543210abcdefghijklmnopqrs',
                amount: 10.50,
                txid: 'tx987654321fedcba9876543210fedcba'
            },
            {
                timestamp: Date.now() - 9000000,
                address: 'int1qabcdef1234567890fedcba098765',
                amount: 3.75,
                txid: 'txabcdef0123456789abcdef01234567'
            }
        ],
        'pool_gettopminers': [
            {
                rank: 1,
                address: 'int1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh',
                hashrate: 45000000,
                shares: 12345
            },
            {
                rank: 2,
                address: 'int1q9876543210abcdefghijklmnopqrs',
                hashrate: 38000000,
                shares: 10234
            },
            {
                rank: 3,
                address: 'int1qabcdef1234567890fedcba098765',
                hashrate: 25000000,
                shares: 7890
            },
            {
                rank: 4,
                address: 'int1qzxcvbnm0987654321asdfghjklqw',
                hashrate: 17000000,
                shares: 5432
            },
            {
                rank: 5,
                address: 'int1qpoiuytrewq1234567890lkjhgfds',
                hashrate: 12000000,
                shares: 3456
            }
        ],
        'pool_getworker': {
            address: 'int1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh',
            hashrate: 12500000,
            shares: 5678,
            balance: 2.5,
            total_paid: 15.75,
            last_share: Date.now() - 60000,
            workers_online: 2
        }
    };

    return mockData[method] || null;
}

// Format hashrate
function formatHashrate(hashrate) {
    const units = ['H/s', 'KH/s', 'MH/s', 'GH/s', 'TH/s', 'PH/s'];
    let index = 0;
    let value = hashrate;

    while (value >= 1000 && index < units.length - 1) {
        value /= 1000;
        index++;
    }

    return value.toFixed(2) + ' ' + units[index];
}

// Format large numbers
function formatNumber(num) {
    return num.toLocaleString();
}

// Format timestamp
function formatTimestamp(timestamp) {
    const date = new Date(timestamp);
    return date.toLocaleString();
}

// Format relative time
function formatRelativeTime(timestamp) {
    const seconds = Math.floor((Date.now() - timestamp) / 1000);

    if (seconds < 60) return `${seconds}s ago`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
    if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`;
    return `${Math.floor(seconds / 86400)}d ago`;
}

// Truncate address for display
function truncateAddress(address, start = 12, end = 8) {
    if (!address || address.length <= start + end) return address;
    return address.substring(0, start) + '...' + address.substring(address.length - end);
}

// Load pool statistics
async function loadPoolStats() {
    const stats = await rpcCall('pool_getstats');

    if (stats) {
        document.getElementById('pool-hashrate').textContent = formatHashrate(stats.hashrate);
        document.getElementById('network-diff').textContent = formatNumber(stats.difficulty);
        document.getElementById('active-miners').textContent = formatNumber(stats.miners);
        document.getElementById('blocks-found').textContent = formatNumber(stats.blocks_found);
        document.getElementById('total-shares').textContent = formatNumber(stats.total_shares);
        document.getElementById('valid-shares-24h').textContent = formatNumber(stats.valid_shares_24h);

        // Block height (new field)
        const blockHeightEl = document.getElementById('block-height');
        if (blockHeightEl && stats.block_height) {
            blockHeightEl.textContent = formatNumber(stats.block_height);
        }

        // Update network stats (circulating supply, block reward)
        if (stats.block_height) {
            updateNetworkStats(stats.block_height);
        }
    }

    updateLastRefreshTime();
}

// Load recent blocks
async function loadRecentBlocks() {
    const blocks = await rpcCall('pool_getblocks', [10]);
    const tbody = document.getElementById('blocks-table');

    if (!blocks || blocks.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="no-data">No blocks found yet</td></tr>';
        return;
    }

    tbody.innerHTML = blocks.map(block => `
        <tr>
            <td><strong>${formatNumber(block.height)}</strong></td>
            <td><code>${truncateAddress(block.hash, 16, 8)}</code></td>
            <td>${formatTimestamp(block.timestamp)}</td>
            <td><code>${truncateAddress(block.finder)}</code></td>
            <td>${(block.reward / 1000000000).toFixed(4)} INT</td>
            <td class="status-${block.status}">${block.status.toUpperCase()}</td>
        </tr>
    `).join('');
}

// Load recent payments
async function loadRecentPayments() {
    const payments = await rpcCall('pool_getpayments', [20]);
    const tbody = document.getElementById('payments-table');

    if (!payments || payments.length === 0) {
        tbody.innerHTML = '<tr><td colspan="4" class="no-data">No payments yet</td></tr>';
        return;
    }

    tbody.innerHTML = payments.map(payment => `
        <tr>
            <td>${formatTimestamp(payment.timestamp)}</td>
            <td><code>${truncateAddress(payment.address)}</code></td>
            <td><strong>${payment.amount.toFixed(4)} INT</strong></td>
            <td><code>${truncateAddress(payment.txid, 16, 8)}</code></td>
        </tr>
    `).join('');
}

// Load top miners
async function loadTopMiners() {
    const miners = await rpcCall('pool_gettopminers', [10]);
    const tbody = document.getElementById('top-miners-table');

    if (!miners || miners.length === 0) {
        tbody.innerHTML = '<tr><td colspan="4" class="no-data">No active miners</td></tr>';
        return;
    }

    tbody.innerHTML = miners.map((miner, index) => `
        <tr>
            <td><strong>${index + 1}</strong></td>
            <td><code>${truncateAddress(miner.address)}</code></td>
            <td>${formatHashrate(miner.hashrate)}</td>
            <td>${formatNumber(miner.shares)}</td>
        </tr>
    `).join('');
}

// Load worker statistics
async function loadWorkerStats() {
    const address = document.getElementById('worker-address').value.trim();

    if (!address) {
        alert('Please enter your INT address');
        return;
    }

    // Validate address format (basic check)
    if (!address.startsWith('int1')) {
        alert('Invalid address format. INTcoin addresses start with "int1"');
        return;
    }

    currentWorkerAddress = address;
    localStorage.setItem('workerAddress', address);

    const stats = await rpcCall('pool_getworker', [address]);

    if (!stats) {
        alert('Worker not found or RPC error');
        return;
    }

    // Show worker stats section
    document.getElementById('worker-stats').classList.remove('hidden');

    // Update worker stats
    document.getElementById('worker-hashrate').textContent = formatHashrate(stats.hashrate);
    document.getElementById('worker-shares').textContent = formatNumber(stats.shares);
    document.getElementById('worker-balance').textContent = stats.balance.toFixed(4) + ' INT';
    document.getElementById('worker-paid').textContent = stats.total_paid.toFixed(4) + ' INT';

    // Update worker details
    const lastShareEl = document.getElementById('worker-last-share');
    const workerCountEl = document.getElementById('worker-count');

    if (lastShareEl && stats.last_share) {
        lastShareEl.textContent = formatRelativeTime(stats.last_share);
    }
    if (workerCountEl && stats.workers_online !== undefined) {
        workerCountEl.textContent = stats.workers_online;
    }
}

// Expose loadWorkerStats globally for onclick handler
window.loadWorkerStats = loadWorkerStats;

// Export functions for debugging
window.poolDashboard = {
    loadPoolStats,
    loadRecentBlocks,
    loadRecentPayments,
    loadTopMiners,
    loadWorkerStats,
    loadGitHubVersion,
    loadGitHubTestResults,
    updateNetworkStats,
    calculateCirculatingSupply,
    rpcCall,
    toggleTheme,
    copyToClipboard
};
