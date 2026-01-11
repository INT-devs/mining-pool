# INTcoin Mining Pool Dashboard

A modern, responsive web dashboard for monitoring INTcoin mining pool statistics.

## Features

- **Real-time Pool Statistics**
  - Pool hashrate
  - Network difficulty
  - Active miners count
  - Total blocks found
  - Valid shares (24h)

- **Worker Statistics**
  - Individual worker hashrate
  - Valid shares
  - Pending balance
  - Total paid amount

- **Recent Activity**
  - Recently found blocks with status
  - Recent payments with transaction IDs
  - Top miners leaderboard (24h)

- **Connection Information**
  - Pool address and port
  - Example mining commands
  - Supported mining software

## Installation

### Method 1: Standalone Web Server

Serve the dashboard using any web server (nginx, Apache, or simple Python server):

```bash
# Using Python 3
cd web/pool-dashboard
python3 -m http.server 8080

# Access at: http://localhost:8080
```

### Method 2: Nginx Configuration

```nginx
server {
    listen 80;
    server_name pool.international-coin.org;

    root /path/to/intcoin/web/pool-dashboard;
    index index.html;

    location / {
        try_files $uri $uri/ =404;
    }

    # Proxy RPC requests to pool server
    location /rpc {
        proxy_pass http://localhost:2214/rpc;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

### Method 3: Apache Configuration

```apache
<VirtualHost *:80>
    ServerName pool.international-coin.org
    DocumentRoot /path/to/intcoin/web/pool-dashboard

    <Directory /path/to/intcoin/web/pool-dashboard>
        Options -Indexes +FollowSymLinks
        AllowOverride None
        Require all granted
    </Directory>

    # Proxy RPC requests
    ProxyPass /rpc http://localhost:2214/rpc
    ProxyPassReverse /rpc http://localhost:2214/rpc
</VirtualHost>
```

## Configuration

Edit `dashboard.js` to configure the dashboard:

```javascript
const CONFIG = {
    rpcUrl: 'http://localhost:2214/rpc',  // Pool RPC endpoint
    updateInterval: 30000,                 // Update every 30 seconds
    rpcUser: 'poolapi',                   // RPC username
    rpcPassword: 'changeme'               // RPC password
};
```

## Required RPC Methods

The dashboard requires the following RPC methods to be implemented in the pool server:

### `pool_getstats`
Returns global pool statistics.

**Response**:
```json
{
    "hashrate": 125000000,
    "difficulty": 1000000,
    "miners": 42,
    "blocks_found": 156,
    "total_shares": 1234567,
    "valid_shares_24h": 89234
}
```

### `pool_getblocks [limit]`
Returns recently found blocks.

**Parameters**:
- `limit` (optional): Number of blocks to return (default: 10)

**Response**:
```json
[
    {
        "height": 123456,
        "hash": "a1b2c3d4e5f6...",
        "timestamp": 1735084800000,
        "finder": "intc1qxy2kgdygjrs...",
        "reward": 105113636,
        "status": "confirmed"
    }
]
```

### `pool_getpayments [limit]`
Returns recent payments.

**Parameters**:
- `limit` (optional): Number of payments to return (default: 20)

**Response**:
```json
[
    {
        "timestamp": 1735084800000,
        "address": "intc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
        "amount": 5.25,
        "txid": "tx123456789abcdef"
    }
]
```

### `pool_gettopminers [limit]`
Returns top miners by hashrate.

**Parameters**:
- `limit` (optional): Number of miners to return (default: 10)

**Response**:
```json
[
    {
        "rank": 1,
        "address": "intc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
        "hashrate": 45000000,
        "shares": 12345
    }
]
```

### `pool_getworker [address]`
Returns statistics for a specific worker.

**Parameters**:
- `address` (required): Worker's INT address

**Response**:
```json
{
    "address": "intc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh",
    "hashrate": 12500000,
    "shares": 5678,
    "balance": 2.5,
    "total_paid": 15.75
}
```

## Mock Data Mode

The dashboard includes mock data for development/demonstration when the RPC server is unavailable. This allows you to preview the dashboard design without a running pool server.

To test with mock data:
1. Open `index.html` in a browser
2. The dashboard will automatically use mock data if RPC calls fail
3. All features will work with sample data

## Features

### Auto-Refresh
- Automatically updates every 30 seconds
- Last update timestamp displayed in footer
- Manual refresh by reloading page

### Worker Search
- Enter your INT address to view personal statistics
- Statistics saved in localStorage for convenience
- Real-time updates when auto-refresh is enabled

### Responsive Design
- Works on desktop, tablet, and mobile devices
- Tables adapt to smaller screens
- Touch-friendly interface

### Security
- RPC credentials should be configured server-side
- Use HTTPS in production
- Implement rate limiting on RPC endpoints

## Customization

### Branding
- Edit `index.html` to change logo and text
- Modify `style.css` for custom colors and themes
- Default theme uses purple gradient (#667eea to #764ba2)

### Update Interval
Change refresh rate in `dashboard.js`:
```javascript
updateInterval: 30000  // 30 seconds (in milliseconds)
```

### Table Limits
Adjust number of items displayed:
```javascript
loadRecentBlocks()  // Edit RPC call: pool_getblocks(limit)
loadRecentPayments() // Edit RPC call: pool_getpayments(limit)
loadTopMiners()     // Edit RPC call: pool_gettopminers(limit)
```

## Browser Support

- Chrome/Edge 90+
- Firefox 88+
- Safari 14+
- Opera 76+

## Performance

- Minimal JavaScript dependencies (vanilla JS)
- CSS optimized for performance
- Lazy loading for images (if added)
- Efficient DOM updates

## Security Considerations

1. **RPC Authentication**: Always use strong credentials
2. **HTTPS**: Deploy with SSL/TLS in production
3. **CORS**: Configure appropriate CORS headers
4. **Rate Limiting**: Implement on server-side
5. **Input Validation**: Validate worker addresses before lookup

## Troubleshooting

**Dashboard shows "Loading..." forever**:
- Check RPC endpoint configuration
- Verify pool server is running
- Check browser console for errors
- Enable mock data mode for testing

**Worker stats not loading**:
- Verify INT address is correct
- Check that worker has mined recently
- Ensure pool server recognizes the worker

**Styles not loading**:
- Verify `style.css` path is correct
- Check browser console for 404 errors
- Clear browser cache

## Contributing

To contribute to the dashboard:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

MIT License - Same as INTcoin Core

## Support

For issues or questions:
- GitHub: https://github.com/intcoin/crypto/issues
- Discord: https://discord.gg/Y7dX4Ps2Ha
- Email: support@international-coin.org

---

**Version**: 1.0.0
**Last Updated**: December 24, 2025
