// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The device's best parts for the game, and the system told that a game runs:
//  - the fast graphics card of a laptop with two (NVIDIA Optimus and AMD
//    switchable graphics on Windows, PRIME render offload on Linux);
//  - Windows: the game thread in the "Games" class of the multimedia
//    scheduler, the process above normal priority;
//  - Linux: Feral GameMode, when installed (the performance CPU governor and
//    the scheduling games get);
//  - Android: a performance hint session (ADPF), so the system raises the
//    processor's speed as soon as frames need it, and Game Mode (manifest);
//  - macOS: the games category in the program's Info.plist (Game Mode).
#pragma once

#include <string>

namespace gh::device {

// Before the window and its OpenGL context are made (graphics card choice).
void beforeWindow();
// Once the window exists: register the game with the system.
void startGame(double targetFrameSeconds);
// Each frame: how long the game worked on it (update and draw, without the
// wait for the screen), for Android's performance hints.
void frameWork(double seconds, double targetFrameSeconds);
void stopGame();
// What was set up, for the FPS counter's hardware lines.
const std::string& status();

}  // namespace gh::device
