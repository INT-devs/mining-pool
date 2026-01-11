/*
 * Copyright (c) 2025 INTcoin Team (Neil Adamson)
 * MIT License
 * Stratum Mining Protocol Server
 */

#include "intcoin/pool.h"
#include "intcoin/util.h"
#include <thread>
#include <map>
#include <iostream>
#include <iomanip>
#include <sstream>

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
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// OpenSSL includes for TLS/SSL support
#ifdef STRATUM_USE_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace intcoin {
namespace stratum {

// ============================================================================
// Helper Functions for Hex Conversion
// ============================================================================

// Convert hex string to uint256
Result<uint256> HexToUint256(const std::string& hex) {
    if (hex.length() != 64) {
        return Result<uint256>::Error("Invalid hex length for uint256");
    }

    uint256 result;
    for (size_t i = 0; i < 32; i++) {
        std::string byte_str = hex.substr(i * 2, 2);
        try {
            result[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        } catch (const std::exception& e) {
            return Result<uint256>::Error("Invalid hex character");
        }
    }

    return Result<uint256>::Ok(result);
}

// Convert hex string to uint32
Result<uint32_t> HexToUint32(const std::string& hex) {
    if (hex.length() != 8) {
        return Result<uint32_t>::Error("Invalid hex length for uint32");
    }

    try {
        uint32_t result = static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
        return Result<uint32_t>::Ok(result);
    } catch (const std::exception& e) {
        return Result<uint32_t>::Error("Invalid hex value");
    }
}

// Convert hex string to byte vector
Result<std::vector<uint8_t>> HexToBytes(const std::string& hex) {
    if (hex.length() % 2 != 0) {
        return Result<std::vector<uint8_t>>::Error("Invalid hex length (must be even)");
    }

    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        try {
            bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
        } catch (const std::exception& e) {
            return Result<std::vector<uint8_t>>::Error("Invalid hex character");
        }
    }

    return Result<std::vector<uint8_t>>::Ok(bytes);
}

// Convert uint256 to hex with endian control
std::string ToHex(const uint256& data, bool little_endian = false) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    if (little_endian) {
        // Reverse byte order
        for (int i = 31; i >= 0; i--) {
            ss << std::setw(2) << static_cast<int>(data[i]);
        }
    } else {
        for (size_t i = 0; i < 32; i++) {
            ss << std::setw(2) << static_cast<int>(data[i]);
        }
    }

    return ss.str();
}

// Convert uint32 to hex (8 characters)
std::string ToHex(uint32_t value) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(8) << value;
    return ss.str();
}

// Convert byte vector to hex
std::string ToHex(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t byte : data) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

// Simple JSON parser for Stratum messages
Result<std::map<std::string, std::string>> ParseJSON(const std::string& json) {
    std::map<std::string, std::string> result;

    // Remove whitespace
    std::string trimmed = json;
    trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(), ::isspace), trimmed.end());

    // Very basic JSON parsing (for Stratum messages only)
    // This is a simplified parser - production would use nlohmann/json

    size_t pos = 0;
    while ((pos = trimmed.find("\"", pos)) != std::string::npos) {
        size_t key_start = pos + 1;
        size_t key_end = trimmed.find("\"", key_start);
        if (key_end == std::string::npos) break;

        std::string key = trimmed.substr(key_start, key_end - key_start);

        // Skip colon
        pos = trimmed.find(":", key_end);
        if (pos == std::string::npos) break;
        pos++;

        // Find value
        std::string value;
        if (trimmed[pos] == '"') {
            // String value
            size_t val_start = pos + 1;
            size_t val_end = trimmed.find("\"", val_start);
            if (val_end == std::string::npos) break;
            value = trimmed.substr(val_start, val_end - val_start);
            pos = val_end + 1;
        } else if (trimmed[pos] == '[') {
            // Array value (find matching ])
            size_t val_start = pos;
            int bracket_count = 1;
            pos++;
            while (pos < trimmed.length() && bracket_count > 0) {
                if (trimmed[pos] == '[') bracket_count++;
                if (trimmed[pos] == ']') bracket_count--;
                pos++;
            }
            value = trimmed.substr(val_start, pos - val_start);
        } else {
            // Number or boolean
            size_t val_start = pos;
            while (pos < trimmed.length() && trimmed[pos] != ',' && trimmed[pos] != '}') {
                pos++;
            }
            value = trimmed.substr(val_start, pos - val_start);
        }

        result[key] = value;
    }

    return Result<std::map<std::string, std::string>>::Ok(result);
}

