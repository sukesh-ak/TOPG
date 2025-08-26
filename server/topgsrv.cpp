// #define ENABLE_DEBUG

// gpustatserver.cpp
#include "uwebsockets/App.h"
#include "libusockets.h"
// gpustatserver.cpp

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <regex>
#include <fmt/core.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <array>
#include <atomic>

// Query string for nvidia-smi
const std::string NVIDIA_SMI_QUERY =
    "index,name,utilization.gpu,utilization.memory,memory.total,memory.free,memory.used,temperature.gpu";

// Regex to parse CSV fields - much more specific pattern
std::regex csv_regex(R"(^(\d+),\s*([^,]+?),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)$)");

// Helper: Run shell command and get output
std::string exec_command(const char *cmd)
{
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen(cmd, "r"), pclose);
    if (!pipe)
    {
        return "nvidia-smi error: command failed";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }
    return result;
}

// Parse nvidia-smi output into JSON strings
std::vector<std::string> parse_nvidia_smi_output(const std::string &raw_output)
{
    std::vector<std::string> lines;
    std::istringstream iss(raw_output);
    std::string line;
    while (std::getline(iss, line))
    {
        line.erase(line.find_last_not_of("\r\n\t ") + 1);
        if (!line.empty())
        {
            lines.push_back(line);
        }
    }

    std::vector<std::string> result;
    for (const auto &l : lines)
    {
        std::smatch match;
        if (std::regex_match(l, match, csv_regex))
        {
            try
            {
                // Parse the 8 fields directly
                int index = std::stoi(match[1].str());
                std::string name = match[2].str();

                // Trim spaces from GPU name
                name.erase(0, name.find_first_not_of(" \t"));
                name.erase(name.find_last_not_of(" \t") + 1);

                int gpu_util = std::stoi(match[3].str());
                int mem_util = std::stoi(match[4].str());
                int mem_total = std::stoi(match[5].str());
                int mem_free = std::stoi(match[6].str());
                int mem_used = std::stoi(match[7].str());
                int temperature = std::stoi(match[8].str());

                // Build clean JSON
                std::string json_str = "{";
                json_str += "\"index\":" + std::to_string(index) + ",";
                json_str += "\"name\":\"" + name + "\",";
                json_str += "\"utilization.gpu\":" + std::to_string(gpu_util) + ",";
                json_str += "\"utilization.memory\":" + std::to_string(mem_util) + ",";
                json_str += "\"memory.total\":" + std::to_string(mem_total) + ",";
                json_str += "\"memory.free\":" + std::to_string(mem_free) + ",";
                json_str += "\"memory.used\":" + std::to_string(mem_used) + ",";
                json_str += "\"temperature.gpu\":" + std::to_string(temperature);
                json_str += "}";
#ifdef ENABLE_DEBUG
                std::cout << "Generated individual JSON: " << json_str << std::endl;
#endif
                result.push_back(json_str);
            }
            catch (const std::exception &e)
            {
                // Skip malformed lines silently
                continue;
            }
        }
    }
    return result;
}

// Convert to JSON array - simplified version
std::string to_json_array(const std::vector<std::string> &data)
{
    if (data.empty())
    {
        return "[]";
    }

    std::string result = "[";
    for (size_t i = 0; i < data.size(); ++i)
    {
        result += data[i];
        if (i < data.size() - 1)
        {
            result += ",";
        }
    }
    result += "]";
#ifdef ENABLE_DEBUG
    std::cout << "=== FINAL JSON ARRAY SENT TO CLIENT ===" << std::endl;
    std::cout << result << std::endl;
    std::cout << "=========================================" << std::endl;
#endif
    return result;
}

// User data for WebSocket
struct UserData
{
    bool live = false;
};

// Global App pointer for broadcasting
uWS::App *globalApp = nullptr;

// Manual connection counters
std::atomic<int> totalConnections{0};
std::atomic<int> liveSubscribers{0};

// GPU Server
class GpuServer
{
public:
    uWS::App app;

    GpuServer()
    {
        setupRoutes();
    }

