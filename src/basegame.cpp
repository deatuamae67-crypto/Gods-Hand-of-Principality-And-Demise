// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

// The base game's character offsets and stage layouts below follow Friday Night
// Funkin' (The Funkin' Crew), used under the Apache License 2.0
// (licenses/Apache-2.0.txt); the images themselves come from the mods.
#include "basegame.hpp"

#include <cstring>

#include "common.hpp"

namespace gh::basegame {

namespace {

struct Named {
    const char* name;
    const char* text;
};

// Offsets and prefixes follow the original game's Character.hx (the same
// values Psych Engine ships in its character JSONs).
const Named kCharacters[] = {
    {"bf", R"({"image": "characters/BOYFRIEND", "position": [0, 350], "camera_position": [0, 0], "healthicon": "bf",
  "flip_x": true, "healthbar_colors": [49, 176, 209], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "BF idle dance", "fps": 24, "loop": false, "offsets": [-5, 0]},
  {"anim": "singLEFT", "name": "BF NOTE LEFT0", "fps": 24, "loop": false, "offsets": [12, -6]},
  {"anim": "singDOWN", "name": "BF NOTE DOWN0", "fps": 24, "loop": false, "offsets": [-10, -50]},
  {"anim": "singUP", "name": "BF NOTE UP0", "fps": 24, "loop": false, "offsets": [-29, 27]},
  {"anim": "singRIGHT", "name": "BF NOTE RIGHT0", "fps": 24, "loop": false, "offsets": [-38, -7]},
  {"anim": "singLEFTmiss", "name": "BF NOTE LEFT MISS", "fps": 24, "loop": false, "offsets": [12, 24]},
  {"anim": "singDOWNmiss", "name": "BF NOTE DOWN MISS", "fps": 24, "loop": false, "offsets": [-11, -19]},
  {"anim": "singUPmiss", "name": "BF NOTE UP MISS", "fps": 24, "loop": false, "offsets": [-29, 27]},
  {"anim": "singRIGHTmiss", "name": "BF NOTE RIGHT MISS", "fps": 24, "loop": false, "offsets": [-30, 21]},
  {"anim": "hey", "name": "BF HEY", "fps": 24, "loop": false, "offsets": [7, 4]},
  {"anim": "hurt", "name": "BF hit", "fps": 24, "loop": false, "offsets": [14, 18]},
  {"anim": "scared", "name": "BF idle shaking", "fps": 24, "loop": true, "offsets": [-4, 0]},
  {"anim": "dodge", "name": "boyfriend dodge", "fps": 24, "loop": false, "offsets": [-10, -16]},
  {"anim": "attack", "name": "boyfriend attack", "fps": 24, "loop": false, "offsets": [294, 267]},
  {"anim": "pre-attack", "name": "bf pre attack", "fps": 24, "loop": false, "offsets": [-40, -40]}]})"},
    {"bf-car", R"({"image": "characters/bfCar", "position": [0, 350], "camera_position": [0, 0], "healthicon": "bf",
  "flip_x": true, "healthbar_colors": [49, 176, 209], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "BF idle dance", "fps": 24, "loop": false, "offsets": [-5, 0]},
  {"anim": "singLEFT", "name": "BF NOTE LEFT0", "fps": 24, "loop": false, "offsets": [12, -6]},
  {"anim": "singDOWN", "name": "BF NOTE DOWN0", "fps": 24, "loop": false, "offsets": [-10, -50]},
  {"anim": "singUP", "name": "BF NOTE UP0", "fps": 24, "loop": false, "offsets": [-29, 27]},
  {"anim": "singRIGHT", "name": "BF NOTE RIGHT0", "fps": 24, "loop": false, "offsets": [-38, -7]},
  {"anim": "singLEFTmiss", "name": "BF NOTE LEFT MISS", "fps": 24, "loop": false, "offsets": [12, 24]},
  {"anim": "singDOWNmiss", "name": "BF NOTE DOWN MISS", "fps": 24, "loop": false, "offsets": [-11, -19]},
  {"anim": "singUPmiss", "name": "BF NOTE UP MISS", "fps": 24, "loop": false, "offsets": [-29, 27]},
  {"anim": "singRIGHTmiss", "name": "BF NOTE RIGHT MISS", "fps": 24, "loop": false, "offsets": [-30, 21]}]})"},
    {"bf-christmas", R"({"image": "characters/bfChristmas", "position": [0, 350], "camera_position": [0, 0], "healthicon": "bf",
  "flip_x": true, "healthbar_colors": [49, 176, 209], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "BF idle dance", "fps": 24, "loop": false, "offsets": [-5, 0]},
  {"anim": "singLEFT", "name": "BF NOTE LEFT0", "fps": 24, "loop": false, "offsets": [12, -6]},
  {"anim": "singDOWN", "name": "BF NOTE DOWN0", "fps": 24, "loop": false, "offsets": [-10, -50]},
  {"anim": "singUP", "name": "BF NOTE UP0", "fps": 24, "loop": false, "offsets": [-29, 27]},
  {"anim": "singRIGHT", "name": "BF NOTE RIGHT0", "fps": 24, "loop": false, "offsets": [-38, -7]},
  {"anim": "singLEFTmiss", "name": "BF NOTE LEFT MISS", "fps": 24, "loop": false, "offsets": [12, 24]},
  {"anim": "singDOWNmiss", "name": "BF NOTE DOWN MISS", "fps": 24, "loop": false, "offsets": [-11, -19]},
  {"anim": "singUPmiss", "name": "BF NOTE UP MISS", "fps": 24, "loop": false, "offsets": [-29, 27]},
  {"anim": "singRIGHTmiss", "name": "BF NOTE RIGHT MISS", "fps": 24, "loop": false, "offsets": [-30, 21]},
  {"anim": "hey", "name": "BF HEY", "fps": 24, "loop": false, "offsets": [7, 4]}]})"},
    {"bf-pixel", R"({"image": "characters/bfPixel", "position": [0, 350], "camera_position": [0, 0], "healthicon": "bf-pixel",
  "flip_x": true, "no_antialiasing": true, "healthbar_colors": [123, 214, 246], "sing_duration": 4, "scale": 6, "animations": [
  {"anim": "idle", "name": "BF IDLE", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "BF LEFT NOTE", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singDOWN", "name": "BF DOWN NOTE", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singUP", "name": "BF UP NOTE", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singRIGHT", "name": "BF RIGHT NOTE", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFTmiss", "name": "BF LEFT MISS", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singDOWNmiss", "name": "BF DOWN MISS", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singUPmiss", "name": "BF UP MISS", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singRIGHTmiss", "name": "BF RIGHT MISS", "fps": 24, "loop": false, "offsets": [0, 0]}]})"},
    {"gf", R"({"image": "characters/GF_assets", "position": [0, 0], "camera_position": [0, 0], "healthicon": "gf",
  "flip_x": false, "healthbar_colors": [165, 0, 77], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "cheer", "name": "GF Cheer", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "GF left note", "fps": 24, "loop": false, "offsets": [0, -19]},
  {"anim": "singDOWN", "name": "GF Down Note", "fps": 24, "loop": false, "offsets": [0, -20]},
  {"anim": "singUP", "name": "GF Up Note", "fps": 24, "loop": false, "offsets": [0, 4]},
  {"anim": "singRIGHT", "name": "GF Right Note", "fps": 24, "loop": false, "offsets": [0, -20]},
  {"anim": "sad", "name": "gf sad", "fps": 24, "loop": false, "offsets": [-2, -2], "indices": [0,1,2,3,4,5,6,7,8,9,10,11,12]},
  {"anim": "danceLeft", "name": "GF Dancing Beat", "fps": 24, "loop": false, "offsets": [0, -9], "indices": [30,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14]},
  {"anim": "danceRight", "name": "GF Dancing Beat", "fps": 24, "loop": false, "offsets": [0, -9], "indices": [15,16,17,18,19,20,21,22,23,24,25,26,27,28,29]},
  {"anim": "hairBlow", "name": "GF Dancing Beat Hair blowing", "fps": 24, "loop": true, "offsets": [45, -8], "indices": [0,1,2,3]},
  {"anim": "hairFall", "name": "GF Dancing Beat Hair Landing", "fps": 24, "loop": false, "offsets": [0, -9], "indices": [0,1,2,3,4,5,6,7,8,9,10,11]},
  {"anim": "scared", "name": "GF FEAR", "fps": 24, "loop": true, "offsets": [-2, -17]}]})"},
    {"gf-christmas", R"({"image": "characters/gfChristmas", "position": [0, 0], "camera_position": [0, 0], "healthicon": "gf",
  "flip_x": false, "healthbar_colors": [165, 0, 77], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "cheer", "name": "GF Cheer", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "GF left note", "fps": 24, "loop": false, "offsets": [0, -19]},
  {"anim": "singDOWN", "name": "GF Down Note", "fps": 24, "loop": false, "offsets": [0, -20]},
  {"anim": "singUP", "name": "GF Up Note", "fps": 24, "loop": false, "offsets": [0, 4]},
  {"anim": "singRIGHT", "name": "GF Right Note", "fps": 24, "loop": false, "offsets": [0, -20]},
  {"anim": "sad", "name": "gf sad", "fps": 24, "loop": false, "offsets": [-2, -21], "indices": [0,1,2,3,4,5,6,7,8,9,10,11,12]},
  {"anim": "danceLeft", "name": "GF Dancing Beat", "fps": 24, "loop": false, "offsets": [0, -9], "indices": [30,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14]},
  {"anim": "danceRight", "name": "GF Dancing Beat", "fps": 24, "loop": false, "offsets": [0, -9], "indices": [15,16,17,18,19,20,21,22,23,24,25,26,27,28,29]},
  {"anim": "hairBlow", "name": "GF Dancing Beat Hair blowing", "fps": 24, "loop": true, "offsets": [45, -8], "indices": [0,1,2,3]},
  {"anim": "hairFall", "name": "GF Dancing Beat Hair Landing", "fps": 24, "loop": false, "offsets": [0, -9], "indices": [0,1,2,3,4,5,6,7,8,9,10,11]},
  {"anim": "scared", "name": "GF FEAR", "fps": 24, "loop": true, "offsets": [-2, -17]}]})"},
    {"gf-car", R"({"image": "characters/gfCar", "position": [0, 0], "camera_position": [0, 0], "healthicon": "gf",
  "flip_x": false, "healthbar_colors": [165, 0, 77], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "singUP", "name": "GF Dancing Beat Hair blowing CAR", "fps": 24, "loop": false, "offsets": [0, 0], "indices": [0]},
  {"anim": "danceLeft", "name": "GF Dancing Beat Hair blowing CAR", "fps": 24, "loop": false, "offsets": [0, 0], "indices": [30,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14]},
  {"anim": "danceRight", "name": "GF Dancing Beat Hair blowing CAR", "fps": 24, "loop": false, "offsets": [0, 0], "indices": [15,16,17,18,19,20,21,22,23,24,25,26,27,28,29]}]})"},
    {"gf-pixel", R"({"image": "characters/gfPixel", "position": [0, 0], "camera_position": [0, 0], "healthicon": "gf",
  "flip_x": false, "no_antialiasing": true, "healthbar_colors": [165, 0, 77], "sing_duration": 4, "scale": 6, "animations": [
  {"anim": "singUP", "name": "GF IDLE", "fps": 24, "loop": false, "offsets": [0, 0], "indices": [2]},
  {"anim": "danceLeft", "name": "GF IDLE", "fps": 24, "loop": false, "offsets": [0, 0], "indices": [30,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14]},
  {"anim": "danceRight", "name": "GF IDLE", "fps": 24, "loop": false, "offsets": [0, 0], "indices": [15,16,17,18,19,20,21,22,23,24,25,26,27,28,29]}]})"},
    {"dad", R"({"image": "characters/DADDY_DEAREST", "position": [0, 0], "camera_position": [0, 0], "healthicon": "dad",
  "flip_x": false, "healthbar_colors": [175, 102, 206], "sing_duration": 6.1, "scale": 1, "animations": [
  {"anim": "idle", "name": "Dad idle dance", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "Dad Sing Note LEFT", "fps": 24, "loop": false, "offsets": [-10, 10]},
  {"anim": "singDOWN", "name": "Dad Sing Note DOWN", "fps": 24, "loop": false, "offsets": [0, -30]},
  {"anim": "singUP", "name": "Dad Sing Note UP", "fps": 24, "loop": false, "offsets": [-6, 50]},
  {"anim": "singRIGHT", "name": "Dad Sing Note RIGHT", "fps": 24, "loop": false, "offsets": [0, 27]}]})"},
    {"spooky", R"({"image": "characters/spooky_kids_assets", "position": [0, 200], "camera_position": [0, 0], "healthicon": "spooky",
  "flip_x": false, "healthbar_colors": [213, 126, 0], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "danceLeft", "name": "spooky dance idle", "fps": 12, "loop": false, "offsets": [0, 0], "indices": [0,2,6]},
  {"anim": "danceRight", "name": "spooky dance idle", "fps": 12, "loop": false, "offsets": [0, 0], "indices": [8,10,12,14]},
  {"anim": "singLEFT", "name": "note sing left", "fps": 24, "loop": false, "offsets": [130, -10]},
  {"anim": "singDOWN", "name": "spooky DOWN note", "fps": 24, "loop": false, "offsets": [-50, -130]},
  {"anim": "singUP", "name": "spooky UP NOTE", "fps": 24, "loop": false, "offsets": [-20, 26]},
  {"anim": "singRIGHT", "name": "spooky sing right", "fps": 24, "loop": false, "offsets": [-130, -14]}]})"},
    {"pico", R"({"image": "characters/Pico_FNF_assetss", "position": [0, 300], "camera_position": [0, 0], "healthicon": "pico",
  "flip_x": true, "healthbar_colors": [183, 216, 85], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "Pico Idle Dance", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "Pico Note Right0", "fps": 24, "loop": false, "offsets": [65, 9]},
  {"anim": "singDOWN", "name": "Pico Down Note0", "fps": 24, "loop": false, "offsets": [200, -70]},
  {"anim": "singUP", "name": "pico Up note0", "fps": 24, "loop": false, "offsets": [-29, 27]},
  {"anim": "singRIGHT", "name": "Pico NOTE LEFT0", "fps": 24, "loop": false, "offsets": [-68, -7]},
  {"anim": "singLEFTmiss", "name": "Pico Note Right Miss", "fps": 24, "loop": false, "offsets": [62, 64]},
  {"anim": "singDOWNmiss", "name": "Pico Down Note MISS", "fps": 24, "loop": false, "offsets": [210, -28]},
  {"anim": "singUPmiss", "name": "pico Up note miss", "fps": 24, "loop": false, "offsets": [-19, 67]},
  {"anim": "singRIGHTmiss", "name": "Pico NOTE LEFT miss", "fps": 24, "loop": false, "offsets": [-60, 41]}]})"},
    {"mom", R"({"image": "characters/Mom_Assets", "position": [0, 0], "camera_position": [0, 0], "healthicon": "mom",
  "flip_x": false, "healthbar_colors": [216, 85, 142], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "Mom Idle", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "Mom Left Pose", "fps": 24, "loop": false, "offsets": [250, -23]},
  {"anim": "singDOWN", "name": "MOM DOWN POSE", "fps": 24, "loop": false, "offsets": [20, -160]},
  {"anim": "singUP", "name": "Mom Up Pose", "fps": 24, "loop": false, "offsets": [14, 71]},
  {"anim": "singRIGHT", "name": "Mom Pose Left", "fps": 24, "loop": false, "offsets": [10, -60]}]})"},
    {"mom-car", R"({"image": "characters/momCar", "position": [0, 0], "camera_position": [0, 0], "healthicon": "mom",
  "flip_x": false, "healthbar_colors": [216, 85, 142], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "Mom Idle", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "Mom Left Pose", "fps": 24, "loop": false, "offsets": [250, -23]},
  {"anim": "singDOWN", "name": "MOM DOWN POSE", "fps": 24, "loop": false, "offsets": [20, -160]},
  {"anim": "singUP", "name": "Mom Up Pose", "fps": 24, "loop": false, "offsets": [14, 71]},
  {"anim": "singRIGHT", "name": "Mom Pose Left", "fps": 24, "loop": false, "offsets": [10, -60]}]})"},
    {"monster", R"({"image": "characters/Monster_Assets", "position": [0, 100], "camera_position": [0, 0], "healthicon": "monster",
  "flip_x": false, "healthbar_colors": [243, 255, 110], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "monster idle", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "Monster left note", "fps": 24, "loop": false, "offsets": [-30, 0]},
  {"anim": "singDOWN", "name": "monster down", "fps": 24, "loop": false, "offsets": [-30, -40]},
  {"anim": "singUP", "name": "monster up note", "fps": 24, "loop": false, "offsets": [-20, 50]},
  {"anim": "singRIGHT", "name": "Monster Right note", "fps": 24, "loop": false, "offsets": [-51, 0]}]})"},
    {"monster-christmas", R"({"image": "characters/monsterChristmas", "position": [0, 130], "camera_position": [0, 0], "healthicon": "monster",
  "flip_x": false, "healthbar_colors": [243, 255, 110], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "monster idle", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "Monster left note", "fps": 24, "loop": false, "offsets": [-30, 0]},
  {"anim": "singDOWN", "name": "monster down", "fps": 24, "loop": false, "offsets": [-40, -94]},
  {"anim": "singUP", "name": "monster up note", "fps": 24, "loop": false, "offsets": [-20, 50]},
  {"anim": "singRIGHT", "name": "Monster Right note", "fps": 24, "loop": false, "offsets": [-51, 0]}]})"},
    {"parents-christmas", R"({"image": "characters/mom_dad_christmas_assets", "position": [-500, 0], "camera_position": [0, 0],
  "healthicon": "parents", "flip_x": false, "healthbar_colors": [196, 94, 174], "sing_duration": 4, "scale": 1, "animations": [
  {"anim": "idle", "name": "Parent Christmas Idle", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "Parent Left Note Dad", "fps": 24, "loop": false, "offsets": [-30, 16]},
  {"anim": "singDOWN", "name": "Parent Down Note Dad", "fps": 24, "loop": false, "offsets": [-31, -29]},
  {"anim": "singUP", "name": "Parent Up Note Dad", "fps": 24, "loop": false, "offsets": [-47, 24]},
  {"anim": "singRIGHT", "name": "Parent Right Note Dad", "fps": 24, "loop": false, "offsets": [-1, -23]},
  {"anim": "singLEFT-alt", "name": "Parent Left Note Mom", "fps": 24, "loop": false, "offsets": [-30, 15]},
  {"anim": "singDOWN-alt", "name": "Parent Down Note Mom", "fps": 24, "loop": false, "offsets": [-30, -27]},
  {"anim": "singUP-alt", "name": "Parent Up Note Mom", "fps": 24, "loop": false, "offsets": [-47, 24]},
  {"anim": "singRIGHT-alt", "name": "Parent Right Note Mom", "fps": 24, "loop": false, "offsets": [-1, -24]}]})"},
    {"senpai", R"({"image": "characters/senpai", "position": [150, 360], "camera_position": [0, 0], "healthicon": "senpai",
  "flip_x": false, "no_antialiasing": true, "healthbar_colors": [255, 170, 111], "sing_duration": 4, "scale": 6, "animations": [
  {"anim": "idle", "name": "Senpai Idle", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "SENPAI LEFT NOTE", "fps": 24, "loop": false, "offsets": [40, 0]},
  {"anim": "singDOWN", "name": "SENPAI DOWN NOTE", "fps": 24, "loop": false, "offsets": [14, 0]},
  {"anim": "singUP", "name": "SENPAI UP NOTE", "fps": 24, "loop": false, "offsets": [5, 37]},
  {"anim": "singRIGHT", "name": "SENPAI RIGHT NOTE", "fps": 24, "loop": false, "offsets": [0, 0]}]})"},
    {"senpai-angry", R"({"image": "characters/senpai", "position": [150, 360], "camera_position": [0, 0], "healthicon": "senpai",
  "flip_x": false, "no_antialiasing": true, "healthbar_colors": [255, 170, 111], "sing_duration": 4, "scale": 6, "animations": [
  {"anim": "idle", "name": "Angry Senpai Idle", "fps": 24, "loop": false, "offsets": [0, 0]},
  {"anim": "singLEFT", "name": "Angry Senpai LEFT NOTE", "fps": 24, "loop": false, "offsets": [40, 0]},
  {"anim": "singDOWN", "name": "Angry Senpai DOWN NOTE", "fps": 24, "loop": false, "offsets": [14, 0]},
  {"anim": "singUP", "name": "Angry Senpai UP NOTE", "fps": 24, "loop": false, "offsets": [5, 37]},
  {"anim": "singRIGHT", "name": "Angry Senpai RIGHT NOTE", "fps": 24, "loop": false, "offsets": [0, 0]}]})"},
    {"spirit", R"({"image": "characters/spirit", "position": [-150, 100], "camera_position": [0, 0], "healthicon": "spirit",
  "flip_x": false, "no_antialiasing": true, "healthbar_colors": [255, 60, 110], "sing_duration": 4, "scale": 6, "animations": [
  {"anim": "idle", "name": "idle spirit_", "fps": 24, "loop": false, "offsets": [-220, -280]},
  {"anim": "singLEFT", "name": "left_", "fps": 24, "loop": false, "offsets": [-200, -280]},
  {"anim": "singDOWN", "name": "spirit down_", "fps": 24, "loop": false, "offsets": [170, 110]},
  {"anim": "singUP", "name": "up_", "fps": 24, "loop": false, "offsets": [-220, -240]},
  {"anim": "singRIGHT", "name": "right_", "fps": 24, "loop": false, "offsets": [-220, -280]}]})"},
};

