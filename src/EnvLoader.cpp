#include "EnvLoader.hpp"

#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const std::size_t begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return "";
    }
    const std::size_t end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

}

EnvLoader::EnvLoader(std::string path) : path_(std::move(path)) {}

bool EnvLoader::load() {
    std::ifstream file(path_);
    if (!file.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = stripQuotes(trim(trimmed.substr(eq + 1)));
        if (!key.empty()) {
            values_[key] = value;
        }
    }
    return true;
}

std::string EnvLoader::get(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    return it != values_.end() ? it->second : fallback;
}

bool EnvLoader::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}
