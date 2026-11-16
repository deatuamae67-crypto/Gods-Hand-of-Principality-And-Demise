// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// godshand --check-scripts <folder>: reads every Lua script, HScript file,
// runHaxeCode block and shader under a folder the way a song would, without
// playing anything, and prints the ones that fail and why. It is how the
// engine is checked against whole mod packs.

#include "check_scripts.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "common.hpp"
#include "hscript.hpp"
#include "lua_host.hpp"
#include "lauxlib.h"
#include "lua.h"
#include "raylib.h"
#include "rlgl.h"
#include "scene.hpp"
#include "shader_glsl.hpp"

namespace gh {
namespace {

namespace fs = std::filesystem;

std::string pathStr(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string* g_log = nullptr;
void captureLog(int level, const char* text, va_list args) {
    if (!g_log || level < LOG_WARNING) return;
    char buf[1024];
    std::vsnprintf(buf, sizeof buf, text, args);
    *g_log += buf;
    *g_log += '\n';
}

// A Lua string literal starting at s[i] ('...', "..." or [[...]] / [=[...]=]):
// its contents, with i moved past it; false when s[i] starts none.
bool luaString(const std::string& s, size_t& i, std::string& out) {
    out.clear();
    if (i >= s.size()) return false;
    if (s[i] == '[') {
        size_t j = i + 1, eq = 0;
        while (j < s.size() && s[j] == '=') ++j, ++eq;
        if (j >= s.size() || s[j] != '[') return false;
        const std::string close = "]" + std::string(eq, '=') + "]";
        const size_t end = s.find(close, j + 1);
        if (end == std::string::npos) return false;
        size_t from = j + 1;
        if (from < s.size() && s[from] == '\n') ++from;  // Lua skips a first newline
        out = s.substr(from, end - from);
        i = end + close.size();
        return true;
    }
    if (s[i] != '"' && s[i] != '\'') return false;
    const char q = s[i];
    for (size_t j = i + 1; j < s.size(); ++j) {
        const char c = s[j];
        if (c == q) {
            i = j + 1;
            return true;
        }
        if (c == '\n') return false;
        if (c != '\\' || j + 1 >= s.size()) {
            out += c;
            continue;
        }
        const char e = s[++j];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '\n': out += '\n'; break;
            default: out += e; break;
        }
    }
    return false;
}

// The runHaxeCode / runHaxeFunction... bodies a Lua file gives as one plain
// string (built from pieces at run time: not checked).
std::vector<std::string> haxeBlocks(const std::string& lua) {
    std::vector<std::string> out;
    static const char* const kCalls[] = {"runHaxeCode", "runHaxeCodePost", "addHaxeCode"};
    for (const char* call : kCalls) {
        const std::string key = call;
        for (size_t p = lua.find(key); p != std::string::npos; p = lua.find(key, p + 1)) {
            if (p > 0 && (std::isalnum((unsigned char)lua[p - 1]) || lua[p - 1] == '_')) continue;
            size_t i = p + key.size();
            if (i < lua.size() && (std::isalnum((unsigned char)lua[i]) || lua[i] == '_')) continue;
            while (i < lua.size() && (lua[i] == ' ' || lua[i] == '\t')) ++i;
            if (i < lua.size() && lua[i] == '(') ++i;
            while (i < lua.size() && std::isspace((unsigned char)lua[i])) ++i;
            std::string body;
            size_t j = i;
            if (!luaString(lua, j, body)) continue;
            while (j < lua.size() && std::isspace((unsigned char)lua[j])) ++j;
            if (j < lua.size() && (lua[j] == '.' || lua[j] == '%')) continue;  // "..." .. x: built at run time
            out.push_back(std::move(body));
        }
    }
    return out;
}

struct Counts {
    int ok = 0, failed = 0;
};

}  // namespace

int checkScripts(const std::string& dir) {
    std::vector<fs::path> lua, hx, frag;
    std::error_code ec;
    const fs::path root(std::u8string(dir.begin(), dir.end()));
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = toLower(pathStr(it->path().extension()));
        if (ext == ".lua") lua.push_back(it->path());
        else if (ext == ".hx" || ext == ".hscript" || ext == ".hxs") hx.push_back(it->path());
        else if (ext == ".frag" || ext == ".fsh" || ext == ".glsl") frag.push_back(it->path());
    }
    std::sort(lua.begin(), lua.end());
    std::sort(hx.begin(), hx.end());
    std::sort(frag.begin(), frag.end());
    Counts luaC, hxC, blockC, glslC;
    int luaRepaired = 0;
    std::map<std::string, int> reasons;  // first words of each error, to see what fails most
    auto reason = [&](const std::string& kind, const std::string& err) {
        std::string r = err;
        const size_t colon = r.find(": ");
        if (colon != std::string::npos && colon < 120) r = r.substr(colon + 2);
        if (r.size() > 70) r.resize(70);
        ++reasons[kind + " " + r];
    };
    auto rel = [&](const fs::path& p) { return pathStr(p.lexically_relative(root)); };

