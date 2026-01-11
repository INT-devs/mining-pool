/*
 * Copyright (c) 2025 INTcoin Team (Neil Adamson)
 * MIT License
 * Mining Pool HTTP API Server
 */

#include "intcoin/pool.h"
#include "intcoin/rpc.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace intcoin {
namespace pool {

// ============================================================================
// HTTP Request/Response Structures
// ============================================================================

struct HttpRequest {
    std::string method;         // GET, POST, etc.
    std::string path;           // /api/pool/stats
    std::string query_string;   // limit=10
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::map<std::string, std::string> headers;
    std::string body;

    std::string ToString() const {
        std::ostringstream oss;

        // Status line
        oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";

        // Headers
        for (const auto& [key, value] : headers) {
            oss << key << ": " << value << "\r\n";
        }

        // Content-Length
        oss << "Content-Length: " << body.size() << "\r\n";

        // End of headers
        oss << "\r\n";

        // Body
        oss << body;

        return oss.str();
    }
};

// ============================================================================
// HTTP API Server for Pool Dashboard
// ============================================================================

/**
 * HTTP API server for mining pool statistics
 * Provides REST endpoints for pool dashboard
 */
class HttpApiServer {
public:
    HttpApiServer(uint16_t port, MiningPoolServer& pool)
        : port_(port)
        , pool_(pool)
        , is_running_(false)
        , server_socket_(-1)
    {}

    ~HttpApiServer() {
        Stop();
    }

    Result<void> Start() {
        if (is_running_) {
            return Result<void>::Error("HTTP API server already running");
        }

        // Create socket
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ < 0) {
            return Result<void>::Error("Failed to create socket");
        }

        // Set socket options
        int opt = 1;
        if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(server_socket_);
            return Result<void>::Error("Failed to set socket options");
        }