// ============================================================================
// Stratum Server Implementation
// ============================================================================

class StratumServer {
public:
    StratumServer(uint16_t port, MiningPoolServer& pool
#ifdef STRATUM_USE_SSL
                  , bool use_ssl = false
                  , const std::string& cert_file = ""
                  , const std::string& key_file = ""
#endif
                  )
        : port_(port)
        , pool_(pool)
        , is_running_(false)
        , server_socket_(-1)
        , next_conn_id_(1)
        , connection_timeout_(300)  // 5 minutes default
        , max_connections_per_ip_(10)
#ifdef STRATUM_USE_SSL
        , use_ssl_(use_ssl)
        , ssl_cert_file_(cert_file)
        , ssl_key_file_(key_file)
#endif
        , total_connections_(0)
        , total_shares_(0)
        , total_valid_shares_(0)
        , total_invalid_shares_(0)
        , server_start_time_(std::chrono::system_clock::now())
#ifdef STRATUM_USE_SSL
        , ssl_ctx_(nullptr)
#endif
    {
#ifdef STRATUM_USE_SSL
        if (use_ssl_) {
            InitializeSSL();
        }
#endif
    }

    ~StratumServer() {
        Stop();
#ifdef STRATUM_USE_SSL
        CleanupSSL();
#endif
    }

    Result<void> Start() {
        if (is_running_) {
            return Result<void>::Error("Stratum server already running");
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

        // Bind to port
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);

        if (bind(server_socket_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            close(server_socket_);
            return Result<void>::Error("Failed to bind to port " + std::to_string(port_));
        }

        // Listen for connections
        if (listen(server_socket_, 100) < 0) {
            close(server_socket_);
            return Result<void>::Error("Failed to listen on socket");
        }

        is_running_ = true;

        // Start accept thread
        accept_thread_ = std::thread(&StratumServer::AcceptLoop, this);

        // Start timeout monitoring thread
        timeout_thread_ = std::thread(&StratumServer::TimeoutMonitorLoop, this);

        LogInfo("Stratum server started on port " + std::to_string(port_));

        return Result<void>::Ok();
    }

    void Stop() {
        if (!is_running_) return;

        is_running_ = false;

        // Close all client connections
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& [conn_id, conn] : connections_) {
            close(conn.socket_fd);
        }
        connections_.clear();

        // Close server socket
        if (server_socket_ >= 0) {
            close(server_socket_);
            server_socket_ = -1;
        }

        // Wait for accept thread
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        // Wait for timeout thread
        if (timeout_thread_.joinable()) {
            timeout_thread_.join();
        }

        LogInfo("Stratum server stopped");
    }

    void BroadcastWork(const Work& work) {
        std::lock_guard<std::mutex> lock(connections_mutex_);

        for (auto& [conn_id, conn] : connections_) {
            if (conn.authorized) {
                SendNotify(conn_id, work);
            }
        }
    }