    void setupRoutes()
    {
        app.ws<UserData>("/*", {.compression = uWS::SHARED_COMPRESSOR,
                                .maxPayloadLength = 16 * 1024,
                                .idleTimeout = 16,
                                .maxBackpressure = 1 * 1024 * 1024,
                                .closeOnBackpressureLimit = false,
                                .resetIdleTimeoutOnSend = true,
                                .sendPingsAutomatically = true,

                                .open = [](auto *ws)
                                {
                UserData* user_data = (UserData*)ws->getUserData();
                user_data->live = false;
                totalConnections++;
                std::cout << "Client connected: " << ws << ", total connections: " << totalConnections.load() << std::endl;
                std::string welcome = R"({"status":"connected","help":"/gpu, /live, /stop"})";
                ws->send(welcome, uWS::OpCode::TEXT); },

                                .message = [this](auto *ws, std::string_view message, uWS::OpCode opCode)
                                {
                std::string payload(message);
                UserData* user_data = static_cast<UserData*>(ws->getUserData());

                if (payload == "/gpu") {
                    std::string output = run_nvidia_smi();
                    auto parsed = parse_nvidia_smi_output(output);
                    std::string json = to_json_array(parsed);
                    ws->send(json, uWS::OpCode::TEXT);
                }
                else if (payload == "/live") {
                    if (!user_data->live) {
                        user_data->live = true;
                        ws->subscribe("gpu_live");
                        liveSubscribers++;
                        std::cout << "Client subscribed to live updates, total live subscribers: " << liveSubscribers.load() << std::endl;
                    }
                    std::string resp = R"({"status":"live","message":"Live updates enabled"})";
                    ws->send(resp, uWS::OpCode::TEXT);
                }
                else if (payload == "/stop") {
                    if (user_data->live) {
                        user_data->live = false;
                        ws->unsubscribe("gpu_live");
                        liveSubscribers--;
                        std::cout << "Client unsubscribed from live updates, remaining live subscribers: " << liveSubscribers.load() << std::endl;
                    }
                    std::string resp = R"({"status":"stopped","message":"Live updates stopped"})";
                    ws->send(resp, uWS::OpCode::TEXT);
                }
                else {
                    std::string resp = "{\"error\":\"Unknown command: " + payload + "\"}";
                    ws->send(resp, uWS::OpCode::TEXT);
                } },

                                .close = [](auto *ws, int code, std::string_view message)
                                { 
                                    UserData* user_data = static_cast<UserData*>(ws->getUserData());
                                    totalConnections--;
                                    if (user_data && user_data->live) {
                                        ws->unsubscribe("gpu_live");
                                        user_data->live = false;
                                        liveSubscribers--;
                                        std::cout << "Unsubscribed client from live updates on disconnect" << std::endl;
                                    }
                                    std::cout << "Client disconnected: " << ws << " (code: " << code << "), total connections: " << totalConnections.load() << ", live subscribers: " << liveSubscribers.load() << std::endl; }})
            .listen("0.0.0.0", 8080, [](auto *listen_socket)
                    {
            if (listen_socket) {
                std::cout << "🚀 TOPG GPU Server running on ws://localhost:8080\n";
                std::cout << "💡 Use: /gpu, /live, /stop\n";
            } else {
                std::cerr << "❌ Failed to listen on port 8080\n";
            } });

        // Set global app for broadcasting
        globalApp = &app;
    }

    void setupTimer()
    {
        // Set global app for broadcasting
        globalApp = &app;

        // Start a background thread for periodic broadcasting
        std::thread([this]()
                    {
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                
                if (globalApp) {
                    std::string output = exec_command("nvidia-smi --query-gpu=index,name,utilization.gpu,utilization.memory,memory.total,memory.free,memory.used,temperature.gpu --format=csv,noheader,nounits");
                    auto parsed = parse_nvidia_smi_output(output);
                    std::string json = to_json_array(parsed);

                    // Broadcast to all subscribed clients
                    globalApp->publish("gpu_live", json, uWS::OpCode::TEXT);
#ifdef ENABLE_DEBUG
                    std::cout << "Broadcasting GPU data to " << liveSubscribers.load() << " live clients" << std::endl;
#endif
                }
            } })
            .detach();
    }

    void run()
    {
        app.run();
    }

private:
    std::string run_nvidia_smi()
    {
        std::string cmd = "nvidia-smi --query-gpu=" + NVIDIA_SMI_QUERY + " --format=csv,noheader,nounits";
        return exec_command(cmd.c_str());
    }
};

// Main
int main()
{
    GpuServer server;

    // Setup timer for periodic broadcasting
    server.setupTimer();

    // Run the app
    server.run();

    return 0;
}