#pragma once
#include <string>
#include <vector>
#include <map>

// Set at build time by the compiler — where mpp.lua lives
#ifndef MPP_LUA_MODULE_DIR
#define MPP_LUA_MODULE_DIR "."
#endif

namespace mpp {

void initBridge(
    std::map<std::string, std::string>& vars,
    const std::string& bridgePath
);

void say(const std::string& text);
void wait(double seconds);
std::string numToStr(double value);
double toNum(const std::string& s);
void whats(std::string& target);

void runLua(const std::string& path);

void luaCall(
    const std::string& path,
    const std::string& funcName,
    const std::vector<std::string>& args
);

void luaExec(const std::string& expr);

}
