// Copyright (c) 2025 INTcoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "intcoin/intcoin.h"
#include "intcoin/network.h"
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <fstream>

using namespace intcoin;

// Global pool server
static MiningPoolServer* g_pool_server = nullptr;

// Signal handler
void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", stopping pool server...\n";
    if (g_pool_server) {
        g_pool_server->Stop();
    }
}

void print_banner() {
    std::cout << "========================================\n";
    std::cout << "INTcoin Mining Pool Server v" << INTCOIN_VERSION_MAJOR << "."
              << INTCOIN_VERSION_MINOR << "." << INTCOIN_VERSION_PATCH << "\n";
    std::cout << "Post-Quantum Cryptocurrency Pool\n";
    std::cout << "Stratum v1 Protocol with SSL/TLS\n";
    std::cout << "========================================\n\n";
}

void print_usage() {
    std::cout << "Usage: intcoin-pool-server [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help                     Show this help message\n";
    std::cout << "  -v, --version                  Show version information\n";
    std::cout << "  -c, --config=<file>            Configuration file path\n";
    std::cout << "  --testnet                      Run on testnet\n";
    std::cout << "\n";
    std::cout << "Stratum Server:\n";
    std::cout << "  --stratum-port=<port>          Stratum server port (default: 3333)\n";
    std::cout << "  --stratum-host=<host>          Stratum bind address (default: 0.0.0.0)\n";
    std::cout << "  --stratum-ssl                  Enable SSL/TLS for Stratum\n";
    std::cout << "  --ssl-cert=<file>              SSL certificate file (PEM format)\n";
    std::cout << "  --ssl-key=<file>               SSL private key file (PEM format)\n";
    std::cout << "  --ssl-port=<port>              SSL Stratum port (default: 3334)\n";
    std::cout << "\n";
    std::cout << "HTTP API:\n";
    std::cout << "  --http-port=<port>             HTTP API port (default: 8080)\n";
    std::cout << "  --http-host=<host>             HTTP bind address (default: 0.0.0.0)\n";
    std::cout << "\n";
    std::cout << "Pool Configuration:\n";
    std::cout << "  --pool-address=<addr>          Pool's payout address (required)\n";
    std::cout << "  --payout-threshold=<amount>    Minimum payout in ints (default: 1000000000)\n";
    std::cout << "  --pool-fee=<percent>           Pool fee percentage (default: 1.0)\n";
    std::cout << "  --payout-method=<method>       PPLNS, PPS, or PROP (default: PPLNS)\n";
    std::cout << "  --vardiff-min=<diff>           Minimum difficulty (default: 1000)\n";
    std::cout << "  --vardiff-max=<diff>           Maximum difficulty (default: 100000)\n";
    std::cout << "  --vardiff-target=<sec>         Target time per share (default: 15)\n";
    std::cout << "\n";
    std::cout << "Database:\n";
    std::cout << "  --db-path=<path>               Database directory (default: ./pooldb)\n";
    std::cout << "\n";
    std::cout << "Daemon Connection:\n";
    std::cout << "  --daemon-host=<host>           intcoind RPC host (default: 127.0.0.1)\n";
    std::cout << "  --daemon-port=<port>           intcoind RPC port (default: " << network::MAINNET_RPC_PORT << ")\n";
    std::cout << "  --rpc-user=<user>              RPC username\n";
    std::cout << "  --rpc-password=<pass>          RPC password\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  # Basic pool server (no SSL)\n";
    std::cout << "  intcoin-pool-server --pool-address=int1qxyz... --rpc-user=user --rpc-password=pass\n";
    std::cout << "\n";
    std::cout << "  # Pool with SSL/TLS\n";
    std::cout << "  intcoin-pool-server --pool-address=int1qxyz... --stratum-ssl \\\n";
    std::cout << "    --ssl-cert=/etc/intcoin/cert.pem --ssl-key=/etc/intcoin/key.pem\n";
    std::cout << "\n";
    std::cout << "  # Using configuration file\n";
    std::cout << "  intcoin-pool-server --config=pool.conf\n";
    std::cout << "\n";
}

struct ServerConfig {
    // Stratum server
    std::string stratum_host = "0.0.0.0";
    uint16_t stratum_port = 3333;
    bool use_ssl = false;
    std::string ssl_cert;
    std::string ssl_key;
    uint16_t ssl_port = 3334;

