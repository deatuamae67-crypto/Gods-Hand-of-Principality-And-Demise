// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// (No raylib here, so the system headers can be used as they are.)
#include "performance.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace gh::device {
namespace {

std::string g_status;

void addStatus(const std::string& s) {
    if (!g_status.empty()) g_status += ", ";
    g_status += s;
}

#if defined(__linux__) && !defined(__ANDROID__)
std::string readFirstLine(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::string line;
    std::getline(f, line);
    return line;
}

void* g_gamemode = nullptr;
#endif

#if defined(__ANDROID__)
// The NDK's performance hint API (Android 13, API 33), looked up at run time
// because the game also runs on Android 7.
struct HintManager;
struct HintSession;
using GetManagerFn = HintManager* (*)();
using CreateSessionFn = HintSession* (*)(HintManager*, const int32_t*, size_t, int64_t);
using ReportFn = int (*)(HintSession*, int64_t);
using UpdateTargetFn = int (*)(HintSession*, int64_t);
using CloseFn = void (*)(HintSession*);
HintSession* g_session = nullptr;
ReportFn g_report = nullptr;
UpdateTargetFn g_updateTarget = nullptr;
CloseFn g_close = nullptr;
int64_t g_targetNs = 0;
#endif

}  // namespace

void beforeWindow() {
#if defined(__linux__) && !defined(__ANDROID__)
    // A laptop with two graphics cards: render on the fast one, unless the
    // player chose already (these variables set) or asked not to.
    if (std::getenv("GH_NO_DGPU") || std::getenv("DRI_PRIME") || std::getenv("__NV_PRIME_RENDER_OFFLOAD")) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string bootVendor;
    bool nvidiaCard = false, amdCard = false;
    for (fs::directory_iterator it("/sys/class/drm", ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos) continue;
        const std::string vendor = readFirstLine(it->path() / "device" / "vendor");
        if (readFirstLine(it->path() / "device" / "boot_vga") == "1") bootVendor = vendor;
        else if (vendor == "0x10de") nvidiaCard = true;
        else if (vendor == "0x1002") amdCard = true;
    }
    if (bootVendor.empty() || bootVendor == "0x10de") return;  // one card, or the fast one already drives the screen
    if (nvidiaCard && fs::exists("/proc/driver/nvidia/version", ec)) {
        // NVIDIA's own driver: PRIME render offload.
        void* glx = dlopen("libGLX_nvidia.so.0", RTLD_LAZY | RTLD_LOCAL);
        if (!glx) return;
        dlclose(glx);
        setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0);
        setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0);
        addStatus("NVIDIA card (PRIME offload)");
    } else if (amdCard || nvidiaCard) {
        setenv("DRI_PRIME", "1", 0);  // Mesa: the second card
        addStatus("second graphics card (DRI_PRIME)");
    }
#endif
}

void startGame(double targetFrameSeconds) {
#if defined(_WIN32)
    // The multimedia class scheduler's "Games" task: the game thread is
    // scheduled as a game's, as Windows does for the games it knows.
    if (HMODULE avrt = LoadLibraryW(L"avrt.dll")) {
        using AvSetFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
        if (auto fn = reinterpret_cast<AvSetFn>(reinterpret_cast<void*>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW")))) {
            DWORD task = 0;
            if (fn(L"Games", &task)) addStatus("Windows game scheduling");
        }
    }
    if (SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) addStatus("above normal priority");
#elif defined(__ANDROID__)
    // Android's performance hints: the system learns how long each frame may
    // take and how long it took, and speeds the processor up in time.
    void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (lib) {
        auto getManager = reinterpret_cast<GetManagerFn>(dlsym(lib, "APerformanceHint_getManager"));
        auto create = reinterpret_cast<CreateSessionFn>(dlsym(lib, "APerformanceHint_createSession"));
        g_report = reinterpret_cast<ReportFn>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
        g_updateTarget = reinterpret_cast<UpdateTargetFn>(dlsym(lib, "APerformanceHint_updateTargetWorkDuration"));
        g_close = reinterpret_cast<CloseFn>(dlsym(lib, "APerformanceHint_closeSession"));
        if (getManager && create && g_report) {
            if (HintManager* m = getManager()) {
                const int32_t tid = int32_t(gettid());
                g_targetNs = int64_t((targetFrameSeconds > 0 ? targetFrameSeconds : 1.0 / 60.0) * 1e9);
                g_session = create(m, &tid, 1, g_targetNs);
                if (g_session) addStatus("Android performance hints");
            }
        }
    }
#elif defined(__linux__)
    // Feral GameMode, when installed: the performance governor and the
    // priorities it gives games.
    if ((g_gamemode = dlopen("libgamemode.so.0", RTLD_NOW | RTLD_LOCAL))) {
        using StartFn = int (*)();
        if (auto start = reinterpret_cast<StartFn>(dlsym(g_gamemode, "real_gamemode_request_start")); start && start() == 0)
            addStatus("GameMode");
    }
#endif
    (void)targetFrameSeconds;
}

void frameWork(double seconds, double targetFrameSeconds) {
#if defined(__ANDROID__)
    if (!g_session || !g_report) return;
    const int64_t target = int64_t((targetFrameSeconds > 0 ? targetFrameSeconds : 1.0 / 60.0) * 1e9);
    if (g_updateTarget && target != g_targetNs) {
        g_targetNs = target;
        g_updateTarget(g_session, target);
    }
    if (seconds > 0) g_report(g_session, int64_t(seconds * 1e9));
#else
    (void)seconds;
    (void)targetFrameSeconds;
#endif
}

void stopGame() {
#if defined(__ANDROID__)
    if (g_session && g_close) g_close(g_session);
    g_session = nullptr;
#elif defined(__linux__) && !defined(_WIN32)
    if (g_gamemode) {
        using EndFn = int (*)();
        if (auto end = reinterpret_cast<EndFn>(dlsym(g_gamemode, "real_gamemode_request_end"))) end();
    }
#endif
}

const std::string& status() { return g_status; }

}  // namespace gh::device

#if defined(_WIN32)
// Laptops with two graphics cards: NVIDIA Optimus and AMD switchable graphics
// run a program on the fast card when it exports these.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
