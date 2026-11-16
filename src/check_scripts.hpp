// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#pragma once

#include <string>

namespace gh {

// godshand --check-scripts <folder>: every Lua script (and the runHaxeCode
// blocks in it), HScript file and shader under the folder is read as a song
// would read it; failures are printed with their reason. 0 when all are fine.
int checkScripts(const std::string& dir);

}  // namespace gh
