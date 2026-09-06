#include "runtime.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cmath>
#include <thread>
#include <chrono>

namespace mpp {

// ============================================================
// Global variable store (shared with Lua via temp file)
// ============================================================

static std::map<std::string, std::string>* g_vars = nullptr;
static std::string g_bridge_path;


void initBridge(
    std::map<std::string, std::string>& vars,
    const std::string& bridgePath
)
{
    g_vars = &vars;
    g_bridge_path = bridgePath;
}


// ============================================================
// Write mpp_vars to bridge file
// Format:  key=value\n  (values are base64-ish: \n -> \n)
// We use a simple escape: newlines in values become \n
// ============================================================

static std::string escapeVal(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static std::string unescapeVal(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            if (s[i+1] == 'n')  { out += '\n'; i++; }
            else if (s[i+1] == '\\') { out += '\\'; i++; }
            else out += s[i];
        }
        else out += s[i];
    }
    return out;
}

static void writeBridge()
{
    if (!g_vars || g_bridge_path.empty()) return;

    std::ofstream f(g_bridge_path);
    for (const auto& pair : *g_vars)
    {
        f << escapeVal(pair.first)
          << "="
          << escapeVal(pair.second)
          << "\n";
    }
}

static void readBridge()
{
    if (!g_vars || g_bridge_path.empty()) return;

    std::ifstream f(g_bridge_path);
    if (!f) return;

    std::string line;
    while (std::getline(f, line))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key =
            unescapeVal(line.substr(0, eq));

        std::string val =
            unescapeVal(line.substr(eq + 1));

        (*g_vars)[key] = val;
    }
}


// ============================================================
// say
// ============================================================

void say(const std::string& text)
{
    std::cout << text << '\n';
}


// ============================================================
// wait
// ============================================================

void wait(double seconds)
{
    auto ms = static_cast<long long>(seconds * 1000.0);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(ms)
    );
}


// ============================================================
// numToStr
// ============================================================

std::string numToStr(double value)
{
    if (value == std::floor(value) &&
        std::abs(value) < 1e15)
    {
        return std::to_string(
            static_cast<long long>(value)
        );
    }

    std::ostringstream oss;
    oss << std::setprecision(6) << value;
    return oss.str();
}


// ============================================================
// toNum
// ============================================================

double toNum(const std::string& s)
{
    if (s.empty()) return 0.0;
    try { return std::stod(s); }
    catch (...) { return 0.0; }
}


// ============================================================
// whats
// ============================================================

void whats(std::string& target)
{
    std::getline(std::cin, target);
}


// ============================================================
// runLua — sync vars to bridge, run file, sync back
// ============================================================

void runLua(const std::string& path)
{
    writeBridge();

    std::string cmd =
        "lua \""
        + path
        + "\" \""
        + g_bridge_path
        + "\"";

    std::system(cmd.c_str());

    readBridge();
}


// ============================================================
// luaCall — call a specific Lua function in a file,
//           passing args as strings
// ============================================================

void luaCall(
    const std::string& path,
    const std::string& funcName,
    const std::vector<std::string>& args
)
{
    writeBridge();

    // Build a small Lua snippet that:
    // 1. requires the mpp bridge module
    // 2. loads the user's file
    // 3. calls the function with the args
    std::string snippet =
        "local _b=\""
        + g_bridge_path
        + "\"; "
        "package.path = package.path .. \";\" .. "
        "\"" + MPP_LUA_MODULE_DIR + "/?.lua\"; "
        "require(\"mpp\")._init(_b); "
        "dofile(\"" + path + "\"); "
        + funcName + "(";

    for (size_t i = 0; i < args.size(); i++)
    {
        if (i > 0) snippet += ", ";
        snippet += "\"" + args[i] + "\"";
    }

    snippet += ")";

    std::string cmd =
        "lua -e '" + snippet + "'";

    std::system(cmd.c_str());

    readBridge();
}


// ============================================================
// luaExec — run an arbitrary Lua expression
// ============================================================

void luaExec(const std::string& expr)
{
    writeBridge();

    std::string snippet =
        "local _b=\""
        + g_bridge_path
        + "\"; "
        "package.path = package.path .. \";\" .. "
        "\"" + MPP_LUA_MODULE_DIR + "/?.lua\"; "
        "require(\"mpp\")._init(_b); "
        + expr;

    std::string cmd =
        "lua -e '" + snippet + "'";

    std::system(cmd.c_str());

    readBridge();
}

} // namespace mpp