        // Bind socket
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);

        if (bind(server_socket_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            close(server_socket_);
            return Result<void>::Error("Failed to bind socket to port " + std::to_string(port_));
        }

        // Listen for connections
        if (listen(server_socket_, 10) < 0) {
            close(server_socket_);
            return Result<void>::Error("Failed to listen on socket");
        }

        is_running_ = true;

        // Start server thread
        server_thread_ = std::thread([this]() { RunServer(); });

        return Result<void>::Ok();
    }

    void Stop() {
        if (!is_running_) return;

        is_running_ = false;

        // Close server socket to unblock accept()
        if (server_socket_ >= 0) {
            shutdown(server_socket_, SHUT_RDWR);
            close(server_socket_);
            server_socket_ = -1;
        }

        // Wait for server thread to finish
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    bool IsRunning() const {
        return is_running_;
    }

private:
    void RunServer() {
        while (is_running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                if (!is_running_) break;  // Server stopped
                continue;  // Accept failed, try again
            }

            // Handle request in separate thread (simple approach for now)
            std::thread([this, client_socket]() {
                HandleClient(client_socket);
            }).detach();
        }
    }

    void HandleClient(int client_socket) {
        // Read request
        char buffer[4096];
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read <= 0) {
            close(client_socket);
            return;
        }

        buffer[bytes_read] = '\0';

        // Parse HTTP request
        HttpRequest request = ParseRequest(std::string(buffer));

        // Generate response
        HttpResponse response = HandleRequest(request);

        // Send response
        std::string response_str = response.ToString();
        send(client_socket, response_str.c_str(), response_str.size(), 0);

        // Close connection
        close(client_socket);
    }

    HttpRequest ParseRequest(const std::string& raw) {
        HttpRequest request;
        std::istringstream stream(raw);
        std::string line;

        // Parse request line (GET /path HTTP/1.1)
        if (std::getline(stream, line)) {
            std::istringstream line_stream(line);
            line_stream >> request.method >> request.path;

            // Extract query string if present
            size_t query_pos = request.path.find('?');
            if (query_pos != std::string::npos) {
                request.query_string = request.path.substr(query_pos + 1);
                request.path = request.path.substr(0, query_pos);
            }
        }

        // Parse headers
        while (std::getline(stream, line) && line != "\r") {
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 2);  // Skip ': '

                // Remove \r if present
                if (!value.empty() && value.back() == '\r') {
                    value.pop_back();
                }

                request.headers[key] = value;
            }
        }

        // Read body (if any)
        std::string body;
        while (std::getline(stream, line)) {
            body += line;
        }
        request.body = body;

        return request;
    }

    HttpResponse HandleRequest(const HttpRequest& request) {
        HttpResponse response;
        response.headers["Content-Type"] = "application/json";
        response.headers["Access-Control-Allow-Origin"] = "*";  // CORS support
        response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
        response.headers["Access-Control-Allow-Headers"] = "Content-Type";

        // Handle OPTIONS request (CORS preflight)
        if (request.method == "OPTIONS") {
            response.status_code = 204;
            response.status_text = "No Content";
            return response;
        }

        // Route to appropriate endpoint
        if (request.method == "GET") {
            if (request.path == "/api/pool/stats") {
                auto result = GetPoolStats();
                response.body = result.ToJSONString();
            }
            else if (request.path == "/api/pool/blocks") {
                int limit = GetQueryParam(request.query_string, "limit", 10);
                auto result = GetRecentBlocks(limit);
                response.body = result.ToJSONString();
            }
            else if (request.path == "/api/pool/payments") {
                int limit = GetQueryParam(request.query_string, "limit", 20);
                auto result = GetRecentPayments(limit);
                response.body = result.ToJSONString();
            }
            else if (request.path == "/api/pool/topminers") {
                int limit = GetQueryParam(request.query_string, "limit", 10);
                auto result = GetTopMiners(limit);
                response.body = result.ToJSONString();
            }
            else if (request.path == "/api/pool/worker") {
                std::string address = GetQueryParam(request.query_string, "address", "");
                auto result = GetWorkerStats(address);
                response.body = result.ToJSONString();
            }
            else if (request.path == "/" || request.path == "/health") {
                // Health check endpoint
                std::map<std::string, rpc::JSONValue> health;
                health["status"] = rpc::JSONValue("ok");
                health["service"] = rpc::JSONValue("intcoin-pool-api");
                response.body = rpc::JSONValue(health).ToJSONString();
            }
            else {
                // 404 Not Found
                response.status_code = 404;
                response.status_text = "Not Found";
                std::map<std::string, rpc::JSONValue> error;
                error["error"] = rpc::JSONValue("Endpoint not found");
                response.body = rpc::JSONValue(error).ToJSONString();
            }
        }
        else {
            // 405 Method Not Allowed
            response.status_code = 405;
            response.status_text = "Method Not Allowed";
            std::map<std::string, rpc::JSONValue> error;
            error["error"] = rpc::JSONValue("Method not allowed");
            response.body = rpc::JSONValue(error).ToJSONString();
        }

        return response;
    }

    int GetQueryParam(const std::string& query_string, const std::string& param, int default_value) {
        std::string value = GetQueryParam(query_string, param, "");
        if (value.empty()) return default_value;
        try {
            return std::stoi(value);
        } catch (...) {
            return default_value;
        }
    }

    std::string GetQueryParam(const std::string& query_string, const std::string& param, const std::string& default_value) {
        // Parse query string: param1=value1&param2=value2
        std::istringstream stream(query_string);
        std::string pair;

        while (std::getline(stream, pair, '&')) {
            size_t eq_pos = pair.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = pair.substr(0, eq_pos);
                std::string value = pair.substr(eq_pos + 1);

                if (key == param) {
                    return value;
                }
            }
        }

        return default_value;
    }

    // ========================================================================
    // API Endpoints (matching pool dashboard requirements)
    // ========================================================================

    /**
     * GET /api/pool/stats
     * Returns pool statistics
     */
    rpc::JSONValue GetPoolStats() {
        auto stats = pool_.GetStatistics();

        std::map<std::string, rpc::JSONValue> response;
        response["hashrate"] = rpc::JSONValue(static_cast<int64_t>(stats.pool_hashrate));
        response["difficulty"] = rpc::JSONValue(static_cast<int64_t>(stats.network_difficulty));
        response["miners"] = rpc::JSONValue(static_cast<int64_t>(stats.active_miners));
        response["blocks_found"] = rpc::JSONValue(static_cast<int64_t>(stats.blocks_found));
        response["total_shares"] = rpc::JSONValue(static_cast<int64_t>(stats.total_shares));
        response["valid_shares_24h"] = rpc::JSONValue(static_cast<int64_t>(stats.shares_last_day));

        return rpc::JSONValue(response);
    }

    /**
     * GET /api/pool/blocks?limit=10
     * Returns recent blocks found by pool
     */
    rpc::JSONValue GetRecentBlocks(int limit) {
        auto rounds = pool_.GetRoundHistory(limit);

        std::vector<rpc::JSONValue> blocks;

        for (const auto& round : rounds) {
            if (!round.is_complete) continue;

            std::map<std::string, rpc::JSONValue> block;
            block["height"] = rpc::JSONValue(static_cast<int64_t>(round.block_height));
            block["hash"] = rpc::JSONValue(ToHex(round.block_hash));
            block["timestamp"] = rpc::JSONValue(static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    round.ended_at.time_since_epoch()).count()));

            // Find block finder (miner with most shares in this round)
            std::string finder_address = "pool";
            if (!round.miner_shares.empty()) {
                uint64_t max_shares = 0;
                uint64_t finder_miner_id = 0;

                for (const auto& [miner_id, share_count] : round.miner_shares) {
                    if (share_count > max_shares) {
                        max_shares = share_count;
                        finder_miner_id = miner_id;
                    }
                }

                // Get finder's payout address
                if (finder_miner_id > 0) {
                    auto miner_opt = pool_.GetMiner(finder_miner_id);
                    if (miner_opt.has_value()) {
                        finder_address = miner_opt->payout_address;
                    }
                }
            }
            block["finder"] = rpc::JSONValue(finder_address);
            block["reward"] = rpc::JSONValue(static_cast<int64_t>(round.block_reward));

            // Check block status from blockchain
            // Get current network height from pool statistics
            std::string status = "pending";
            auto pool_stats = pool_.GetStatistics();
            uint64_t current_height = pool_stats.network_height;

            if (round.block_height <= current_height) {
                uint64_t confirmations = current_height - round.block_height + 1;

                if (confirmations >= 100) {
                    status = "confirmed";
                } else if (confirmations >= 1) {
                    status = "confirming";
                } else {
                    status = "pending";
                }

                // Note: To properly detect orphaned blocks, would need direct blockchain access
                // For now, assume blocks with sufficient confirmations are valid
                // In full implementation, would check if block hash at this height matches
            }
            block["status"] = rpc::JSONValue(status);

            blocks.push_back(rpc::JSONValue(block));
        }

        return rpc::JSONValue(blocks);
    }

    /**
     * GET /api/pool/payments?limit=20
     * Returns recent payments to miners
     */
    rpc::JSONValue GetRecentPayments(int limit) {
        auto payments = pool_.GetPaymentHistory(limit);

        std::vector<rpc::JSONValue> payment_array;

        for (const auto& payment : payments) {
            std::map<std::string, rpc::JSONValue> payment_obj;
            payment_obj["payment_id"] = rpc::JSONValue(static_cast<int64_t>(payment.payment_id));
            payment_obj["miner_id"] = rpc::JSONValue(static_cast<int64_t>(payment.miner_id));
            payment_obj["address"] = rpc::JSONValue(payment.payout_address);
            payment_obj["amount"] = rpc::JSONValue(static_cast<int64_t>(payment.amount));
            payment_obj["tx_hash"] = rpc::JSONValue(ToHex(payment.tx_hash));
            payment_obj["timestamp"] = rpc::JSONValue(static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    payment.created_at.time_since_epoch()).count()));
            payment_obj["is_confirmed"] = rpc::JSONValue(payment.is_confirmed);
            payment_obj["status"] = rpc::JSONValue(payment.status);

            payment_array.push_back(rpc::JSONValue(payment_obj));
        }

        return rpc::JSONValue(payment_array);
    }

    /**
     * GET /api/pool/topminers?limit=10
     * Returns top miners by hashrate (24h)
     */
    rpc::JSONValue GetTopMiners(int limit) {
        auto miners = pool_.GetAllMiners();

        // Sort by hashrate (descending)
        std::sort(miners.begin(), miners.end(),
            [this](const intcoin::Miner& a, const intcoin::Miner& b) {
                return pool_.CalculateMinerHashrate(a.miner_id) >
                       pool_.CalculateMinerHashrate(b.miner_id);
            });

        std::vector<rpc::JSONValue> top_miners;

        for (size_t i = 0; i < std::min(static_cast<size_t>(limit), miners.size()); i++) {
            const auto& miner = miners[i];

            std::map<std::string, rpc::JSONValue> miner_obj;
            miner_obj["rank"] = rpc::JSONValue(static_cast<int64_t>(i + 1));
            miner_obj["address"] = rpc::JSONValue(miner.payout_address);
            miner_obj["hashrate"] = rpc::JSONValue(static_cast<int64_t>(pool_.CalculateMinerHashrate(miner.miner_id)));
            miner_obj["shares"] = rpc::JSONValue(static_cast<int64_t>(miner.total_shares_accepted));

            top_miners.push_back(rpc::JSONValue(miner_obj));
        }

        return rpc::JSONValue(top_miners);
    }

    /**
     * GET /api/pool/worker?address=intc1...
     * Returns statistics for specific worker/miner
     */
    rpc::JSONValue GetWorkerStats(const std::string& address) {
        // Find miner by payout address
        auto miners = pool_.GetAllMiners();

        for (const auto& miner : miners) {
            if (miner.payout_address == address) {
                std::map<std::string, rpc::JSONValue> stats;
                stats["address"] = rpc::JSONValue(miner.payout_address);
                stats["hashrate"] = rpc::JSONValue(static_cast<int64_t>(pool_.CalculateMinerHashrate(miner.miner_id)));
                stats["shares"] = rpc::JSONValue(static_cast<int64_t>(miner.total_shares_accepted));
                stats["balance"] = rpc::JSONValue(static_cast<int64_t>(miner.unpaid_balance));
                stats["total_paid"] = rpc::JSONValue(static_cast<int64_t>(miner.paid_balance));

                return rpc::JSONValue(stats);
            }
        }

        // Worker not found
        std::map<std::string, rpc::JSONValue> error;
        error["error"] = rpc::JSONValue("Worker not found");
        return rpc::JSONValue(error);
    }

private:
    uint16_t port_;
    MiningPoolServer& pool_;
    std::atomic<bool> is_running_;
    int server_socket_;
    std::thread server_thread_;
};

// Factory and wrapper functions for external use
HttpApiServer* CreateHttpApiServer(uint16_t port, MiningPoolServer& pool) {
    return new HttpApiServer(port, pool);
}

void DestroyHttpApiServer(HttpApiServer* server) {
    if (server) {
        server->Stop();
        delete server;
    }
}

Result<void> HttpApiServerStart(HttpApiServer* server) {
    if (server) {
        return server->Start();
    }
    return Result<void>::Error("Null server pointer");
}

} // namespace pool
} // namespace intcoin
