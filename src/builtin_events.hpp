// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// Chart events the engine itself knows: the ones Psych Engine and the engines
// mods are made for (JS Engine, Denpa, Dek, P-Slice, SCReboot...) have built
// in, so their mods ship no script for them. Some are done in C++
// (play_psych.cpp); the others are Lua scripts written against the Psych API,
// loaded like a mod's own custom_events script when a song uses the event and
// its mod has no script of that name.
#pragma once

#include <string_view>

namespace gh::builtin {

// The engine handles this event (in C++ or with a script of its own): another
// mod's custom_events script of the same name is not borrowed for it (it may
// mean something else there), though the song's own mod's script still wins.
bool engineEvent(std::string_view name);
// The engine's own script for the event, or nullptr when it is done in C++
// (or not at all).
const char* eventLua(std::string_view name);

}  // namespace gh::builtin
