# INTcoin Website Apache Deployment Guide

## Prerequisites

- Ubuntu 20.04/22.04 LTS or Debian 11/12
- Apache 2.4+
- PHP 7.4+ (for webhooks)
- Root or sudo access

## 1. Install Required Software

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Apache, PHP, and required modules
sudo apt install -y apache2 php libapache2-mod-php php-curl php-mbstring

# Enable required Apache modules
sudo a2enmod ssl
sudo a2enmod headers
sudo a2enmod rewrite
sudo a2enmod deflate
sudo a2enmod expires
```

## 2. Install SSL Certificate (Sectigo)

The site uses a Sectigo-issued SSL certificate. Place the certificate files in `/etc/ssl/certs/`:

```bash
# Full chain certificate (includes intermediate CAs for OCSP stapling)
/etc/ssl/certs/international-coin.org_fullchain.cer

# Private key file
/etc/ssl/certs/international-coin.org.key
```

To create the fullchain certificate, concatenate the server cert and intermediate certs:
```bash
cat international-coin.org_ssl_certificate.cer intermediate1.cer intermediate2.cer > international-coin.org_fullchain.cer
```

Set proper permissions:
```bash
sudo chmod 644 /etc/ssl/certs/international-coin.org_fullchain.cer
sudo chmod 600 /etc/ssl/certs/international-coin.org.key
sudo chown root:root /etc/ssl/certs/international-coin.org_fullchain.cer
sudo chown root:root /etc/ssl/certs/international-coin.org.key
```

The Apache configuration (`intcoin.conf`) is already configured to use these certificate paths.

## 3. Deploy Website Files

```bash
# Create website directory
sudo mkdir -p /var/www/html

