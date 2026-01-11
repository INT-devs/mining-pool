#!/bin/bash
#
# INTcoin Pool Dashboard - Server Setup Script
#
# This script sets up the Apache server for hosting the INTcoin pool dashboard
# with GitHub webhook support for automatic deployments.
#
# Usage: sudo ./setup.sh
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}INTcoin Pool Dashboard Setup${NC}"
echo -e "${GREEN}========================================${NC}"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Please run as root (sudo ./setup.sh)${NC}"
    exit 1
fi

# Detect OS
if [ -f /etc/debian_version ]; then
    OS="debian"
    PKG_MANAGER="apt"
elif [ -f /etc/redhat-release ]; then
    OS="redhat"
    PKG_MANAGER="dnf"
elif [ -f /etc/freebsd-update.conf ]; then
    OS="freebsd"
    PKG_MANAGER="pkg"
else
    echo -e "${RED}Unsupported operating system${NC}"
    exit 1
fi

echo -e "${YELLOW}Detected OS: $OS${NC}"

# Install dependencies
echo -e "${YELLOW}Installing dependencies...${NC}"

case $OS in
    debian)
        apt update
        apt install -y apache2 php php-fpm php-json git certbot python3-certbot-apache
        ;;
    redhat)
        dnf install -y httpd php php-fpm php-json git certbot python3-certbot-apache
        ;;
    freebsd)
        pkg install -y apache24 php82 php82-json git py39-certbot py39-certbot-apache
        ;;
esac

# Create web directory structure
echo -e "${YELLOW}Creating directory structure...${NC}"

mkdir -p /var/www/html/logs
mkdir -p /var/www/html/api

# Set ownership
case $OS in
    debian)
        chown -R www-data:www-data /var/www/html
        ;;
    redhat)
        chown -R apache:apache /var/www/html
        ;;
    freebsd)
        chown -R www:www /var/www/html
        ;;
esac

# Set permissions
chmod 755 /var/www/html
chmod 755 /var/www/html/logs

# Copy webhook script
echo -e "${YELLOW}Installing webhook handler...${NC}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cp "$SCRIPT_DIR/webhook.php" /var/www/html/webhook.php

# Copy Apache configuration
echo -e "${YELLOW}Configuring Apache...${NC}"

case $OS in
    debian)
        cp "$SCRIPT_DIR/intcoin-pool.conf" /etc/apache2/sites-available/
        a2enmod rewrite ssl headers proxy_fcgi
        a2ensite intcoin-pool.conf
        a2dissite 000-default.conf 2>/dev/null || true
        ;;
    redhat)
        cp "$SCRIPT_DIR/intcoin-pool.conf" /etc/httpd/conf.d/
        ;;
    freebsd)
        cp "$SCRIPT_DIR/intcoin-pool.conf" /usr/local/etc/apache24/Includes/
        ;;
esac

# Initialize git repository in web root (for deployments)
echo -e "${YELLOW}Initializing git repository...${NC}"

cd /var/www/html
if [ ! -d ".git" ]; then
    git init
    git remote add origin https://github.com/INT-devs/intcoin.git
    git fetch origin main
    git checkout -f main
fi

# Copy dashboard files
if [ -d "web/pool-dashboard" ]; then
    cp -r web/pool-dashboard/* /var/www/html/
fi

# Create test-results.json if it doesn't exist
if [ ! -f "/var/www/html/test-results.json" ]; then
    cat > /var/www/html/test-results.json << 'EOF'
{
  "total": 32,
  "passed": 31,
  "failed": 1,
  "skipped": 0,
  "lastUpdated": "2026-01-11T10:30:00Z"
}
EOF
fi

# Set final permissions
case $OS in
    debian)
        chown -R www-data:www-data /var/www/html
        ;;
    redhat)
        chown -R apache:apache /var/www/html
        ;;
    freebsd)
        chown -R www:www /var/www/html
        ;;
esac

# Enable and start services
echo -e "${YELLOW}Starting services...${NC}"

case $OS in
    debian)
        systemctl enable apache2
        systemctl enable php8.2-fpm 2>/dev/null || systemctl enable php-fpm
        systemctl restart php8.2-fpm 2>/dev/null || systemctl restart php-fpm
        systemctl restart apache2
        ;;
    redhat)
        systemctl enable httpd php-fpm
        systemctl restart php-fpm httpd
        ;;
    freebsd)
        sysrc apache24_enable=YES
        sysrc php_fpm_enable=YES
        service php-fpm restart
        service apache24 restart
        ;;
esac

# SSL Certificate setup reminder
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Setup Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "${YELLOW}Next steps:${NC}"
echo "1. Update the domain in /etc/apache2/sites-available/intcoin-pool.conf"
echo "2. Obtain SSL certificate:"
echo "   certbot --apache -d pool.international-coin.org"
echo ""
echo "3. Set up GitHub webhook:"
echo "   - Go to: https://github.com/INT-devs/intcoin/settings/hooks"
echo "   - Payload URL: https://pool.international-coin.org/webhook.php"
echo "   - Content type: application/json"
echo "   - Secret: (set a strong secret)"
echo "   - Events: Push, Releases, Workflow runs"
echo ""
echo "4. Update webhook secret in /var/www/html/webhook.php:"
echo "   define('WEBHOOK_SECRET', 'your-secret-here');"
echo ""
echo "5. Test webhook with:"
echo "   tail -f /var/www/html/logs/webhook-$(date +%Y-%m-%d).log"
echo ""
echo -e "${GREEN}Dashboard URL: https://pool.international-coin.org${NC}"
