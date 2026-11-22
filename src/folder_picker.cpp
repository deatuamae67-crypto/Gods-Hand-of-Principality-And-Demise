// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The system's "choose a folder" and "choose files" dialogs on desktops. Kept
// apart from the rest of the platform code: windows.h and raylib.h do not mix.
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <algorithm>
#include <cwchar>
#elif !defined(__ANDROID__)
#include <cctype>
#include <cstdio>
#endif

namespace gh::platform {

#if defined(_WIN32)
std::string pickFolderNative(const std::string& title) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring wt(title.size() + 1, L'\0');
    wt.resize(size_t(MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wt.data(), int(wt.size()))));
    BROWSEINFOW bi{};
    bi.lpszTitle = wt.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    std::string out;
    if (PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi)) {
        wchar_t path[MAX_PATH * 4] = {};
        if (SHGetPathFromIDListW(pidl, path)) {
            const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                out.resize(size_t(n));
                WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), n, nullptr, nullptr);
                out.resize(size_t(n - 1));
            }
        }
        CoTaskMemFree(pidl);
    }
    if (SUCCEEDED(co)) CoUninitialize();
    for (char& c : out)
        if (c == '\\') c = '/';
    return out;
}

namespace {
std::string narrow(const wchar_t* w, size_t n) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, int(n), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(len, 0)), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, int(n), out.data(), len, nullptr, nullptr);
    for (char& c : out)
        if (c == '\\') c = '/';
    return out;
}
}  // namespace

std::vector<std::string> pickFilesNative(const std::string& title, const std::string& pattern) {
    std::wstring wt(title.size() + 1, L'\0');
    wt.resize(size_t(MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wt.data(), int(wt.size()))));
    std::wstring wp(pattern.size() + 1, L'\0');
    wp.resize(size_t(MultiByteToWideChar(CP_UTF8, 0, pattern.c_str(), -1, wp.data(), int(wp.size()))));
    if (!wp.empty() && wp.back() == L'\0') wp.pop_back();
    // "Charts (*.json)\0*.json\0All files\0*.*\0\0"
    std::wstring filter = L"Charts (" + wp + L")";
    filter.push_back(L'\0');
    filter += wp;
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    std::vector<wchar_t> buf(size_t(1) << 20, L'\0');  // room for thousands of names
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = DWORD(buf.size());
    ofn.lpstrTitle = wt.c_str();
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    std::vector<std::string> out;
    if (!GetOpenFileNameW(&ofn)) return out;
    // One file: its path. Several: the folder, then each name; an empty
    // string ends the list.
    std::vector<std::wstring> parts;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) parts.emplace_back(p);
    if (parts.size() == 1) {
        out.push_back(narrow(parts[0].c_str(), parts[0].size()));
    } else {
        for (size_t i = 1; i < parts.size(); ++i) {
            std::wstring full = parts[0];
            if (!full.empty() && full.back() != L'\\') full += L'\\';
            full += parts[i];
            out.push_back(narrow(full.c_str(), full.size()));
        }
    }
    return out;
}
#elif defined(__ANDROID__)
std::string pickFolderNative(const std::string&) { return {}; }
std::vector<std::string> pickFilesNative(const std::string&, const std::string&) { return {}; }
#else
namespace {
std::string run(const std::string& cmd) {
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[4096];
        while (std::fgets(buf, sizeof buf, p)) out += buf;
        pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}
}  // namespace

std::string pickFolderNative(const std::string& title) {
    std::string safe;
    for (char c : title)
        if (c != '"' && c != '\'' && c != '\\' && c != '`' && c != '$') safe += c;
#if defined(__APPLE__)
    return run("osascript -e 'POSIX path of (choose folder with prompt \"" + safe + "\")' 2>/dev/null");
#else
    std::string out = run("zenity --file-selection --directory --title=\"" + safe + "\" 2>/dev/null");
    if (out.empty()) out = run("kdialog --getexistingdirectory . --title \"" + safe + "\" 2>/dev/null");
    return out;
#endif
}

std::vector<std::string> pickFilesNative(const std::string& title, const std::string& pattern) {
    std::string safe, pat;
    for (char c : title)
        if (c != '"' && c != '\'' && c != '\\' && c != '`' && c != '$') safe += c;
    for (char c : pattern)
        if (c == '*' || c == '.' || c == '_' || c == '-' || std::isalnum(static_cast<unsigned char>(c))) pat += c;
#if defined(__APPLE__)
    // One POSIX path a line.
    const std::string out = run("osascript -e 'set fs to choose file with prompt \"" + safe +
                                "\" with multiple selections allowed' -e 'set o to \"\"' -e 'repeat with f in fs' "
                                "-e 'set o to o & POSIX path of f & linefeed' -e 'end repeat' -e 'return o' 2>/dev/null");
#else
    // zenity puts its separator (a new line, inside the quotes) between the
    // names; kdialog writes one a line.
    std::string out = run("zenity --file-selection --multiple --separator=\"\n\" --title=\"" + safe +
                          "\" --file-filter=\"" + pat + "\" --file-filter=\"*\" 2>/dev/null");
    if (out.empty())
        out = run("kdialog --getopenfilename . \"" + pat + "\" --multiple --separate-output --title \"" + safe + "\" 2>/dev/null");
#endif
    std::vector<std::string> files;
    size_t b = 0;
    while (b < out.size()) {
        size_t e = out.find('\n', b);
        if (e == std::string::npos) e = out.size();
        std::string line = out.substr(b, e - b);
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) files.push_back(line);
        b = e + 1;
    }
    return files;
}
#endif

}  // namespace gh::platform
