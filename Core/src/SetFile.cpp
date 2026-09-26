/**
 * @file SetFile.cpp
 * @brief The `.ephset` text form.
 */
#include "eph/SetFile.h"
#include "eph/Params.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace eph {

bool saveSet(const char* path, const SetFile& set, const ParamStore& params)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "# Ephemeris set\n";
    f << "seed=" << set.seed << "\n";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", set.minutes);
    f << "minutes=" << buf << "\n";
    std::snprintf(buf, sizeof(buf), "%.9g", set.concert);
    f << "concert=" << buf << "\n";
    f << "sounds=knobs\n";   // the parameters hold the composer's sounds (SetFile::soundsInParams)
    for (const auto& r : set.curation.rerolls)
        if (r.second != 0) f << "reroll " << r.first << "=" << r.second << "\n";
    std::istringstream lines(params.toText(true));
    std::string line;
    while (std::getline(lines, line)) if (!line.empty()) f << "param " << line << "\n";
    return static_cast<bool>(f);
}

bool loadSet(const char* path, SetFile& set, ParamStore& params, std::string* error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = std::string("cannot read ") + path; return false; }
    set = SetFile{};
    std::string line, paramText;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto value = [&](size_t from) { return line.substr(from); };
        if (line.rfind("seed=", 0) == 0) set.seed = std::strtoull(value(5).c_str(), nullptr, 10);
        else if (line.rfind("minutes=", 0) == 0) set.minutes = std::atof(value(8).c_str());
        else if (line.rfind("concert=", 0) == 0) set.concert = std::atof(value(8).c_str());
        else if (line.rfind("sounds=", 0) == 0) set.soundsInParams = value(7) == "knobs";
        else if (line.rfind("reroll ", 0) == 0) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) { if (error) *error = "line " + std::to_string(n) + ": reroll without '='"; return false; }
            set.curation.rerolls[line.substr(7, eq - 7)] = std::atoi(value(eq + 1).c_str());
        } else if (line.rfind("param ", 0) == 0) paramText += value(6) + "\n";
        else { if (error) *error = "line " + std::to_string(n) + ": not understood"; return false; }
    }
    params.resetDefaults();
    set.params = paramText;
    return params.parseText(paramText, error);
}

} // namespace eph
