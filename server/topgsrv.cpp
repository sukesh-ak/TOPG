// gpustatserver.cpp
#include "uwebsockets/App.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <regex>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <array>
#include <atomic>
#include <cxxopts.hpp>

const std::string NVIDIA_SMI_QUERY =
    "nvidia-smi --query-gpu=index,name,utilization.gpu,utilization.memory,memory.total,memory.free,memory.used,temperature.gpu --format=csv,noheader,nounits";

std::regex csv_regex(R"(^(\d+),\s*([^,]+?),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)$)");

std::string exec_command(const std::string &cmd)
{
    std::array<char, 128> buffer;
    std::string result;

#ifdef _WIN32
    std::unique_ptr<FILE, int (*)(FILE *)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif

    if (!pipe)
        return "nvidia-smi error: command failed";

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }
    return result;
}

std::vector<std::string> parse_nvidia_smi_output(const std::string &raw_output)
{
    std::vector<std::string> result;
    std::istringstream iss(raw_output);
    std::string line;

    while (std::getline(iss, line))
    {
        line.erase(line.find_last_not_of("\r\n\t ") + 1);
        if (line.empty())
            continue;

        std::smatch match;
        if (!std::regex_match(line, match, csv_regex))
            continue;

        try
        {
            std::string name = match[2].str();
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);

            result.push_back(
                "{\"index\":" + match[1].str() +
                ",\"name\":\"" + name + "\"" +
                ",\"utilization.gpu\":" + match[3].str() +
                ",\"utilization.memory\":" + match[4].str() +
                ",\"memory.total\":" + match[5].str() +
                ",\"memory.free\":" + match[6].str() +
                ",\"memory.used\":" + match[7].str() +
                ",\"temperature.gpu\":" + match[8].str() +
                "}");
        }
        catch (...)
        {
            continue;
        }
    }
    return result;
}

std::string to_json_array(const std::vector<std::string> &data)
{
    if (data.empty())
        return "[]";

    std::string result = "[";
    for (size_t i = 0; i < data.size(); ++i)
    {
        result += data[i];
        if (i < data.size() - 1)
            result += ",";
    }
    result += "]";
    return result;
}

struct UserData
{
    bool live = false;
};

uWS::App *globalApp = nullptr;

class GpuServer
{
public:
    uWS::App app;
    std::string host;
    int port;
    int update_interval_ms;

    GpuServer(const std::string &host_addr, int port_num, int update_interval)
        : host(host_addr), port(port_num), update_interval_ms(update_interval)
    {
        setupRoutes();
    }

    void setupRoutes()
    {
        uWS::App::WebSocketBehavior<UserData> behavior;
        behavior.compression = uWS::SHARED_COMPRESSOR;
        behavior.maxPayloadLength = 16 * 1024;
        behavior.idleTimeout = 16;
        behavior.maxBackpressure = 1 * 1024 * 1024;
        behavior.closeOnBackpressureLimit = false;
        behavior.resetIdleTimeoutOnSend = true;
        behavior.sendPingsAutomatically = true;

        behavior.open = [](auto *ws)
        {
            UserData *user_data = (UserData *)ws->getUserData();
            user_data->live = false;
            std::cout << "Client connected: " << ws << std::endl;
            ws->send(R"({"status":"connected","help":"/live"})", uWS::OpCode::TEXT);
        };

        behavior.message = [this](auto *ws, std::string_view message, uWS::OpCode)
        {
            std::string payload(message);
            UserData *user_data = static_cast<UserData *>(ws->getUserData());

            if (payload == "/live")
            {
                if (!user_data->live)
                {
                    user_data->live = true;
                    ws->subscribe("gpu_live");
                    std::cout << "Client subscribed to live updates" << std::endl;
                }
                ws->send(R"({"status":"live","message":"Live updates enabled"})", uWS::OpCode::TEXT);
            }
            else
            {
                ws->send("{\"error\":\"Unknown command: " + payload + "\"}", uWS::OpCode::TEXT);
            }
        };

        behavior.close = [](auto *ws, int, std::string_view)
        {
            UserData *user_data = static_cast<UserData *>(ws->getUserData());
            if (user_data && user_data->live)
            {
                ws->unsubscribe("gpu_live");
                user_data->live = false;
                std::cout << "Unsubscribed client from live updates on disconnect" << std::endl;
            }
            std::cout << "Client disconnected: " << ws << std::endl;
        };

        app.ws<UserData>("/*", std::move(behavior))
            .listen(host, port, [this](auto *listen_socket)
                    {
               if (listen_socket) {
                   std::cout << "🟢 TOPG GPU Server running on ws://" << host << ":" << port << "\n";
                   std::cout << "💡 Use: /live\n";
               } else {
                   std::cerr << "❌ Failed to listen on " << host << ":" << port << "\n";
               } });

        globalApp = &app;
    }

    void setupTimer()
    {
        globalApp = &app;
        std::thread([this]()
                    {
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
                if (globalApp) {
                    std::string output = exec_command(NVIDIA_SMI_QUERY);
                    auto parsed = parse_nvidia_smi_output(output);
                    std::string json = to_json_array(parsed);
                    globalApp->publish("gpu_live", json, uWS::OpCode::TEXT);
                }
            } })
            .detach();
    }

    void run()
    {
        app.run();
    }
};

int main(int argc, char *argv[])
{
    try
    {
        cxxopts::Options options("topgsrv", "TOPG GPU Monitoring Server - Real-time GPU stats via WebSocket");

        options.add_options()("h,host", "Host address to bind to", cxxopts::value<std::string>()->default_value("0.0.0.0"))   // Host
            ("p,port", "Port to listen on", cxxopts::value<int>()->default_value("8080"))                                     // Port
            ("f,frequency", "Update frequency in milliseconds (default: 1000)", cxxopts::value<int>()->default_value("1000")) // Update frequency
            ("help", "Print usage information");

        auto result = options.parse(argc, argv);
        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            return 0;
        }

        std::string host = result["host"].as<std::string>();
        int port = result["port"].as<int>();
        int frequency = result["frequency"].as<int>();

        std::cout << "⚪️ Starting TOPG GPU Server...\n";
        std::cout << " [Binding to: " << host << ":" << port << "]\n";
        std::cout << " Update frequency: " << frequency << "ms\n";

        GpuServer server(host, port, frequency);
        server.setupTimer();
        server.run();
    }
    catch (const cxxopts::exceptions::exception &e)
    {
        std::cerr << "❌ Error parsing options: " << e.what() << std::endl;
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}