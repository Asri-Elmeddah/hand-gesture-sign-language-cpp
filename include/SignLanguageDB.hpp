#pragma once

#include "HandProcessor.hpp"

#include <string>
#include <vector>

struct GestureSpec {
    std::string name;
    std::string phrase;
    int fingers{0};
    double orientMin{0.0};
    double orientMax{180.0};
    double solidityMin{0.0};
    double solidityMax{1.01};
    double aspectMin{0.0};
    double aspectMax{10.0};
    bool custom{false};
};

class SignLanguageDB {
public:
    explicit SignLanguageDB(std::string path);

    bool load();
    bool save() const;

    void addCustom(const GestureSpec& spec);

    GestureSpec match(const HandAnalysisResult& features) const;

    const std::vector<GestureSpec>& gestures() const;
    std::size_t size() const;

private:
    void ensureDefaults();
    bool orientInRange(double orient, const GestureSpec& spec) const;

    std::string path_;
    std::vector<GestureSpec> gestures_;
    int customCounter_{0};
};
