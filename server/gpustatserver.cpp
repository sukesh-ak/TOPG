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

// Query string for nvidia-smi
const std::string NVIDIA_SMI_QUERY =
    "index,name,utilization.gpu,utilization.memory,memory.total,memory.free,memory.used,temperature.gpu";

// Regex to parse CSV fields
std::regex csv_regex(R"((\d+),\s*([^,]+?),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+))");

// Helper: Run shell command and get output
std::string exec_command(const char *cmd)
{
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
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
            result.push_back(fmt::format(
                R"({{"index":"{}","name":"{}","utilization.gpu":{},"utilization.memory":{},"memory.total":{},"memory.free":{},"memory.used":{},"temperature.gpu":{}}})",
                match[1].str(), match[2].str(), match[3].str(), match[4].str(),
                match[5].str(), match[6].str(), match[7].str(), match[8].str()));
        }
    }
    return result;
}

// Convert to JSON array
std::string to_json_array(const std::vector<std::string> &data)
{
    rapidjson::Document doc;
    doc.SetArray();
    auto &allocator = doc.GetAllocator();

    for (const auto &s : data)
    {
        rapidjson::Document item;
        item.Parse(s.c_str());
        if (item.IsObject())
        {
            doc.PushBack(item, allocator);
        }
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}

// User data for WebSocket
struct UserData
{
    bool live = false;
};

// Global App pointer for broadcasting
uWS::App *globalApp = nullptr;

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
        app.ws<UserData>("/*", {
            .compression = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 16 * 1024,
            .idleTimeout = 16,
            .maxBackpressure = 1 * 1024 * 1024,
            .closeOnBackpressureLimit = false,
            .resetIdleTimeoutOnSend = true,
            .sendPingsAutomatically = true,
            
            .open = [](auto *ws) {
                UserData* user_data = (UserData*)ws->getUserData();
                user_data->live = false;
                std::cout << "Client connected: " << ws << std::endl;
                std::string welcome = R"({"status":"connected","help":"/gpu, /live, /stop"})";
                ws->send(welcome, uWS::OpCode::TEXT);
            },
            
            .message = [this](auto *ws, std::string_view message, uWS::OpCode opCode) {
                std::string payload(message);
                UserData* user_data = static_cast<UserData*>(ws->getUserData());

                if (payload == "/gpu") {
                    std::string output = run_nvidia_smi();
                    auto parsed = parse_nvidia_smi_output(output);
                    std::string json = to_json_array(parsed);
                    ws->send(json, uWS::OpCode::TEXT);
                }
                else if (payload == "/live") {
                    user_data->live = true;
                    ws->subscribe("gpu_live");
                    std::string resp = R"({"status":"live","message":"Live updates enabled"})";
                    ws->send(resp, uWS::OpCode::TEXT);
                }
                else if (payload == "/stop") {
                    user_data->live = false;
                    ws->unsubscribe("gpu_live");
                    std::string resp = R"({"status":"stopped","message":"Live updates stopped"})";
                    ws->send(resp, uWS::OpCode::TEXT);
                }
                else {
                    std::string resp = "{\"error\":\"Unknown command: " + payload + "\"}";
                    ws->send(resp, uWS::OpCode::TEXT);
                }
            },
            
            .close = [](auto *ws, int code, std::string_view message) {
                std::cout << "Client disconnected: " << ws << " (code: " << code << ")" << std::endl;
            }
        })
        .listen(8080, [](auto *listen_socket) {
            if (listen_socket) {
                std::cout << "🚀 uWebSockets++ GPU Server running on ws://localhost:8080\n";
                std::cout << "💡 Use: /gpu, /live, /stop\n";
            } else {
                std::cerr << "❌ Failed to listen on port 8080\n";
            }
        });

        // Set global app for broadcasting
        globalApp = &app;
    }

    void setupTimer()
    {
        // Set global app for broadcasting
        globalApp = &app;
        
        // Create a timer for periodic broadcasting (every 1000ms)
        struct us_loop_t *loop = (struct us_loop_t *) uWS::Loop::get();
        struct us_timer_t *broadcastTimer = us_create_timer(loop, 0, 0);
        
        us_timer_set(broadcastTimer, [](struct us_timer_t *timer) {
            if (globalApp) {
                std::string output = exec_command("nvidia-smi --query-gpu=index,name,utilization.gpu,utilization.memory,memory.total,memory.free,memory.used,temperature.gpu --format=csv,noheader,nounits");
                auto parsed = parse_nvidia_smi_output(output);
                std::string json = to_json_array(parsed);

                // Broadcast to all subscribed clients
                globalApp->publish("gpu_live", json, uWS::OpCode::TEXT);
            }
        }, 1000, 1000); // 1000ms delay, 1000ms repeat
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

    // Run the app
    server.run();

    return 0;
}