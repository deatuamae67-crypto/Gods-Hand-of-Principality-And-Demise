// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Installs mods dropped into mods/ as .zip files: each archive is unpacked
// into its own folder (a single top-level folder is kept as the mod name) and
// then moved to mods/.installed/. Entries that would escape the folder are
// skipped.
#pragma once

#include <functional>
#include <string>

namespace gh {

// Called with the archive name and how far along it is (0..1).
using InstallProgress = std::function<void(const std::string& zip, double fraction)>;

int installModZips(const std::string& modsDir, std::string& report, const InstallProgress& progress = {});

}  // namespace gh