# Copy public files to web root
sudo cp -r public/* /var/www/html/

# Set proper ownership
sudo chown -R www-data:www-data /var/www/html

# Set proper permissions
sudo find /var/www/html -type d -exec chmod 755 {} \;
sudo find /var/www/html -type f -exec chmod 644 {} \;

# Create logs and cache directories
sudo mkdir -p /var/www/html/logs
sudo mkdir -p /var/www/html/cache
sudo chown -R www-data:www-data /var/www/html/logs /var/www/html/cache
sudo chmod 755 /var/www/html/logs /var/www/html/cache
```

## 4. Configure Apache VirtualHost

```bash
# Copy Apache configuration
sudo cp apache/intcoin.conf /etc/apache2/sites-available/

# Disable default site (optional)
sudo a2dissite 000-default.conf

# Enable INTcoin site
sudo a2ensite intcoin.conf

# Test Apache configuration
sudo apache2ctl configtest

# If test passes, reload Apache
sudo systemctl reload apache2
```

## 5. Configure GitHub Webhooks

### 5.1 Generate Webhook Secret

```bash
# Generate a secure random secret
openssl rand -hex 32
```

Copy the output (e.g., `a1b2c3d4e5f6...`)

### 5.2 Update Webhook PHP Files

Edit both webhook files and replace `YOUR_GITHUB_WEBHOOK_SECRET_HERE`:

```bash
sudo nano /var/www/html/webhooks/update-roadmap.php
sudo nano /var/www/html/webhooks/update-docs.php
```

Change line:
```php
define('WEBHOOK_SECRET', 'YOUR_GITHUB_WEBHOOK_SECRET_HERE');
```

To:
```php
define('WEBHOOK_SECRET', 'a1b2c3d4e5f6...');  // Your generated secret
```

### 5.3 Set Up Webhooks on GitHub

1. Go to https://github.com/INT-devs/intcoin/settings/hooks
2. Click "Add webhook"

#### Roadmap Webhook:
- **Payload URL:** `https://international-coin.org/webhooks/update-roadmap.php`
- **Content type:** `application/json`
- **Secret:** Paste your generated secret
- **Events:** Select "Just the push event"
- **Active:** Check the box
- Click "Add webhook"

#### Documentation Webhook:
- **Payload URL:** `https://international-coin.org/webhooks/update-docs.php`
- **Content type:** `application/json`
- **Secret:** Same secret as above
- **Events:** Select "Just the push event"
- **Active:** Check the box
- Click "Add webhook"

### 5.4 Test Webhooks

```bash
# Watch webhook logs
tail -f /var/www/html/logs/webhook-*.log

# Make a test commit to ROADMAP.md or docs/
# Check logs for successful execution
```

## 6. Deploy Testnet Faucet

The faucet requires the testnet node to be running and the faucet service to be connected.

### 6.1 Enable Apache Proxy Modules

```bash
# Enable required proxy modules
sudo a2enmod proxy
sudo a2enmod proxy_http
sudo a2enmod headers

# Restart Apache
sudo systemctl restart apache2
```

### 6.2 Start Testnet Node and Faucet

```bash
# Ensure testnet node is running
sudo systemctl start intcoind-testnet

# Wait for node to sync (check with)
intcoin-cli -conf=/home/intcoin/.intcoin-testnet/intcoin.conf -testnet getblockchaininfo

# Start the faucet service
sudo systemctl start intcoin-faucet

# Enable on boot
sudo systemctl enable intcoin-faucet
```

### 6.3 Verify Faucet is Running

```bash
# Check faucet status
sudo systemctl status intcoin-faucet

# Test faucet API locally
curl http://127.0.0.1:8080/stats

# Test through Apache proxy
curl https://international-coin.org/api/faucet/stats
```

### 6.4 Fund the Faucet Wallet

The faucet needs testnet coins to distribute:

```bash
# Generate faucet wallet address
intcoin-cli -conf=/home/intcoin/.intcoin-testnet/intcoin.conf -testnet getnewaddress "faucet"

# Mine some blocks to the faucet address (if mining enabled)
# Or transfer coins from another testnet wallet
```

## 7. Configure Firewall

```bash
# If using UFW (Ubuntu Firewall)
sudo ufw allow 'Apache Full'
sudo ufw allow ssh
sudo ufw enable

# Verify rules
sudo ufw status
```

## 8. Security Hardening

### 8.1 Disable Server Tokens

```bash
sudo nano /etc/apache2/conf-available/security.conf
```

Set:
```apache
ServerTokens Prod
ServerSignature Off
```

### 8.2 Limit Request Size

Add to `/etc/apache2/apache2.conf`:
```apache
LimitRequestBody 10485760  # 10MB limit
```

### 8.3 Enable ModSecurity (Optional)

```bash
sudo apt install -y libapache2-mod-security2
sudo cp /etc/modsecurity/modsecurity.conf-recommended /etc/modsecurity/modsecurity.conf
sudo nano /etc/modsecurity/modsecurity.conf
# Change: SecRuleEngine DetectionOnly -> SecRuleEngine On
sudo systemctl restart apache2
```

## 9. Monitoring & Maintenance

### 9.1 Log Rotation

Create `/etc/logrotate.d/intcoin-webhooks`:
```
/var/www/html/logs/*.log {
    daily
    rotate 30
    compress
    delaycompress
    notifempty
    create 644 www-data www-data
}
```

### 9.2 Monitor Logs

```bash
# Apache error log
sudo tail -f /var/log/apache2/intcoin-error.log

# Apache access log
sudo tail -f /var/log/apache2/intcoin-access.log

# Webhook logs
sudo tail -f /var/www/html/logs/webhook-*.log
```

### 9.3 SSL Certificate Renewal

Sectigo certificates must be renewed manually before expiration. Contact admin@international-coin.org for renewal procedures. Certificate expiration can be checked with:
```bash
openssl x509 -enddate -noout -in /etc/ssl/certs/international-coin.org_ssl_certificate.cer
```

### 9.4 System Updates

```bash
# Regular updates
sudo apt update && sudo apt upgrade -y
sudo systemctl restart apache2
```

## 10. Performance Optimization

### 10.1 Enable HTTP/2

```bash
sudo a2enmod http2
```

Add to VirtualHost:
```apache
Protocols h2 http/1.1
```

### 10.2 Enable PHP OpCache

```bash
sudo apt install -y php-opcache
```

Edit `/etc/php/8.1/apache2/conf.d/10-opcache.ini`:
```ini
opcache.enable=1
opcache.memory_consumption=128
opcache.max_accelerated_files=10000
opcache.revalidate_freq=60
```

### 10.3 Tune Apache MPM

```bash
sudo nano /etc/apache2/mods-available/mpm_prefork.conf
```

Adjust based on server resources:
```apache
<IfModule mpm_prefork_module>
    StartServers             5
    MinSpareServers          5
    MaxSpareServers          10
    MaxRequestWorkers        150
    MaxConnectionsPerChild   3000
</IfModule>
```

## 11. Backup Strategy

### 11.1 Automated Backups

Create `/usr/local/bin/backup-intcoin-website.sh`:
```bash
#!/bin/bash
BACKUP_DIR="/var/backups/intcoin-website"
DATE=$(date +%Y%m%d_%H%M%S)

mkdir -p $BACKUP_DIR

# Backup website files
tar -czf $BACKUP_DIR/website-$DATE.tar.gz /var/www/html

# Keep only last 7 days
find $BACKUP_DIR -name "website-*.tar.gz" -mtime +7 -delete
```

Make executable and add to cron:
```bash
sudo chmod +x /usr/local/bin/backup-intcoin-website.sh
sudo crontab -e
# Add: 0 2 * * * /usr/local/bin/backup-intcoin-website.sh
```

## 12. Troubleshooting

### Issue: 403 Forbidden on webhooks

**Solution:** Check GitHub IP ranges in `.htaccess` and Apache config. GitHub IPs change occasionally - update from https://api.github.com/meta

### Issue: SSL certificate errors

**Solution:**
```bash
# Check certificate validity
openssl x509 -enddate -noout -in /etc/ssl/certs/international-coin.org_ssl_certificate.cer

# Verify certificate chain
openssl verify -CAfile /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/international-coin.org_ssl_certificate.cer

# Restart Apache after fixing
sudo systemctl restart apache2
```

### Issue: Webhook not triggering

**Check:**
1. GitHub webhook delivery history (Settings → Webhooks → Recent Deliveries)
2. Webhook logs: `sudo cat /var/www/html/logs/webhook-*.log`
3. PHP error log: `sudo cat /var/www/html/logs/php-errors.log`
4. Apache error log: `sudo cat /var/log/apache2/intcoin-error.log`

### Issue: Permission denied errors

**Solution:**
```bash
sudo chown -R www-data:www-data /var/www/html
sudo find /var/www/html -type d -exec chmod 755 {} \;
sudo find /var/www/html -type f -exec chmod 644 {} \;
```

### Issue: Faucet API returns 503 or connection refused

**Solution:**
```bash
# Check if faucet service is running
sudo systemctl status intcoin-faucet

# Check if testnet node is running
sudo systemctl status intcoind-testnet

# Check faucet logs
journalctl -u intcoin-faucet -f

# Verify proxy module is enabled
apache2ctl -M | grep proxy

# If proxy not enabled:
sudo a2enmod proxy proxy_http
sudo systemctl restart apache2
```

### Issue: Faucet says "Wallet not initialized"

**Solution:**
The faucet wallet needs to be funded. Check wallet balance:
```bash
intcoin-cli -conf=/home/intcoin/.intcoin-testnet/intcoin.conf -testnet getbalance
```

## 13. DNS Configuration

Point your domain to the server:

```
Type    Name                        Value
A       international-coin.org      YOUR_SERVER_IP
A       www.international-coin.org  YOUR_SERVER_IP
```

Wait for DNS propagation (up to 48 hours).

## 14. Security Checklist

- [ ] SSL certificate installed (Sectigo) and expiration tracked
- [ ] HSTS header enabled (check with securityheaders.com)
- [ ] Webhook secret configured (strong 64-character hex string)
- [ ] GitHub webhook IPs restricted in Apache and .htaccess
- [ ] PHP dangerous functions disabled
- [ ] Directory listing disabled
- [ ] Firewall configured (only ports 80, 443, 22, 8080 open)
- [ ] Regular backups configured
- [ ] Log rotation configured
- [ ] ModSecurity enabled (optional but recommended)
- [ ] Server tokens disabled
- [ ] File permissions correct (755 for directories, 644 for files)
- [ ] Faucet service running and rate limits configured
- [ ] Faucet wallet funded with testnet coins
- [ ] Apache proxy modules enabled (proxy, proxy_http)

## 15. Post-Deployment Testing

```bash
# Test HTTPS redirect
curl -I http://international-coin.org
# Should show: HTTP/1.1 301 Moved Permanently

# Test HTTPS
curl -I https://international-coin.org
# Should show: HTTP/2 200

# Test security headers
curl -I https://international-coin.org
# Should show HSTS, X-Frame-Options, CSP, etc.

# Test webhooks
# Make a test commit to ROADMAP.md, check webhook logs

# Test faucet API
curl https://international-coin.org/api/faucet/stats
# Should return JSON with faucet statistics

# Test faucet request (with valid testnet address)
curl -X POST https://international-coin.org/api/faucet/request \
  -d "address=intc1qxy2kgdygjrsqtzq2n0yrf2493p83kkfjhx0wlh"
# Should return success or rate limit error
```

## Support

For issues or questions:
- GitHub Issues: https://github.com/INT-devs/intcoin/issues
- Discord: https://discord.gg/Y7dX4Ps2Ha
- Email: admin@international-coin.org

---

**Last Updated:** January 2026
**INTcoin Core Development Team**