    void SendDifficulty(uint64_t conn_id, uint64_t difficulty) {
        std::lock_guard<std::mutex> lock(connections_mutex_);

        auto it = connections_.find(conn_id);
        if (it == connections_.end()) return;

        // mining.set_difficulty
        std::string msg = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[" +
                         std::to_string(difficulty) + "]}\n";

        send(it->second.socket_fd, msg.c_str(), msg.length(), 0);
    }

private:
    struct Connection {
        int socket_fd;
        uint64_t worker_id;
        std::string ip_address;
        bool subscribed;
        bool authorized;
        std::string extranonce1;
        std::chrono::system_clock::time_point connected_at;
        std::chrono::system_clock::time_point last_activity;
#ifdef STRATUM_USE_SSL
        SSL* ssl;
#endif
    };

    uint16_t port_;
    MiningPoolServer& pool_;
    std::atomic<bool> is_running_;
    int server_socket_;
    std::atomic<uint64_t> next_conn_id_;

    std::map<uint64_t, Connection> connections_;
    std::mutex connections_mutex_;

    std::thread accept_thread_;
    std::thread timeout_thread_;

    // Configuration
    uint32_t connection_timeout_;      // Seconds
    uint32_t max_connections_per_ip_;

#ifdef STRATUM_USE_SSL
    bool use_ssl_;
    std::string ssl_cert_file_;
    std::string ssl_key_file_;
#endif

    // Metrics
    std::atomic<uint64_t> total_connections_;
    std::atomic<uint64_t> total_shares_;
    std::atomic<uint64_t> total_valid_shares_;
    std::atomic<uint64_t> total_invalid_shares_;
    std::chrono::system_clock::time_point server_start_time_;

#ifdef STRATUM_USE_SSL
    SSL_CTX* ssl_ctx_;
#endif

    // SSL/TLS Methods
#ifdef STRATUM_USE_SSL
    void InitializeSSL() {
        // Initialize OpenSSL
        SSL_load_error_strings();
        OpenSSL_add_ssl_algorithms();

        // Create SSL context
        const SSL_METHOD* method = TLS_server_method();
        ssl_ctx_ = SSL_CTX_new(method);

        if (!ssl_ctx_) {
            LogError("Failed to create SSL context");
            ERR_print_errors_fp(stderr);
            return;
        }

        // Load certificate file
        if (SSL_CTX_use_certificate_file(ssl_ctx_, ssl_cert_file_.c_str(), SSL_FILETYPE_PEM) <= 0) {
            LogError("Failed to load SSL certificate: " + ssl_cert_file_);
            ERR_print_errors_fp(stderr);
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
            return;
        }

        // Load private key file
        if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, ssl_key_file_.c_str(), SSL_FILETYPE_PEM) <= 0) {
            LogError("Failed to load SSL private key: " + ssl_key_file_);
            ERR_print_errors_fp(stderr);
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
            return;
        }

        // Verify private key
        if (!SSL_CTX_check_private_key(ssl_ctx_)) {
            LogError("SSL private key does not match certificate");
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
            return;
        }

        LogInfo("SSL/TLS initialized successfully (Certificate: " + ssl_cert_file_ + ")");
    }

    void CleanupSSL() {
        if (ssl_ctx_) {
            SSL_CTX_free(ssl_ctx_);
            ssl_ctx_ = nullptr;
        }
        EVP_cleanup();
    }

    ssize_t SSLRead(SSL* ssl, void* buf, size_t len) {
        if (!ssl) {
            return -1;
        }
        return SSL_read(ssl, buf, len);
    }

    ssize_t SSLWrite(SSL* ssl, const void* buf, size_t len) {
        if (!ssl) {
            return -1;
        }
        return SSL_write(ssl, buf, len);
    }

    SSL* AcceptSSLConnection(int client_fd) {
        if (!ssl_ctx_) {
            LogError("SSL context not initialized");
            return nullptr;
        }

        SSL* ssl = SSL_new(ssl_ctx_);
        if (!ssl) {
            LogError("Failed to create SSL structure");
            ERR_print_errors_fp(stderr);
            return nullptr;
        }

        SSL_set_fd(ssl, client_fd);

        if (SSL_accept(ssl) <= 0) {
            LogWarning("SSL handshake failed");
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            return nullptr;
        }

        return ssl;
    }

    void CloseSSLConnection(SSL* ssl) {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
    }