    // HTTP API
    std::string http_host = "0.0.0.0";
    uint16_t http_port = 8080;

    // Pool settings
    std::string pool_address;
    uint64_t payout_threshold = 1000000000;  // 10 INT
    double pool_fee = 1.0;  // 1%
    std::string payout_method = "PPLNS";

    // VarDiff
    uint64_t vardiff_min = 1000;
    uint64_t vardiff_max = 100000;
    uint32_t vardiff_target = 15;  // seconds

    // Database
    std::string db_path = "./pooldb";

    // Daemon connection
    std::string daemon_host = "127.0.0.1";
    uint16_t daemon_port = network::MAINNET_RPC_PORT;
    std::string rpc_user;
    std::string rpc_password;

    // Network
    bool testnet = false;
};

bool load_config_file(const std::string& path, ServerConfig& config) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open config file: " << path << "\n";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Parse key=value
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        // Set configuration values
        if (key == "stratum-port") config.stratum_port = std::stoi(value);
        else if (key == "stratum-host") config.stratum_host = value;
        else if (key == "stratum-ssl") config.use_ssl = (value == "true" || value == "1");
        else if (key == "ssl-cert") config.ssl_cert = value;
        else if (key == "ssl-key") config.ssl_key = value;
        else if (key == "ssl-port") config.ssl_port = std::stoi(value);
        else if (key == "http-port") config.http_port = std::stoi(value);
        else if (key == "http-host") config.http_host = value;
        else if (key == "pool-address") config.pool_address = value;
        else if (key == "payout-threshold") config.payout_threshold = std::stoull(value);
        else if (key == "pool-fee") config.pool_fee = std::stod(value);
        else if (key == "payout-method") config.payout_method = value;
        else if (key == "vardiff-min") config.vardiff_min = std::stoull(value);
        else if (key == "vardiff-max") config.vardiff_max = std::stoull(value);
        else if (key == "vardiff-target") config.vardiff_target = std::stoul(value);
        else if (key == "db-path") config.db_path = value;
        else if (key == "daemon-host") config.daemon_host = value;
        else if (key == "daemon-port") config.daemon_port = std::stoi(value);
        else if (key == "rpc-user") config.rpc_user = value;
        else if (key == "rpc-password") config.rpc_password = value;
        else if (key == "testnet") config.testnet = (value == "true" || value == "1");
    }

    return true;
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    ServerConfig config;
    std::string config_file;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_banner();
            print_usage();
            return 0;
        }
        else if (arg == "-v" || arg == "--version") {
            std::cout << "INTcoin Mining Pool Server v" << INTCOIN_VERSION << "\n";
            return 0;
        }
        else if (arg.find("-c=") == 0 || arg.find("--config=") == 0) {
            size_t eq_pos = arg.find('=');
            config_file = arg.substr(eq_pos + 1);
        }
        else if (arg == "--testnet") {
            config.testnet = true;
        }
        else if (arg.find("--stratum-port=") == 0) {
            config.stratum_port = std::stoi(arg.substr(15));
        }
        else if (arg.find("--stratum-host=") == 0) {
            config.stratum_host = arg.substr(15);
        }
        else if (arg == "--stratum-ssl") {
            config.use_ssl = true;
        }
        else if (arg.find("--ssl-cert=") == 0) {
            config.ssl_cert = arg.substr(11);
        }
        else if (arg.find("--ssl-key=") == 0) {
            config.ssl_key = arg.substr(10);
        }
        else if (arg.find("--ssl-port=") == 0) {
            config.ssl_port = std::stoi(arg.substr(11));
        }
        else if (arg.find("--http-port=") == 0) {
            config.http_port = std::stoi(arg.substr(12));
        }
        else if (arg.find("--http-host=") == 0) {
            config.http_host = arg.substr(12);
        }
        else if (arg.find("--pool-address=") == 0) {
            config.pool_address = arg.substr(15);
        }
        else if (arg.find("--payout-threshold=") == 0) {
            config.payout_threshold = std::stoull(arg.substr(19));
        }
        else if (arg.find("--pool-fee=") == 0) {
            config.pool_fee = std::stod(arg.substr(11));
        }
        else if (arg.find("--payout-method=") == 0) {
            config.payout_method = arg.substr(16);
        }
        else if (arg.find("--vardiff-min=") == 0) {
            config.vardiff_min = std::stoull(arg.substr(14));
        }
        else if (arg.find("--vardiff-max=") == 0) {
            config.vardiff_max = std::stoull(arg.substr(14));
        }
        else if (arg.find("--vardiff-target=") == 0) {
            config.vardiff_target = std::stoul(arg.substr(17));
        }
        else if (arg.find("--db-path=") == 0) {
            config.db_path = arg.substr(10);
        }
        else if (arg.find("--daemon-host=") == 0) {
            config.daemon_host = arg.substr(14);
        }
        else if (arg.find("--daemon-port=") == 0) {
            config.daemon_port = std::stoi(arg.substr(14));
        }
        else if (arg.find("--rpc-user=") == 0) {
            config.rpc_user = arg.substr(11);
        }
        else if (arg.find("--rpc-password=") == 0) {
            config.rpc_password = arg.substr(15);
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::cerr << "Use -h or --help for usage information.\n";
            return 1;
        }
    }

    // Load config file if specified
    if (!config_file.empty()) {
        if (!load_config_file(config_file, config)) {
            return 1;
        }
    }

    // Validate configuration
    if (config.pool_address.empty()) {
        std::cerr << "Error: Pool address is required (--pool-address)\n";
        std::cerr << "Use -h or --help for usage information.\n";
        return 1;
    }

    if (config.rpc_user.empty() || config.rpc_password.empty()) {
        std::cerr << "Error: RPC credentials are required (--rpc-user, --rpc-password)\n";
        std::cerr << "Use -h or --help for usage information.\n";
        return 1;
    }

    if (config.use_ssl && (config.ssl_cert.empty() || config.ssl_key.empty())) {
        std::cerr << "Error: SSL enabled but certificate or key not specified\n";
        std::cerr << "Use --ssl-cert and --ssl-key to provide SSL credentials.\n";
        return 1;
    }

    // Print banner
    print_banner();

    // Register signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        // Initialize blockchain connection
        std::cout << "Connecting to intcoind at " << config.daemon_host << ":" << config.daemon_port << "...\n";

        // TODO: Create blockchain RPC client
        // For now, this is a placeholder
        // Blockchain blockchain(config.daemon_host, config.daemon_port, config.rpc_user, config.rpc_password);

        // Initialize mining pool server
        std::cout << "Initializing mining pool server...\n";
        std::cout << "  Pool address: " << config.pool_address << "\n";
        std::cout << "  Payout method: " << config.payout_method << "\n";
        std::cout << "  Pool fee: " << config.pool_fee << "%\n";
        std::cout << "  Payout threshold: " << config.payout_threshold << " ints\n";
        std::cout << "\n";

        std::cout << "Stratum Server:\n";
        std::cout << "  Listening on " << config.stratum_host << ":" << config.stratum_port << "\n";
        if (config.use_ssl) {
            std::cout << "  SSL/TLS enabled on port " << config.ssl_port << "\n";
            std::cout << "  Certificate: " << config.ssl_cert << "\n";
        }
        std::cout << "\n";

        std::cout << "HTTP API:\n";
        std::cout << "  Listening on " << config.http_host << ":" << config.http_port << "\n";
        std::cout << "\n";

        std::cout << "Variable Difficulty:\n";
        std::cout << "  Min: " << config.vardiff_min << "\n";
        std::cout << "  Max: " << config.vardiff_max << "\n";
        std::cout << "  Target: " << config.vardiff_target << " seconds\n";
        std::cout << "\n";

        // TODO: Create and start pool server
        // For now, this is a placeholder
        // MiningPoolServer pool_server(blockchain, config);
        // g_pool_server = &pool_server;
        //
        // auto result = pool_server.Start();
        // if (!result.IsOk()) {
        //     std::cerr << "Error starting pool server: " << result.GetError() << "\n";
        //     return 1;
        // }

        std::cout << "Pool server started successfully!\n";
        std::cout << "Mining pool is ready to accept connections.\n";
        std::cout << "Press Ctrl+C to stop.\n\n";

        // Keep running until signal received
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // TODO: Print periodic statistics
            // pool_server.PrintStats();
        }

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