    lua_State* L = luaL_newstate();
    for (const fs::path& p : lua) {
        std::string code;
        if (!scene::readFile(pathStr(p), code)) continue;
        if (code.size() >= 3 && (unsigned char)code[0] == 0xEF && (unsigned char)code[1] == 0xBB && (unsigned char)code[2] == 0xBF)
            code.erase(0, 3);
        const std::string name = "@" + rel(p);
        int fixes = 0;
        const std::string fixedCode = repairLuaEnds(L, code, name, &fixes);
        if (fixes > 0) {
            std::printf("FIX  %s: %d misplaced 'end' fixed\n", rel(p).c_str(), fixes);
            ++luaRepaired;
        }
        if (luaL_loadbufferx(L, fixedCode.data(), fixedCode.size(), name.c_str(), "t") != LUA_OK) {
            const std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
            std::printf("LUA  %s\n", err.c_str());
            reason("lua", err);
            ++luaC.failed;
        } else {
            ++luaC.ok;
        }
        lua_settop(L, 0);
        int n = 0;
        for (const std::string& block : haxeBlocks(code)) {
            ++n;
            std::string err;
            if (hx::Interp::checkSyntax(block, err)) {
                ++blockC.ok;
                continue;
            }
            ++blockC.failed;
            std::printf("HX   %s (runHaxeCode #%d): %s\n", rel(p).c_str(), n, err.c_str());
            reason("runHaxeCode", err);
        }
    }
    lua_close(L);

    int engineSources = 0;
    for (const fs::path& p : hx) {
        std::string code, err;
        if (!scene::readFile(pathStr(p), code)) continue;
        // An engine's own source code or build files (some packs carry them),
        // not a script: songs never load these.
        const std::string where = "/" + rel(p);
        if (code.find("\npackage ") != std::string::npos || code.rfind("package ", 0) == 0 ||
            where.find("/source/") != std::string::npos || where.find("/export/") != std::string::npos ||
            code.find("\nclass ") != std::string::npos || code.rfind("class ", 0) == 0) {
            ++engineSources;
            continue;
        }
        if (hx::Interp::checkSyntax(code, err)) {
            ++hxC.ok;
            continue;
        }
        ++hxC.failed;
        std::printf("HX   %s: %s\n", rel(p).c_str(), err.c_str());
        reason("hx", err);
    }

    if (!frag.empty()) {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(64, 64, "check-scripts");
        const bool gles = rlGetVersion() == RL_OPENGL_ES_20;
        for (const fs::path& p : frag) {
            std::string code;
            if (!scene::readFile(pathStr(p), code)) continue;
            const std::string glsl = translateShader(code, gles);
            std::string log;
            g_log = &log;
            SetTraceLogCallback(captureLog);
            const Shader sh = LoadShaderFromMemory(nullptr, glsl.c_str());
            SetTraceLogCallback(nullptr);
            g_log = nullptr;
            const bool ok = sh.id != 0 && sh.id != rlGetShaderIdDefault();
            if (ok) {
                UnloadShader(sh);
                ++glslC.ok;
                continue;
            }
            ++glslC.failed;
            std::string first;
            size_t e = log.find("ERROR");
            if (e == std::string::npos) e = log.find("error");
            if (e != std::string::npos) first = log.substr(e, log.find('\n', e) - e);
            std::printf("GLSL %s: %s\n", rel(p).c_str(), first.empty() ? "does not compile" : first.c_str());
            reason("glsl", first);
        }
        CloseWindow();
    }

    std::printf("\nLua files      %6d ok %6d failed (%d read after fixing an end)\n", luaC.ok, luaC.failed, luaRepaired);
    std::printf("runHaxeCode    %6d ok %6d failed\n", blockC.ok, blockC.failed);
    std::printf("HScript files  %6d ok %6d failed (%d engine source files skipped)\n", hxC.ok, hxC.failed, engineSources);
    std::printf("Shaders        %6d ok %6d failed\n", glslC.ok, glslC.failed);
    std::vector<std::pair<int, std::string>> top;
    for (const auto& [r, n] : reasons) top.push_back({n, r});
    std::sort(top.rbegin(), top.rend());
    if (!top.empty()) std::printf("\nMost common reasons:\n");
    for (size_t i = 0; i < top.size() && i < 40; ++i) std::printf("%6d  %s\n", top[i].first, top[i].second.c_str());
    return luaC.failed + hxC.failed + blockC.failed + glslC.failed > 0 ? 1 : 0;
}

}  // namespace gh