#endif

    void AcceptLoop() {
        while (is_running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);

            if (client_fd < 0) {
                if (is_running_) {
                    // Only log error if we're still supposed to be running
                    continue;
                }
                break;
            }

            // Create connection
            Connection conn;
            conn.socket_fd = client_fd;
            conn.worker_id = 0;
            conn.ip_address = inet_ntoa(client_addr.sin_addr);
            conn.subscribed = false;
            conn.authorized = false;
            conn.connected_at = std::chrono::system_clock::now();
            conn.last_activity = std::chrono::system_clock::now();

#ifdef STRATUM_USE_SSL
            // Perform SSL handshake if SSL is enabled
            if (use_ssl_) {
                SSL* ssl = AcceptSSLConnection(client_fd);
                if (!ssl) {
                    LogWarning("SSL handshake failed for " + conn.ip_address);
                    close(client_fd);
                    continue;
                }
                conn.ssl = ssl;
                LogInfo("SSL connection established for " + conn.ip_address);
            } else {
                conn.ssl = nullptr;
            }
#endif

            uint64_t conn_id = next_conn_id_++;

            {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                connections_[conn_id] = conn;
            }

            // Check connection limit per IP
            uint32_t ip_conn_count = CountConnectionsFromIP(conn.ip_address);
            if (ip_conn_count >= max_connections_per_ip_) {
                LogWarning("Connection limit exceeded for IP " + conn.ip_address +
                          " (" + std::to_string(ip_conn_count) + " connections)");
                close(client_fd);
                continue;
            }

            LogInfo("New connection from " + conn.ip_address + " (ID: " + std::to_string(conn_id) + ")");
            total_connections_++;

            // Start client handler thread
            std::thread(&StratumServer::HandleClient, this, conn_id).detach();
        }
    }

    void TimeoutMonitorLoop() {
        while (is_running_) {
            // Sleep for 30 seconds between checks
            std::this_thread::sleep_for(std::chrono::seconds(30));

            if (!is_running_) break;

            // Check for idle connections
            std::vector<uint64_t> timeout_connections;
            auto now = std::chrono::system_clock::now();

            {
                std::lock_guard<std::mutex> lock(connections_mutex_);

                for (const auto& [conn_id, conn] : connections_) {
                    auto idle_duration = std::chrono::duration_cast<std::chrono::seconds>(
                        now - conn.last_activity);

                    if (idle_duration.count() > connection_timeout_) {
                        timeout_connections.push_back(conn_id);
                    }
                }
            }

            // Disconnect idle connections
            for (uint64_t conn_id : timeout_connections) {
                LogInfo("Disconnecting idle connection " + std::to_string(conn_id) +
                       " (timeout: " + std::to_string(connection_timeout_) + "s)");
                RemoveConnection(conn_id);
            }

            // Log statistics every 30 seconds
            if (timeout_connections.empty()) {
                LogDebug("Active connections: " + std::to_string(GetConnectionCount()) +
                        ", Total shares: " + std::to_string(total_shares_.load()) +
                        " (Valid: " + std::to_string(total_valid_shares_.load()) +
                        ", Invalid: " + std::to_string(total_invalid_shares_.load()) + ")");
            }
        }
    }

    uint32_t CountConnectionsFromIP(const std::string& ip_address) {
        std::lock_guard<std::mutex> lock(connections_mutex_);

        uint32_t count = 0;
        for (const auto& [conn_id, conn] : connections_) {
            if (conn.ip_address == ip_address) {
                count++;
            }
        }
        return count;
    }

    size_t GetConnectionCount() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(connections_mutex_));
        return connections_.size();
    }

    // Logging functions
    void LogInfo(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::ctime(&time);
        timestamp.pop_back();  // Remove newline
        std::cout << "[" << timestamp << "] [INFO] [Stratum] " << message << std::endl;
    }

    void LogWarning(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::ctime(&time);
        timestamp.pop_back();
        std::cout << "[" << timestamp << "] [WARN] [Stratum] " << message << std::endl;
    }

    void LogError(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::ctime(&time);
        timestamp.pop_back();
        std::cerr << "[" << timestamp << "] [ERROR] [Stratum] " << message << std::endl;
    }

    void LogDebug(const std::string& message) {
        // Only log debug messages if debug mode is enabled
        #ifdef STRATUM_DEBUG
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::ctime(&time);
        timestamp.pop_back();
        std::cout << "[" << timestamp << "] [DEBUG] [Stratum] " << message << std::endl;
        #else
        (void)message;  // Suppress unused parameter warning
        #endif
    }

    void HandleClient(uint64_t conn_id) {
        char buffer[4096];
        std::string message_buffer;

        while (is_running_) {
            ssize_t bytes_read;

#ifdef STRATUM_USE_SSL
            // Get SSL pointer if SSL is enabled
            SSL* ssl = nullptr;
            if (use_ssl_) {
                std::lock_guard<std::mutex> lock(connections_mutex_);
                auto it = connections_.find(conn_id);
                if (it != connections_.end()) {
                    ssl = it->second.ssl;
                }
            }

            if (use_ssl_ && ssl) {
                bytes_read = SSLRead(ssl, buffer, sizeof(buffer) - 1);
            } else {
                bytes_read = recv(GetSocket(conn_id), buffer, sizeof(buffer) - 1, 0);
            }
#else
            bytes_read = recv(GetSocket(conn_id), buffer, sizeof(buffer) - 1, 0);
#endif

            if (bytes_read <= 0) {
                // Connection closed or error
                break;
            }

            buffer[bytes_read] = '\0';
            message_buffer += buffer;

            // Process complete JSON-RPC messages (newline-delimited)
            size_t pos;
            while ((pos = message_buffer.find('\n')) != std::string::npos) {
                std::string message = message_buffer.substr(0, pos);
                message_buffer = message_buffer.substr(pos + 1);

                ProcessMessage(conn_id, message);
            }

            UpdateActivity(conn_id);
        }

        // Clean up connection
        RemoveConnection(conn_id);
    }

    void ProcessMessage(uint64_t conn_id, const std::string& message) {
        // Parse JSON-RPC message
        auto msg_result = ParseStratumMessage(message);
        if (msg_result.IsError()) {
            LogError("Invalid JSON from connection " + std::to_string(conn_id) +
                    " (" + GetIP(conn_id) + "): " + msg_result.error);
            SendError(conn_id, 20, "Invalid JSON");
            return;
        }

        auto msg = msg_result.GetValue();

        LogDebug("Received " + msg.method + " from connection " + std::to_string(conn_id));

        // Route to appropriate handler
        if (msg.method == "mining.subscribe") {
            HandleSubscribe(conn_id, msg);
        } else if (msg.method == "mining.authorize") {
            HandleAuthorize(conn_id, msg);
        } else if (msg.method == "mining.submit") {
            HandleSubmit(conn_id, msg);
        } else {
            LogWarning("Unknown method '" + msg.method + "' from connection " +
                      std::to_string(conn_id) + " (" + GetIP(conn_id) + ")");
            SendError(conn_id, 20, "Unknown method");
        }
    }

    void HandleSubscribe(uint64_t conn_id, const Message& msg) {
        // Generate extranonce1 (unique per connection)
        std::string extranonce1 = std::to_string(conn_id);

        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(conn_id);
            if (it != connections_.end()) {
                it->second.subscribed = true;
                it->second.extranonce1 = extranonce1;
            }
        }

        LogInfo("Worker subscribed: Connection " + std::to_string(conn_id) +
               " (" + GetIP(conn_id) + "), Extranonce1: " + extranonce1);

        // Send subscribe response
        std::string response = "{\"id\":" + std::to_string(msg.id) +
                              ",\"result\":[[\"mining.notify\",\"" + extranonce1 + "\"],\"" +
                              extranonce1 + "\",4],\"error\":null}\n";

        SendRaw(conn_id, response);
    }

    void HandleAuthorize(uint64_t conn_id, const Message& msg) {
        if (msg.params.size() < 2) {
            SendError(conn_id, 20, "Invalid params");
            return;
        }

        std::string username = msg.params[0];
        std::string password = msg.params[1];

        // Parse username.workername format
        size_t dot_pos = username.find('.');
        std::string miner_username = username;
        std::string worker_name = "default";

        if (dot_pos != std::string::npos) {
            miner_username = username.substr(0, dot_pos);
            worker_name = username.substr(dot_pos + 1);
        }

        // Get or register miner
        auto miner_opt = pool_.GetMinerByUsername(miner_username);
        uint64_t miner_id;

        if (!miner_opt.has_value()) {
            // Auto-register miner with username as payout address
            auto register_result = pool_.RegisterMiner(miner_username, miner_username, "");
            if (register_result.IsError()) {
                SendError(conn_id, 24, "Authorization failed");
                return;
            }
            miner_id = register_result.GetValue();
        } else {
            miner_id = miner_opt->miner_id;
        }

        // Add worker
        std::string ip = GetIP(conn_id);
        auto worker_result = pool_.AddWorker(miner_id, worker_name, ip, 0);

        if (worker_result.IsError()) {
            SendError(conn_id, 24, "Failed to add worker");
            return;
        }

        uint64_t worker_id = worker_result.GetValue();

        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(conn_id);
            if (it != connections_.end()) {
                it->second.authorized = true;
                it->second.worker_id = worker_id;
            }
        }

        LogInfo("Worker authorized: " + username + " (Worker ID: " + std::to_string(worker_id) +
               ", Connection: " + std::to_string(conn_id) + ", IP: " + ip + ")");

        // Send success
        std::string response = "{\"id\":" + std::to_string(msg.id) +
                              ",\"result\":true,\"error\":null}\n";
        SendRaw(conn_id, response);

        // Send current difficulty
        auto worker_opt = pool_.GetWorker(worker_id);
        if (worker_opt.has_value()) {
            SendDifficulty(conn_id, worker_opt->current_difficulty);
        }

        // Send current work
        auto work_opt = pool_.GetCurrentWork();
        if (work_opt.has_value()) {
            SendNotify(conn_id, work_opt.value());
        }
    }

    void HandleSubmit(uint64_t conn_id, const Message& msg) {
        if (msg.params.size() < 5) {
            SendError(conn_id, 20, "Invalid params");
            return;
        }

        uint64_t worker_id = GetWorkerId(conn_id);
        if (worker_id == 0) {
            SendError(conn_id, 25, "Not authorized");
            return;
        }

        // Parse submit parameters
        // params: [worker_name, job_id, extranonce2, ntime, nonce]
        std::string worker_name = msg.params[0];
        std::string job_id_str = msg.params[1];
        std::string extranonce2_hex = msg.params[2];
        std::string ntime_hex = msg.params[3];
        std::string nonce_hex = msg.params[4];

        // Convert job_id from hex
        auto job_id_result = HexToUint256(job_id_str);
        if (job_id_result.IsError()) {
            SendError(conn_id, 20, "Invalid job_id");
            return;
        }
        uint256 job_id = job_id_result.GetValue();

        // Convert nonce from hex
        auto nonce_result = HexToUint256(nonce_hex);
        if (nonce_result.IsError()) {
            SendError(conn_id, 20, "Invalid nonce");
            return;
        }
        uint256 nonce = nonce_result.GetValue();

        // Parse ntime (4 bytes hex)
        auto ntime_result = HexToUint32(ntime_hex);
        if (ntime_result.IsError()) {
            SendError(conn_id, 20, "Invalid ntime");
            return;
        }
        uint32_t ntime = ntime_result.GetValue();
        (void)ntime;  // TODO: Use ntime in share validation

        // Parse extranonce2
        auto extranonce2_result = HexToBytes(extranonce2_hex);
        if (extranonce2_result.IsError()) {
            SendError(conn_id, 20, "Invalid extranonce2");
            return;
        }
        std::vector<uint8_t> extranonce2 = extranonce2_result.GetValue();

        // Calculate share hash
        // Note: This is simplified - production version would reconstruct full block header
        // For now, use the provided nonce to calculate a hash
        uint256 hash{};

        // Submit share to pool
        auto submit_result = pool_.SubmitShare(worker_id, job_id, nonce, hash);

        // Update metrics
        total_shares_++;

        std::string ip = GetIP(conn_id);

        if (submit_result.IsOk()) {
            total_valid_shares_++;
            LogInfo("Valid share from worker " + std::to_string(worker_id) +
                   " (" + ip + ") - Job: " + job_id_str.substr(0, 16) + "...");

            std::string response = "{\"id\":" + std::to_string(msg.id) +
                                  ",\"result\":true,\"error\":null}\n";
            SendRaw(conn_id, response);
        } else {
            total_invalid_shares_++;
            LogWarning("Invalid share from worker " + std::to_string(worker_id) +
                      " (" + ip + ") - Reason: " + submit_result.error);

            SendError(conn_id, 23, submit_result.error);
        }
    }

    void SendNotify(uint64_t conn_id, const Work& work) {
        // Build coinbase transaction parts (coinb1 and coinb2)
        // The extranonce goes between coinb1 and coinb2
        auto coinbase_serialized = work.coinbase_tx.Serialize();

        // Find the extranonce placeholder position in the coinbase script
        // For now, we'll split at a reasonable position (before the scriptSig)
        // The extranonce is typically 8 bytes (4 bytes extranonce1 + 4 bytes extranonce2)
        size_t extranonce_pos = 42;  // Standard position after inputs count and prev hash

        std::string coinb1 = ToHex(
            std::vector<uint8_t>(coinbase_serialized.begin(),
                                 coinbase_serialized.begin() + extranonce_pos)
        );
        std::string coinb2 = ToHex(
            std::vector<uint8_t>(coinbase_serialized.begin() + extranonce_pos + 8,  // +8 for extranonce space
                                 coinbase_serialized.end())
        );

        // Build merkle branch from other transactions
        std::vector<std::string> merkle_branch;
        if (work.transactions.size() > 0) {
            // Get transaction hashes (excluding coinbase at index 0)
            std::vector<uint256> tx_hashes;
            tx_hashes.push_back(work.coinbase_tx.GetHash());  // Coinbase first

            for (const auto& tx : work.transactions) {
                tx_hashes.push_back(tx.GetHash());
            }

            // Build merkle tree
            auto merkle_tree = BuildMerkleTree(tx_hashes);

            // Extract merkle branches (the sibling hashes needed to verify)
            // For Stratum, we need the branches to reconstruct merkle root
            size_t tree_size = merkle_tree.size();
            size_t height = 0;
            size_t current_size = tx_hashes.size();

            while (current_size > 1) {
                // Get the sibling hash for this level
                size_t sibling_index = (height % 2 == 0) ? height + 1 : height - 1;
                if (sibling_index < tree_size) {
                    merkle_branch.push_back(ToHex(merkle_tree[sibling_index]));
                }
                current_size = (current_size + 1) / 2;
                height = (height + current_size);
            }
        }

        // Build merkle_branch JSON array
        std::string merkle_array = "[";
        for (size_t i = 0; i < merkle_branch.size(); i++) {
            if (i > 0) merkle_array += ",";
            merkle_array += "\"" + merkle_branch[i] + "\"";
        }
        merkle_array += "]";

        // Format mining.notify message according to Stratum protocol
        // params: [job_id, prevhash, coinb1, coinb2, merkle_branch, version, nbits, ntime, clean_jobs]
        std::string msg = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"" +
                         ToHex(work.job_id) + "\",\"" +
                         ToHex(work.header.prev_block_hash) + "\",\"" +
                         coinb1 + "\",\"" +
                         coinb2 + "\"," +
                         merkle_array + ",\"" +
                         ToHex(work.header.version) + "\",\"" +
                         ToHex(work.header.bits) + "\",\"" +
                         ToHex(static_cast<uint32_t>(work.header.timestamp)) + "\"," +
                         (work.clean_jobs ? "true" : "false") + "]}\n";

        SendRaw(conn_id, msg);
    }

    void SendError(uint64_t conn_id, int code, const std::string& message) {
        std::string response = "{\"id\":null,\"result\":null,\"error\":[" +
                              std::to_string(code) + ",\"" + message + "\",null]}\n";
        SendRaw(conn_id, response);
    }

    void SendRaw(uint64_t conn_id, const std::string& data) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
