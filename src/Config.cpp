// src/Config.cpp
#include "Config.h"
#include <fstream>
#include <sstream>
#include <algorithm>

Config& Config::Instance() {
    static Config inst;
    return inst;
}

bool Config::Load(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;

    std::string line, section;
    while (std::getline(ifs, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);

        data_[section][key] = val;
    }
    return true;
}

std::string Config::Get(const std::string& section, const std::string& key,
    const std::string& def) const {
    auto it = data_.find(section);
    if (it == data_.end()) return def;
    auto it2 = it->second.find(key);
    return it2 == it->second.end() ? def : it2->second;
}

int Config::GetInt(const std::string& section, const std::string& key, int def) const {
    std::string v = Get(section, key);
    if (v.empty()) return def;
    try { return std::stoi(v); }
    catch (...) { return def; }
}

bool Config::GetBool(const std::string& section, const std::string& key, bool def) const {
    std::string v = Get(section, key);
    if (v.empty()) return def;
    std::string low = v;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (low == "true" || low == "1" || low == "yes") return true;
    if (low == "false" || low == "0" || low == "no") return false;
    return def;
}