#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <regex>
#include <algorithm>
#include <fmt/core.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

// Same parsing functions from the main code
std::regex csv_regex(R"((\d+),\s*([^,]+?),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+))");

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

int main() {
    std::string test_data = "0, NVIDIA GeForce RTX 3080, 0, 0, 10240, 407, 9467, 40";
    std::cout << "Testing with: " << test_data << std::endl;
    
    // Test regex directly
    std::smatch match;
    if (std::regex_match(test_data, match, csv_regex)) {
        std::cout << "Regex matched!" << std::endl;
        for (size_t i = 0; i < match.size(); ++i) {
            std::cout << "Match " << i << ": '" << match[i].str() << "'" << std::endl;
        }
    } else {
        std::cout << "Regex did NOT match!" << std::endl;
        // Try with spaces removed
        std::string no_spaces = test_data;
        no_spaces.erase(std::remove(no_spaces.begin(), no_spaces.end(), ' '), no_spaces.end());
        std::cout << "Without spaces: " << no_spaces << std::endl;
        if (std::regex_match(no_spaces, match, csv_regex)) {
            std::cout << "Matches without spaces!" << std::endl;
        }
    }
    
    auto parsed = parse_nvidia_smi_output(test_data);
    std::string json = to_json_array(parsed);
    
    std::cout << "Parsed JSON: " << json << std::endl;
    return 0;
}