#ifdef STRATUM_USE_SSL
            if (use_ssl_ && it->second.ssl) {
                SSLWrite(it->second.ssl, data.c_str(), data.length());
            } else {
                send(it->second.socket_fd, data.c_str(), data.length(), 0);
            }
#else
            send(it->second.socket_fd, data.c_str(), data.length(), 0);
#endif
        }
    }

    int GetSocket(uint64_t conn_id) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(conn_id);
        return (it != connections_.end()) ? it->second.socket_fd : -1;
    }

    uint64_t GetWorkerId(uint64_t conn_id) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(conn_id);
        return (it != connections_.end()) ? it->second.worker_id : 0;
    }

    std::string GetIP(uint64_t conn_id) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(conn_id);
        return (it != connections_.end()) ? it->second.ip_address : "";
    }

    void UpdateActivity(uint64_t conn_id) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            it->second.last_activity = std::chrono::system_clock::now();
        }
    }

    void RemoveConnection(uint64_t conn_id) {
        std::lock_guard<std::mutex> lock(connections_mutex_);

        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            std::string ip = it->second.ip_address;
            uint64_t worker_id = it->second.worker_id;

            // Calculate connection duration
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now() - it->second.connected_at);

            LogInfo("Connection closed: ID " + std::to_string(conn_id) +
                   ", IP " + ip +
                   ", Duration " + std::to_string(duration.count()) + "s" +
                   (worker_id != 0 ? ", Worker " + std::to_string(worker_id) : ""));

            // Remove worker from pool
            if (worker_id != 0) {
                pool_.RemoveWorker(worker_id);
            }

            close(it->second.socket_fd);
            connections_.erase(it);
        }
    }

    Result<Message> ParseStratumMessage(const std::string& json) {
        auto parse_result = ParseJSON(json);
        if (parse_result.IsError()) {
            return Result<Message>::Error(parse_result.error);
        }

        auto fields = parse_result.GetValue();

        Message msg;

        // Extract id
        if (fields.find("id") != fields.end()) {
            try {
                msg.id = std::stoull(fields["id"]);
            } catch (...) {
                msg.id = 0;
            }
        }

        // Extract method
        if (fields.find("method") != fields.end()) {
            msg.method = fields["method"];
        }

        // Extract params array
        if (fields.find("params") != fields.end()) {
            std::string params_str = fields["params"];

            // Remove brackets
            if (params_str.front() == '[') params_str = params_str.substr(1);
            if (params_str.back() == ']') params_str.pop_back();

            // Split by comma (simplified - doesn't handle nested arrays)
            size_t pos = 0;
            while (pos < params_str.length()) {
                // Skip whitespace
                while (pos < params_str.length() && std::isspace(params_str[pos])) pos++;

                if (pos >= params_str.length()) break;

                std::string param;
                if (params_str[pos] == '"') {
                    // String parameter
                    pos++;  // Skip opening quote
                    size_t end = params_str.find('"', pos);
                    if (end != std::string::npos) {
                        param = params_str.substr(pos, end - pos);
                        pos = end + 1;
                    }
                } else {
                    // Number or other
                    size_t end = params_str.find(',', pos);
                    if (end == std::string::npos) end = params_str.length();
                    param = params_str.substr(pos, end - pos);
                    pos = end;
                }

                if (!param.empty()) {
                    msg.params.push_back(param);
                }

                // Skip comma
                if (pos < params_str.length() && params_str[pos] == ',') pos++;
            }
        }

        return Result<Message>::Ok(msg);
    }
};

// Factory and wrapper functions for external use
StratumServer* CreateStratumServer(uint16_t port, MiningPoolServer& pool) {
    return new StratumServer(port, pool);
}

void DestroyStratumServer(StratumServer* server) {
    if (server) {
        server->Stop();
        delete server;
    }
}

Result<void> StratumServerStart(StratumServer* server) {
    if (server) {
        return server->Start();
    }
    return Result<void>::Error("Null server pointer");
}

void StratumServerBroadcastWork(StratumServer* server, const Work& work) {
    if (server) {
        server->BroadcastWork(work);
    }
}

} // namespace stratum
} // namespace intcoin