// Positions and zooms from the original PlayState (as in Psych's stage JSONs).
const Named kStageJson[] = {
    {"stage", R"({"defaultZoom": 0.9, "isPixelStage": false, "boyfriend": [770, 100], "girlfriend": [400, 130], "opponent": [100, 100],
  "hide_girlfriend": false, "camera_boyfriend": [0, 0], "camera_opponent": [0, 0], "camera_girlfriend": [0, 0], "camera_speed": 1})"},
    {"spooky", R"({"defaultZoom": 1.05, "isPixelStage": false, "boyfriend": [770, 100], "girlfriend": [400, 130], "opponent": [100, 100],
  "hide_girlfriend": false, "camera_boyfriend": [0, 0], "camera_opponent": [0, 0], "camera_girlfriend": [0, 0], "camera_speed": 1})"},
    {"philly", R"({"defaultZoom": 1.05, "isPixelStage": false, "boyfriend": [770, 100], "girlfriend": [400, 130], "opponent": [100, 100],
  "hide_girlfriend": false, "camera_boyfriend": [0, 0], "camera_opponent": [0, 0], "camera_girlfriend": [0, 0], "camera_speed": 1})"},
    {"limo", R"({"defaultZoom": 0.9, "isPixelStage": false, "boyfriend": [1030, -120], "girlfriend": [400, 130], "opponent": [100, 100],
  "hide_girlfriend": false, "camera_boyfriend": [-200, 0], "camera_opponent": [0, 0], "camera_girlfriend": [0, 0], "camera_speed": 1})"},
    {"mall", R"({"defaultZoom": 0.8, "isPixelStage": false, "boyfriend": [970, 100], "girlfriend": [400, 130], "opponent": [100, 100],
  "hide_girlfriend": false, "camera_boyfriend": [0, -100], "camera_opponent": [0, 0], "camera_girlfriend": [0, 0], "camera_speed": 1})"},
    {"mallEvil", R"({"defaultZoom": 1.05, "isPixelStage": false, "boyfriend": [1090, 100], "girlfriend": [400, 130], "opponent": [100, 20],
  "hide_girlfriend": false, "camera_boyfriend": [0, 0], "camera_opponent": [0, 0], "camera_girlfriend": [0, 0], "camera_speed": 1})"},
    {"school", R"({"defaultZoom": 1.05, "isPixelStage": true, "boyfriend": [970, 320], "girlfriend": [580, 430], "opponent": [100, 100],
  "hide_girlfriend": false, "camera_boyfriend": [-100, -100], "camera_opponent": [0, 0], "camera_girlfriend": [0, 0], "camera_speed": 1})"},
    {"schoolEvil", R"({"defaultZoom": 1.05, "isPixelStage": true, "boyfriend": [970, 320], "girlfriend": [580, 430], "opponent": [100, 100],
  "hide_girlfriend": false, "camera_boyfriend": [-100, -100], "camera_opponent": [0, 0], "camera_girlfriend": [0, 0], "camera_speed": 1})"},
};

