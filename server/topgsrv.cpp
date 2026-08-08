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

#ifdef _WIN32
#include <windows.h>
#endif

/*
     TODO: Implement later to to show processes using GPU
    == List of apps using the GPU
    $ nvidia-smi --query-compute-apps timestamp,gpu_name,pid,process_name,used_memory
    timestamp, gpu_name, pid, process_name, used_gpu_memory [MiB]
    2025/09/07 22:33:21.481, NVIDIA GeForce RTX 3090, 1106871, /app/llama-server, 23342 MiB

    $ nvidia-smi --query-compute-apps timestamp,gpu_name,pid,process_name,used_memory --format=csv,noheader,nounits
    2025/09/07 22:37:17.908, NVIDIA GeForce RTX 3090, 1106871, /app/llama-server, 23342
*/
const std::string NVIDIA_SMI_QUERY_PROCS =
    "nvidia-smi --query-compute-apps timestamp,gpu_name,pid,process_name,used_memory --format=csv,noheader,nounits";

/*
    == GPU Stats
    $ nvidia-smi --query-gpu=index,name,utilization.gpu,utilization.memory,memory.total,memory.free,memory.used,temperature.gpu,clocks.gr,power.draw,power.limit,pstate,clocks_event_reasons.active
    index, name, utilization.gpu [%], utilization.memory [%], memory.total [MiB], memory.free [MiB], memory.used [MiB], temperature.gpu, clocks.current.graphics [MHz], power.draw [W], power.limit [W], pstate, clocks_event_reasons.active

    $ nvidia-smi --query-gpu=... --format=csv,noheader,nounits
    0, NVIDIA GeForce RTX 3090, 0, 17, 24576, 767, 23358, 29, 1815, 118.42, 350.00, P2, 0x0000000000000001

    The last five are OPTIONAL in the published JSON. They are not reported
    on every card or driver -- a laptop GPU has no readable power limit, and
    older drivers spell the reasons field clocks_throttle_reasons.active --
    so each is emitted only when it parses as the type it should be. A client
    that has never heard of them is unaffected; one that wants them can tell
    "absent" from "zero", which matters for a gauge that must not invent a
    reading it does not have.
*/
const std::string NVIDIA_SMI_QUERY_STATS =
    "nvidia-smi --query-gpu=index,name,utilization.gpu,utilization.memory,memory.total,memory.free,memory.used,temperature.gpu,clocks.gr,power.draw,power.limit,pstate,clocks_event_reasons.active --format=csv,noheader,nounits";

std::regex csv_regex(R"(^(\d+),\s*([^,]+?),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*([^,]+?),\s*([^,]+?),\s*([^,]+?),\s*([^,]+?),\s*([^,]+?)$)");

// The optional fields arrive as "[N/A]" where the card cannot report them,
// so each is published only when it looks like what it claims to be.
bool is_uint(const std::string &s)
{
    return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
}

bool is_decimal(const std::string &s)
{
    if (s.empty() || s.find_first_not_of("0123456789.") != std::string::npos)
        return false;
    return std::count(s.begin(), s.end(), '.') <= 1 && s != ".";
}

// "P0".."P15"
bool is_pstate(const std::string &s)
{
    return s.size() >= 2 && s.size() <= 3 && s[0] == 'P' && is_uint(s.substr(1));
}

bool is_hex_mask(const std::string &s)
{
    return s.size() > 2 && s.rfind("0x", 0) == 0 &&
           s.find_first_not_of("0123456789abcdefABCDEF", 2) == std::string::npos;
}

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

            std::string obj =
                "{\"index\":" + match[1].str() +
                ",\"name\":\"" + name + "\"" +
                ",\"utilization.gpu\":" + match[3].str() +
                ",\"utilization.memory\":" + match[4].str() +
                ",\"memory.total\":" + match[5].str() +
                ",\"memory.free\":" + match[6].str() +
                ",\"memory.used\":" + match[7].str() +
                ",\"temperature.gpu\":" + match[8].str();

            if (is_uint(match[9].str()))
                obj += ",\"clocks.gr\":" + match[9].str();
            if (is_decimal(match[10].str()))
                obj += ",\"power.draw\":" + match[10].str();
            if (is_decimal(match[11].str()))
                obj += ",\"power.limit\":" + match[11].str();
            if (is_pstate(match[12].str()))
                obj += ",\"pstate\":\"" + match[12].str() + "\"";
            if (is_hex_mask(match[13].str()))
                obj += ",\"clocks_event_reasons.active\":\"" + match[13].str() + "\"";

            result.push_back(obj + "}");
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
    }

    void setupTimer()
    {
        // uWS is single-threaded: publish() may only be called on the loop
        // thread, so the poller thread hands each payload over via defer().
        uWS::Loop *loop = uWS::Loop::get();
        std::thread([this, loop]()
                    {
            while (true) {
                std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
                std::string output = exec_command(NVIDIA_SMI_QUERY_STATS);
                auto parsed = parse_nvidia_smi_output(output);
                std::string json = to_json_array(parsed);
                loop->defer([this, json]()
                {
                    app.publish("gpu_live", json, uWS::OpCode::TEXT);
                });
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
#ifdef _WIN32
    // Status output contains UTF-8 emoji; the default console codepage mangles them
    SetConsoleOutputCP(CP_UTF8);
#endif
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