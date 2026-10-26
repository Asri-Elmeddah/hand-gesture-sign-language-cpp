#pragma once

#include <map>
#include <string>

class EnvLoader {
public:
    explicit EnvLoader(std::string path);

    bool load();
    std::string get(const std::string& key, const std::string& fallback = "") const;
    bool has(const std::string& key) const;

private:
    std::string path_;
    std::map<std::string, std::string> values_;
};