const Named kStageLua[] = {
    {"stage", R"LUA(
-- Week 1 (Psych Engine 0.7's StageWeek1), with "Dadbattle Spotlight".
function onCreate()
    makeLuaSprite('stageback', 'stageback', -600, -200)
    setScrollFactor('stageback', 0.9, 0.9)
    addLuaSprite('stageback', false)
    makeLuaSprite('stagefront', 'stagefront', -650, 600)
    setScrollFactor('stagefront', 0.9, 0.9)
    scaleObject('stagefront', 1.1, 1.1)
    addLuaSprite('stagefront', false)
    if not lowQuality and checkFileExists('images/stage_light.png') then
        for i, x in ipairs({-125, 1225}) do
            local tag = 'stageLight' .. i
            makeLuaSprite(tag, 'stage_light', x, -100)
            setScrollFactor(tag, 0.9, 0.9)
            scaleObject(tag, 1.1, 1.1)
            setProperty(tag .. '.flipX', i == 2)
            addLuaSprite(tag, false)
        end
    end
    makeLuaSprite('stagecurtains', 'stagecurtains', -500, -300)
    setScrollFactor('stagecurtains', 1.3, 1.3)
    scaleObject('stagecurtains', 0.9, 0.9)
    addLuaSprite('stagecurtains', false)
end

-- "Dadbattle Spotlight": 1 the lights go down and a spotlight falls on the
-- opponent (2: on the opponent again, 3: on the player), fog rolling in;
-- 0 back to normal.
local spot = false
local function makeSpot()
    if spot then return end
    spot = true
    makeLuaSprite('dadbattleBlack', '', -800, -400)
    makeGraphic('dadbattleBlack', 2560, 1440, '000000')
    setScrollFactor('dadbattleBlack', 0, 0)
    setProperty('dadbattleBlack.alpha', 0.25)
    setProperty('dadbattleBlack.visible', false)
    addLuaSprite('dadbattleBlack', true)
    makeLuaSprite('dadbattleLight', 'spotlight', 400, -400)
    setProperty('dadbattleLight.alpha', 0.375)
    setBlendMode('dadbattleLight', 'add')
    setProperty('dadbattleLight.visible', false)
    addLuaSprite('dadbattleLight', true)
    for i = 1, 2 do
        local tag = 'dadbattleSmoke' .. i
        makeLuaSprite(tag, 'smoke', (i == 1 and -1550 or 1550) + 200, 660 + getRandomFloat(-20, 20))
        local k = getRandomFloat(1.1, 1.22)
        scaleObject(tag, k, k)
        setProperty(tag .. '.velocity.x', (i == 1 and 1 or -1) * getRandomFloat(15, 22))
        setProperty(tag .. '.flipX', i == 2)
        setBlendMode(tag, 'add')
        setProperty(tag .. '.alpha', 0)
        setProperty(tag .. '.visible', false)
        addLuaSprite(tag, true)
    end
end
function onEventPushed(name)
    if name == 'Dadbattle Spotlight' then makeSpot() end
end
function onEvent(name, v1, v2)
    if name ~= 'Dadbattle Spotlight' then return end
    makeSpot()
    local val = math.floor((tonumber(v1) or 0) + 0.5)
    if val >= 1 and val <= 3 then
        if val == 1 then
            setProperty('dadbattleBlack.visible', true)
            setProperty('dadbattleLight.visible', true)
            for i = 1, 2 do setProperty('dadbattleSmoke' .. i .. '.visible', true) end
            setProperty('defaultCamZoom', getProperty('defaultCamZoom') + 0.12)
        end
        local who = val > 2 and 'boyfriend' or 'dad'
        setProperty('dadbattleLight.alpha', 0)
        runTimer('dadbattleLightOn', 0.12)
        setProperty('dadbattleLight.x', getGraphicMidpointX(who) - getProperty('dadbattleLight.width') / 2)
        setProperty('dadbattleLight.y', getProperty(who .. '.y') + getProperty(who .. '.height') - getProperty('dadbattleLight.height') + 50)
        for i = 1, 2 do doTweenAlpha('dadbattleSmoke' .. i, 'dadbattleSmoke' .. i, 0.7, 1.5, 'quadInOut') end
    else
        setProperty('dadbattleBlack.visible', false)
        setProperty('dadbattleLight.visible', false)
        setProperty('defaultCamZoom', getProperty('defaultCamZoom') - 0.12)
        for i = 1, 2 do doTweenAlpha('dadbattleSmoke' .. i, 'dadbattleSmoke' .. i, 0, 0.7, 'linear') end
    end
end
function onTimerCompleted(tag)
    if tag == 'dadbattleLightOn' then setProperty('dadbattleLight.alpha', 0.375) end
end
function onTweenCompleted(tag)
    if (tag == 'dadbattleSmoke1' or tag == 'dadbattleSmoke2') and getProperty(tag .. '.alpha') <= 0 then
        setProperty(tag .. '.visible', false)
    end
end
)LUA"},
    {"spooky", R"LUA(
local lightningStrikeBeat = 0
local lightningOffset = 8
function onCreate()
    makeAnimatedLuaSprite('halloweenBG', 'halloween_bg', -200, -100)
    addAnimationByPrefix('halloweenBG', 'idle', 'halloweem bg0', 24, false)
    addAnimationByPrefix('halloweenBG', 'lightning', 'halloweem bg lightning strike', 24, false)
    playAnim('halloweenBG', 'idle', true)
    addLuaSprite('halloweenBG', false)
end
function onBeatHit()
    if getRandomBool(10) and curBeat > lightningStrikeBeat + lightningOffset then
        playAnim('halloweenBG', 'lightning', true)
        playSound('thunder_' .. getRandomInt(1, 2))
        lightningStrikeBeat = curBeat
        lightningOffset = getRandomInt(8, 24)
        characterPlayAnim('boyfriend', 'scared', true)
        characterPlayAnim('gf', 'scared', true)
    end
end
)LUA"},
    {"philly", R"LUA(
-- Week 3. Psych Engine 0.7's layout (one window picture tinted a new colour
-- every bar) when the mod has philly/window, the older one (five coloured
-- windows) otherwise; with the "Philly Glow" event.
local colors = {'31A2FD', '31FD8C', 'FB33F5', 'FD4531', 'FBA633'}
local newLayout = false
local curLight = 0
local trainMoving = false
local trainFrameTiming = 0
local trainCars = 8
local trainFinishing = false
local trainCooldown = 0
local startedMoving = false
function onCreate()
    newLayout = checkFileExists('images/philly/window.png')
    if not lowQuality then
        makeLuaSprite('sky', 'philly/sky', -100, 0)
        setScrollFactor('sky', 0.1, 0.1)
        addLuaSprite('sky', false)
    end
    makeLuaSprite('city', 'philly/city', -10, 0)
    setScrollFactor('city', 0.3, 0.3)
    scaleObject('city', 0.85, 0.85)
    addLuaSprite('city', false)
    if newLayout then
        makeLuaSprite('phillyWindow', 'philly/window', -10, 0)
        setScrollFactor('phillyWindow', 0.3, 0.3)
        scaleObject('phillyWindow', 0.85, 0.85)
        setProperty('phillyWindow.alpha', 0)
        addLuaSprite('phillyWindow', false)
    else
        for i = 0, 4 do
            makeLuaSprite('win' .. i, 'philly/win' .. i, -10, 0)
            setScrollFactor('win' .. i, 0.3, 0.3)
            scaleObject('win' .. i, 0.85, 0.85)
            setProperty('win' .. i .. '.visible', false)
            addLuaSprite('win' .. i, false)
        end
    end
    if not lowQuality then
        makeLuaSprite('streetBehind', 'philly/behindTrain', -40, 50)
        addLuaSprite('streetBehind', false)
    end
    makeLuaSprite('phillyTrain', 'philly/train', 2000, 360)
    addLuaSprite('phillyTrain', false)
    makeLuaSprite('street', 'philly/street', -40, 50)
    addLuaSprite('street', false)
end
local function trainReset()
    setProperty('phillyTrain.x', 1280 + 200)
    trainMoving = false
    trainCars = 8
    trainFinishing = false
    startedMoving = false
    characterPlayAnim('gf', 'hairFall', true)
end

-- "Philly Glow": 1 the street goes dark and glows in a new colour (the
-- characters tinted, a gradient rising from below), 2 a burst of particles
-- and the gradient bouncing up again, 0 back to normal.
local glow = false
local glowColor = 0
local gradY, gradH, gradAlpha = 225, 400, 1
local particles = {}
local pn = 0
local function makeGlow()
    if glow then return end
    glow = true
    makeLuaSprite('blammedLightsBlack', '', -640, -360)
    makeGraphic('blammedLightsBlack', 2560, 1440, '000000')
    setProperty('blammedLightsBlack.visible', false)
    addLuaSprite('blammedLightsBlack', false)
    setObjectOrder('blammedLightsBlack', getObjectOrder('street'))
    makeLuaSprite('phillyGlowGradient', 'philly/gradient', -400, gradY)
    setScrollFactor('phillyGlowGradient', 0, 0.75)
    setGraphicSize('phillyGlowGradient', 2000, gradH)
    updateHitbox('phillyGlowGradient')
    setProperty('phillyGlowGradient.visible', false)
    addLuaSprite('phillyGlowGradient', false)
    setObjectOrder('phillyGlowGradient', getObjectOrder('blammedLightsBlack') + 1)
    makeLuaSprite('phillyWindowEvent', newLayout and 'philly/window' or 'philly/win0', -10, 0)
    setScrollFactor('phillyWindowEvent', 0.3, 0.3)
    scaleObject('phillyWindowEvent', 0.85, 0.85)
    setProperty('phillyWindowEvent.visible', false)
    addLuaSprite('phillyWindowEvent', false)
    setObjectOrder('phillyWindowEvent', getObjectOrder('phillyGlowGradient') + 1)
    if not flashingLights then gradAlpha = 0.7 end
    precacheImage('philly/particle')
end
function onEventPushed(name)
    if name == 'Philly Glow' then makeGlow() end
end
local function hsv(hex, sMul, vMul)  -- the colour with its saturation and brightness scaled
    local n = tonumber(hex, 16) or 0xFFFFFF
    local r, g, b = (n // 65536) % 256 / 255, (n // 256) % 256 / 255, n % 256 / 255
    local mx, mn = math.max(r, g, b), math.min(r, g, b)
    local d, h = mx - mn, 0
    if d > 0 then
        if mx == r then h = ((g - b) / d) % 6 elseif mx == g then h = (b - r) / d + 2 else h = (r - g) / d + 4 end
        h = h * 60
    end
    local v = math.min(1, mx * vMul)
    local sat = math.min(1, (mx == 0 and 0 or d / mx) * sMul)
    local c = v * sat
    local x = c * (1 - math.abs((h / 60) % 2 - 1))
    local m = v - c
    local rr, gg, bb
    if h < 60 then rr, gg, bb = c, x, 0 elseif h < 120 then rr, gg, bb = x, c, 0
    elseif h < 180 then rr, gg, bb = 0, c, x elseif h < 240 then rr, gg, bb = 0, x, c
    elseif h < 300 then rr, gg, bb = x, 0, c else rr, gg, bb = c, 0, x end
    return string.format('%02X%02X%02X', math.floor((rr + m) * 255 + 0.5), math.floor((gg + m) * 255 + 0.5), math.floor((bb + m) * 255 + 0.5))
end
local function doFlash()
    cameraFlash('camGame', flashingLights and 'FFFFFF' or '80FFFFFF', 0.15, true)
end
local function zoomBump()
    if cameraZoomOnBeat then
        setProperty('camGame.zoom', getProperty('camGame.zoom') + 0.5)
        setProperty('camHUD.zoom', getProperty('camHUD.zoom') + 0.1)
    end
end
local function clearParticles()
    for _, p in ipairs(particles) do removeLuaSprite(p.tag, true) end
    particles = {}
end
local function bop()
    setGraphicSize('phillyGlowGradient', 2000, gradH)
    updateHitbox('phillyGlowGradient')
    setProperty('phillyGlowGradient.y', gradY)
    setProperty('phillyGlowGradient.alpha', gradAlpha)
end
function onEvent(name, v1, v2)
    if name ~= 'Philly Glow' then return end
    makeGlow()
    local id = math.max(0, math.floor((tonumber(v1) or 0) + 0.5))
    local chars = {'boyfriend', 'gf', 'dad'}
    if id == 0 then
        if getProperty('phillyGlowGradient.visible') then
            doFlash()
            zoomBump()
            for _, o in ipairs({'blammedLightsBlack', 'phillyWindowEvent', 'phillyGlowGradient'}) do setProperty(o .. '.visible', false) end
            clearParticles()
            glowColor = 0
            for _, c in ipairs(chars) do setProperty(c .. '.color', 0xFFFFFF) end
            setProperty('street.color', 0xFFFFFF)
        end
    elseif id == 1 then
        local pick = getRandomInt(1, 5, tostring(glowColor))
        if pick == glowColor then pick = pick % 5 + 1 end
        glowColor = pick
        local color = colors[pick]
        if not getProperty('phillyGlowGradient.visible') then
            doFlash()
            zoomBump()
            setProperty('blammedLightsBlack.visible', true)
            setProperty('blammedLightsBlack.alpha', 1)
            setProperty('phillyWindowEvent.visible', true)
            setProperty('phillyGlowGradient.visible', true)
        elseif flashingLights then
            cameraFlash('camGame', '40' .. color, 0.5, true)
        end
        local charColor = getColorFromHex(hsv(color, flashingLights and 0.75 or 0.5, 1))
        for _, c in ipairs(chars) do setProperty(c .. '.color', charColor) end
        for _, p in ipairs(particles) do setProperty(p.tag .. '.color', getColorFromHex(color)) end
        setProperty('phillyGlowGradient.color', getColorFromHex(color))
        setProperty('phillyWindowEvent.color', getColorFromHex(color))
        setProperty('street.color', getColorFromHex(hsv(color, 1, 0.5)))
    elseif id == 2 then
        if not lowQuality and getProperty('phillyGlowGradient.visible') then
            local num = getRandomInt(8, 12)
            local width = 2000 / num
            local color = getColorFromHex(colors[glowColor] or 'FFFFFF')
            for j = 0, 2 do
                for i = 0, num - 1 do
                    pn = pn + 1
                    local tag = 'phillyGlowParticle' .. pn
                    makeLuaSprite(tag, 'philly/particle', -400 + width * i + getRandomFloat(-width / 5, width / 5),
                        gradY + 200 + getRandomFloat(0, 125) + j * 40)
                    setProperty(tag .. '.color', color)
                    local k = getRandomFloat(0.75, 1)
                    setProperty(tag .. '.scale.x', k)
                    setProperty(tag .. '.scale.y', k)
                    setScrollFactor(tag, getRandomFloat(0.3, 0.75), getRandomFloat(0.65, 0.75))
                    setProperty(tag .. '.velocity.x', getRandomFloat(-40, 40))
                    setProperty(tag .. '.velocity.y', getRandomFloat(-175, -250))
                    setProperty(tag .. '.acceleration.x', getRandomFloat(-10, 10))
                    setProperty(tag .. '.acceleration.y', 25)
                    local decay = getRandomFloat(0.8, 1)
                    if not flashingLights then
                        decay = decay * 0.5
                        setProperty(tag .. '.alpha', 0.5)
                    end
                    addLuaSprite(tag, false)
                    setObjectOrder(tag, getObjectOrder('phillyWindowEvent'))
                    particles[#particles + 1] = {tag = tag, life = getRandomFloat(0.6, 0.9), decay = decay, scale = k}
                end
            end
        end
        bop()
    end
end

function onUpdate(elapsed)
    if newLayout then
        setProperty('phillyWindow.alpha', math.max(0, getProperty('phillyWindow.alpha') - (crochet / 1000) * elapsed * 1.5))
    end
    if glow then
        local h = math.floor(getProperty('phillyGlowGradient.height') - 1000 * elapsed + 0.5)
        if h > 0 then
            setProperty('phillyGlowGradient.alpha', gradAlpha)
            setGraphicSize('phillyGlowGradient', 2000, h)
            updateHitbox('phillyGlowGradient')
            setProperty('phillyGlowGradient.y', gradY + (gradH - getProperty('phillyGlowGradient.height')))
        else
            setProperty('phillyGlowGradient.alpha', 0)
            setProperty('phillyGlowGradient.y', -5000)
        end
        for i = #particles, 1, -1 do
            local p = particles[i]
            p.life = p.life - elapsed
            if p.life < 0 then
                p.life = 0
                local a = getProperty(p.tag .. '.alpha') - p.decay * elapsed
                if a > 0 then
                    setProperty(p.tag .. '.alpha', a)
                    setProperty(p.tag .. '.scale.x', p.scale * a)
                    setProperty(p.tag .. '.scale.y', p.scale * a)
                else
                    removeLuaSprite(p.tag, true)
                    table.remove(particles, i)
                end
            end
        end
    end
    if trainMoving then
        trainFrameTiming = trainFrameTiming + elapsed
        if trainFrameTiming >= 1 / 24 then
            if not startedMoving then
                startedMoving = true
                characterPlayAnim('gf', 'hairBlow', false)
            end
            if startedMoving then
                setProperty('phillyTrain.x', getProperty('phillyTrain.x') - 400)
                if getProperty('phillyTrain.x') < -2000 and not trainFinishing then
                    setProperty('phillyTrain.x', -1150)
                    trainCars = trainCars - 1
                    if trainCars <= 0 then trainFinishing = true end
                end
                if getProperty('phillyTrain.x') < -4000 and trainFinishing then trainReset() end
            end
            trainFrameTiming = 0
        end
    end
end
function onBeatHit()
    if not trainMoving then trainCooldown = trainCooldown + 1 end
    if curBeat % 4 == 0 then
        if newLayout then
            local pick = getRandomInt(1, 5, tostring(curLight))
            if pick == curLight then pick = pick % 5 + 1 end
            curLight = pick
            setProperty('phillyWindow.color', getColorFromHex(colors[pick]))
            setProperty('phillyWindow.alpha', 1)
        else
            setProperty('win' .. curLight .. '.visible', false)
            curLight = getRandomInt(0, 4)
            setProperty('win' .. curLight .. '.visible', true)
        end
    end
    if curBeat % 8 == 4 and getRandomBool(30) and not trainMoving and trainCooldown > 8 then
        trainCooldown = getRandomInt(-4, 0)
        trainMoving = true
        playSound('train_passes', 1, 'train')
    end
end
)LUA"},
    {"limo", R"LUA(
-- Week 4 (Psych Engine 0.7's Limo), with "Kill Henchmen" when the mod has
-- the gore pictures: a pole sweeps the dancers off the limo, then the limo
-- speeds off and comes back.
local danced = false
local carCanDrive = true
local gore = false
local killState = 'wait'
local limoSpeed = 0
local dancersDiff = 320
local bits = {}
local bitN = 0
local function resetLimoKill()
    for _, t in ipairs({'limoMetalPole', 'limoLight', 'limoCorpse', 'limoCorpseTwo'}) do
        setProperty(t .. '.x', -500)
        setProperty(t .. '.visible', false)
    end
end
function onCreate()
    makeLuaSprite('skyBG', 'limo/limoSunset', -120, -50)
    setScrollFactor('skyBG', 0.1, 0.1)
    addLuaSprite('skyBG', false)
    gore = not lowQuality and checkFileExists('images/gore/metalPole.png')
    if gore then
        makeLuaSprite('limoMetalPole', 'gore/metalPole', -500, 220)
        setScrollFactor('limoMetalPole', 0.4, 0.4)
        addLuaSprite('limoMetalPole', false)
    end
    makeAnimatedLuaSprite('bgLimo', 'limo/bgLimo', -150, 480)
    addAnimationByPrefix('bgLimo', 'drive', 'background limo pink', 24, true)
    setScrollFactor('bgLimo', 0.4, 0.4)
    addLuaSprite('bgLimo', false)
    if gore then
        makeAnimatedLuaSprite('limoCorpse', 'gore/noooooo', -500, 220 - 130)
        addAnimationByPrefix('limoCorpse', 'idle', 'Henchmen on rail', 24, true)
        setScrollFactor('limoCorpse', 0.4, 0.4)
        addLuaSprite('limoCorpse', false)
        makeAnimatedLuaSprite('limoCorpseTwo', 'gore/noooooo', -500, 220)
        addAnimationByPrefix('limoCorpseTwo', 'idle', 'henchmen death', 24, true)
        setScrollFactor('limoCorpseTwo', 0.4, 0.4)
        addLuaSprite('limoCorpseTwo', false)
    end
    for i = 0, 4 do
        local tag = 'limoDancer' .. i
        makeAnimatedLuaSprite(tag, 'limo/limoDancer', (370 * i) + dancersDiff - 150, 480 - 400)
        addAnimationByIndices(tag, 'danceLeft', 'bg dancer sketch PINK', '0,1,2,3,4,5,6,7,8,9,10,11,12,13,14', 24)
        addAnimationByIndices(tag, 'danceRight', 'bg dancer sketch PINK', '15,16,17,18,19,20,21,22,23,24,25,26,27,28,29', 24)
        playAnim(tag, 'danceLeft', true)
        setScrollFactor(tag, 0.4, 0.4)
        addLuaSprite(tag, false)
    end
    if gore then
        makeLuaSprite('limoLight', 'gore/coldHeartKiller', -500 - 180, 220 - 80)
        setScrollFactor('limoLight', 0.4, 0.4)
        addLuaSprite('limoLight', false)
        resetLimoKill()
    end
    makeLuaSprite('fastCar', 'limo/fastCarLol', -300, 160)
    addLuaSprite('fastCar', false)
    makeAnimatedLuaSprite('limo', 'limo/limoDrive', -120, 550)
    addAnimationByPrefix('limo', 'drive', 'Limo stage', 24, true)
    addLuaSprite('limo', true)
end
function onCreatePost()
    -- The car passes behind girlfriend; the limo sits between her and the singers.
    setObjectOrder('fastCar', getObjectOrder('gfGroup'))
    setObjectOrder('limo', getObjectOrder('dadGroup'))
end
local function resetFastCar()
    setProperty('fastCar.x', -12600)
    setProperty('fastCar.y', getRandomInt(140, 250))
    carCanDrive = true
end
local function bit(img, x, y, anim)
    bitN = bitN + 1
    local tag = 'limoParticle' .. bitN
    makeAnimatedLuaSprite(tag, img, x, y)
    addAnimationByPrefix(tag, 'p', anim, 24, false)
    playAnim(tag, 'p', true)
    setScrollFactor(tag, 0.4, 0.4)
    addLuaSprite(tag, false)
    setObjectOrder(tag, getObjectOrder('limoLight') + 1)
    bits[#bits + 1] = tag
    return tag
end
local function dancersParenting()
    local bx = getProperty('bgLimo.x')
    for i = 0, 4 do setProperty('limoDancer' .. i .. '.x', (370 * i) + dancersDiff + bx) end
end
function onEvent(name, v1, v2)
    if name == 'Kill Henchmen' and gore and killState == 'wait' then
        setProperty('limoMetalPole.x', -400)
        setProperty('limoMetalPole.visible', true)
        setProperty('limoLight.visible', true)
        setProperty('limoCorpse.visible', false)
        setProperty('limoCorpseTwo.visible', false)
        killState = 'killing'
    end
end
function onUpdate(elapsed)
    if not gore then return end
    for i = #bits, 1, -1 do
        if getProperty(bits[i] .. '.animation.finished') then
            removeLuaSprite(bits[i], true)
            table.remove(bits, i)
        end
    end
    if killState == 'killing' then
        local pole = getProperty('limoMetalPole.x') + 5000 * elapsed
        local light = pole - 180
        setProperty('limoMetalPole.x', pole)
        setProperty('limoLight.x', light)
        setProperty('limoCorpse.x', light - 50)
        setProperty('limoCorpseTwo.x', light + 35)
        for i = 0, 4 do
            local tag = 'limoDancer' .. i
            local dx, dy = getProperty(tag .. '.x'), getProperty(tag .. '.y')
            if dx < 1280 * 1.5 and light > (370 * i) + 170 then
                if i == 0 or i == 3 then
                    if i == 0 then playSound('dancerdeath', 0.5) end
                    local d = i == 3 and ' 2 ' or ' '
                    bit('gore/noooooo', dx + 200, dy, 'hench leg spin' .. d .. 'PINK')
                    bit('gore/noooooo', dx + 160, dy + 200, 'hench arm spin' .. d .. 'PINK')
                    bit('gore/noooooo', dx, dy + 50, 'hench head spin' .. d .. 'PINK')
                    local b = bit('gore/stupidBlood', dx - 110, dy + 20, 'blood')
                    setProperty(b .. '.flipX', true)
                    setProperty(b .. '.angle', -57.5)
                elseif i == 1 then
                    setProperty('limoCorpse.visible', true)
                elseif i == 2 then
                    setProperty('limoCorpseTwo.visible', true)
                end
                setProperty(tag .. '.x', dx + 1280 * 2)
            end
        end
        if pole > 1280 * 2 then
            resetLimoKill()
            limoSpeed = 800
            killState = 'offscreen'
        end
    elseif killState == 'offscreen' then
        limoSpeed = limoSpeed - 4000 * elapsed
        setProperty('bgLimo.x', getProperty('bgLimo.x') - limoSpeed * elapsed)
        if getProperty('bgLimo.x') > 1280 * 1.5 then
            limoSpeed = 3000
            killState = 'speeding'
        end
    elseif killState == 'speeding' then
        limoSpeed = math.max(1000, limoSpeed - 2000 * elapsed)
        setProperty('bgLimo.x', getProperty('bgLimo.x') - limoSpeed * elapsed)
        if getProperty('bgLimo.x') < -275 then
            killState = 'stopping'
            limoSpeed = 800
        end
        dancersParenting()
    elseif killState == 'stopping' then
        local x = -150 + (getProperty('bgLimo.x') + 150) * math.exp(-elapsed * 9)
        setProperty('bgLimo.x', x)
        if math.floor(x + 0.5) == -150 then
            setProperty('bgLimo.x', -150)
            killState = 'wait'
        end
        dancersParenting()
    end
end
function onBeatHit()
    danced = not danced
    for i = 0, 4 do playAnim('limoDancer' .. i, danced and 'danceRight' or 'danceLeft', true) end
    if getRandomBool(10) and carCanDrive then
        playSound('carPass' .. getRandomInt(0, 1), 0.7)
        setProperty('fastCar.x', -12600)
        setProperty('fastCar.y', getRandomInt(140, 250))
        doTweenX('fastCarDrive', 'fastCar', 2000, 1.2, 'linear')
        carCanDrive = false
        runTimer('fastCarReset', 2)
    end
end
function onTimerCompleted(tag)
    if tag == 'fastCarReset' then resetFastCar() end
end
)LUA"},
    {"mall", R"LUA(
function onCreate()
    makeLuaSprite('bgWalls', 'christmas/bgWalls', -1000, -500)
    setScrollFactor('bgWalls', 0.2, 0.2)
    scaleObject('bgWalls', 0.8, 0.8)
    addLuaSprite('bgWalls', false)
    makeAnimatedLuaSprite('upperBoppers', 'christmas/upperBop', -240, -90)
    addAnimationByPrefix('upperBoppers', 'bop', 'Upper Crowd Bob', 24, false)
    setScrollFactor('upperBoppers', 0.33, 0.33)
    scaleObject('upperBoppers', 0.85, 0.85)
    addLuaSprite('upperBoppers', false)
    makeLuaSprite('bgEscalator', 'christmas/bgEscalator', -1100, -600)
    setScrollFactor('bgEscalator', 0.3, 0.3)
    scaleObject('bgEscalator', 0.9, 0.9)
    addLuaSprite('bgEscalator', false)
    makeLuaSprite('tree', 'christmas/christmasTree', 370, -250)
    setScrollFactor('tree', 0.4, 0.4)
    addLuaSprite('tree', false)
    makeAnimatedLuaSprite('bottomBoppers', 'christmas/bottomBop', -300, 140)
    addAnimationByPrefix('bottomBoppers', 'bop', 'Bottom Level Boppers', 24, false)
    setScrollFactor('bottomBoppers', 0.9, 0.9)
    addLuaSprite('bottomBoppers', false)
    makeLuaSprite('fgSnow', 'christmas/fgSnow', -600, 700)
    addLuaSprite('fgSnow', false)
    makeAnimatedLuaSprite('santa', 'christmas/santa', -840, 150)
    addAnimationByPrefix('santa', 'idle', 'santa idle in fear', 24, false)
    addLuaSprite('santa', false)
end
function onBeatHit()
    playAnim('upperBoppers', 'bop', true)
    playAnim('bottomBoppers', 'bop', true)
    playAnim('santa', 'idle', true)
end
)LUA"},
    {"mallEvil", R"LUA(
function onCreate()
    makeLuaSprite('evilBG', 'christmas/evilBG', -400, -500)
    setScrollFactor('evilBG', 0.2, 0.2)
    scaleObject('evilBG', 0.8, 0.8)
    addLuaSprite('evilBG', false)
    makeLuaSprite('evilTree', 'christmas/evilTree', 300, -300)
    setScrollFactor('evilTree', 0.2, 0.2)
    addLuaSprite('evilTree', false)
    makeLuaSprite('evilSnow', 'christmas/evilSnow', -200, 700)
    addLuaSprite('evilSnow', false)
end
)LUA"},
    {"school", R"LUA(
-- Week 6, with "BG Freaks Expression" (the girls at the back turn from
-- cheering to dissuaded and back). Charts without the event (older engines)
-- get the dissuaded girls in Roses, as those engines did.
local danced = false
local pissed = false
local expressionEvent = false
local function pixel(tag, widthMult)
    setGraphicSize(tag, math.floor(getProperty(tag .. '.width') * widthMult))
    updateHitbox(tag)
    setProperty(tag .. '.antialiasing', false)
end
local function setGirls(p)
    pissed = p
    local prefix = p and 'BG fangirls dissuaded' or 'BG girls group'
    addAnimationByIndices('bgGirls', 'danceLeft', prefix, '0,1,2,3,4,5,6,7,8,9,10,11,12,13', 24)
    addAnimationByIndices('bgGirls', 'danceRight', prefix, '15,16,17,18,19,20,21,22,23,24,25,26,27,28,29', 24)
end
local function girlsDance()
    danced = not danced
    playAnim('bgGirls', danced and 'danceRight' or 'danceLeft', true)
end
function onCreate()
    local rep = -200
    makeLuaSprite('bgSky', 'weeb/weebSky', 0, 0)
    setScrollFactor('bgSky', 0.1, 0.1)
    addLuaSprite('bgSky', false)
    makeLuaSprite('bgSchool', 'weeb/weebSchool', rep, 0)
    setScrollFactor('bgSchool', 0.6, 0.9)
    addLuaSprite('bgSchool', false)
    makeLuaSprite('bgStreet', 'weeb/weebStreet', rep, 0)
    setScrollFactor('bgStreet', 0.95, 0.95)
    addLuaSprite('bgStreet', false)
    makeLuaSprite('fgTrees', 'weeb/weebTreesBack', rep + 170, 130)
    setScrollFactor('fgTrees', 0.9, 0.9)
    addLuaSprite('fgTrees', false)
    makeAnimatedLuaSprite('bgTrees', 'weeb/weebTrees', rep - 380, -800)
    addAnimation('bgTrees', 'treeLoop', {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18}, 12, true)
    setScrollFactor('bgTrees', 0.85, 0.85)
    addLuaSprite('bgTrees', false)
    makeAnimatedLuaSprite('treeLeaves', 'weeb/petals', rep, -40)
    addAnimationByPrefix('treeLeaves', 'leaves', 'PETALS ALL', 24, true)
    setScrollFactor('treeLeaves', 0.85, 0.85)
    addLuaSprite('treeLeaves', false)
    pixel('bgSky', 6); pixel('bgSchool', 6); pixel('bgStreet', 6)
    pixel('bgTrees', 6 * 1.4); pixel('fgTrees', 6 * 0.8); pixel('treeLeaves', 6)
    makeAnimatedLuaSprite('bgGirls', 'weeb/bgFreaks', -100, 190)
    setGirls(false)
    playAnim('bgGirls', 'danceLeft', true)
    setScrollFactor('bgGirls', 0.9, 0.9)
    pixel('bgGirls', 6)
    addLuaSprite('bgGirls', false)
end
function onEventPushed(name)
    if name == 'BG Freaks Expression' then expressionEvent = true end
end
function onCreatePost()
    if not expressionEvent and songName and string.lower(songName) == 'roses' then
        setGirls(true)
        playAnim('bgGirls', 'danceLeft', true)
    end
end
function onEvent(name, v1, v2)
    if name == 'BG Freaks Expression' then
        setGirls(not pissed)
        girlsDance()
    end
end
function onBeatHit()
    girlsDance()
end
)LUA"},
    {"schoolEvil", R"LUA(
-- Week 6's Thorns, with "Trigger BG Ghouls" (ghosts flicker up behind
-- girlfriend once per event).
local ghouls = false
local function pixelProp(tag, img, x, y, scroll)
    makeLuaSprite(tag, img, x, y)
    setScrollFactor(tag, scroll, scroll)
    scaleObject(tag, 6, 6)
    setProperty(tag .. '.antialiasing', false)
    addLuaSprite(tag, false)
end
function onCreate()
    if checkFileExists('images/weeb/animatedEvilSchool.png') then
        makeAnimatedLuaSprite('evilSchool', 'weeb/animatedEvilSchool', 400, 200)
        addAnimationByPrefix('evilSchool', 'idle', 'background 2', 24, true)
        setScrollFactor('evilSchool', 0.8, 0.9)
        scaleObject('evilSchool', 6, 6, false)
        setProperty('evilSchool.antialiasing', false)
        addLuaSprite('evilSchool', false)
    else
        -- P-Slice's (the base game's newer) pictures, in its places.
        pixelProp('evilTrees', 'weeb/evil/weebBackTrees', -842, -80, 0.5)
        pixelProp('evilSchool', 'weeb/evil/weebSchool', -816, -38, 0.75)
        pixelProp('evilStreet', 'weeb/evil/weebStreet', -662, 6, 1)
        if checkFileExists('images/weeb/evil/weebTrees.png') then pixelProp('evilFgTrees', 'weeb/evil/weebTrees', -662, 6, 1) end
    end
end
function onEventPushed(name)
    if name == 'Trigger BG Ghouls' and not ghouls and not lowQuality and checkFileExists('images/weeb/bgGhouls.png') then
        ghouls = true
        makeAnimatedLuaSprite('bgGhouls', 'weeb/bgGhouls', -100, 190)
        addAnimationByPrefix('bgGhouls', 'glitch', 'BG freaks glitch instance', 24, false)
        setScrollFactor('bgGhouls', 0.9, 0.9)
        setGraphicSize('bgGhouls', math.floor(getProperty('bgGhouls.width') * 6))
        updateHitbox('bgGhouls')
        setProperty('bgGhouls.antialiasing', false)
        setProperty('bgGhouls.visible', false)
        addLuaSprite('bgGhouls', false)
    end
end
function onCreatePost()
    if ghouls then setObjectOrder('bgGhouls', getObjectOrder('gfGroup')) end
end
function onEvent(name, v1, v2)
    if name == 'Trigger BG Ghouls' and ghouls then
        playAnim('bgGhouls', 'glitch', true)
        setProperty('bgGhouls.visible', true)
    end
end
function onUpdate(elapsed)
    if ghouls and getProperty('bgGhouls.visible') and getProperty('bgGhouls.animation.finished') then
        setProperty('bgGhouls.visible', false)
    end
end
)LUA"},
};

const Named kProbe[] = {
    {"stage", "stageback"}, {"spooky", "halloween_bg"}, {"philly", "philly/sky"}, {"limo", "limo/limoSunset"},
    {"mall", "christmas/bgWalls"}, {"mallEvil", "christmas/evilBG"}, {"school", "weeb/weebSky"},
    {"schoolEvil", "weeb/animatedEvilSchool|weeb/evil/weebSchool"},
};

template <size_t N>
const char* lookup(const Named (&table)[N], std::string_view name) {
    for (const Named& n : table)
        if (equalsI(n.name, name)) return n.text;
    return nullptr;
}

}  // namespace

const char* characterJson(std::string_view name) { return lookup(kCharacters, name); }
const char* stageJson(std::string_view stage) { return lookup(kStageJson, stage); }
const char* stageLua(std::string_view stage) { return lookup(kStageLua, stage); }
const char* stageProbeImage(std::string_view stage) { return lookup(kProbe, stage); }

std::string stageForSong(std::string_view song) {
    const std::string s = songPathName(song);
    if (s == "spookeez" || s == "south" || s == "monster") return "spooky";
    if (s == "pico" || s == "philly" || s == "philly-nice" || s == "blammed") return "philly";
    if (s == "satin-panties" || s == "high" || s == "milf") return "limo";
    if (s == "cocoa" || s == "eggnog") return "mall";
    if (s == "winter-horrorland") return "mallEvil";
    if (s == "senpai" || s == "roses") return "school";
    if (s == "thorns") return "schoolEvil";
    return "stage";
}

std::string gfForStage(std::string_view stage) {
    const std::string s = toLower(stage);
    if (s == "limo") return "gf-car";
    if (s == "mall" || s == "mallevil") return "gf-christmas";
    if (s == "school" || s == "schoolevil") return "gf-pixel";
    return "gf";
}

}  // namespace gh::basegame
