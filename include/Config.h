// include/Config.h
#pragma once
#include <filesystem>
#include <string>
#include <map>

class Config {
public:
    static Config& Instance();

    bool Load(const std::filesystem::path& path);

    std::string Get(const std::string& section, const std::string& key,
        const std::string& def = "") const;
    int GetInt(const std::string& section, const std::string& key, int def = 0) const;
    bool GetBool(const std::string& section, const std::string& key, bool def = false) const;

private:
    Config() = default;
    std::map<std::string, std::map<std::string, std::string>> data_;
};