// God's Hand of Principality and Demise. Copyright (c) 2026 deatuamae67-crypto.
// All rights reserved; see LICENSE. Forking is not allowed: contribute through issues.

#include "builtin_events.hpp"

#include <algorithm>
#include <iterator>

namespace gh::builtin {
namespace {

struct Named {
    const char* name;
    const char* text;
};

// Done in C++ (PlayState::handleBuiltinEvent and friends) or by the built-in
// base-game stages.
const char* const kNative[] = {
    // Psych Engine
    "Hey!", "Set GF Speed", "Add Camera Zoom", "Play Animation", "Camera Follow Pos", "Alt Idle Animation",
    "Screen Shake", "Change Character", "Change Scroll Speed", "Set Property", "Play Sound",
    // the base game's stages (week 1, 3, 4 and 6)
    "Dadbattle Spotlight", "Philly Glow", "Kill Henchmen", "BG Freaks Expression", "Trigger BG Ghouls",
    // JS Engine
    "Set Camera Zoom", "Set Cam Zoom", "Tween Camera Zoom", "Enable Camera Bop", "Disable Camera Bop",
    "Camera Bopping", "Camera Twist", "Change Note Multiplier", "Fake Song Length", "Change Song Name",
    "Rainbow Eyesore", "Popup", "Popup (No Pause)", "Credits Popup", "Windows Notification", "Enable Bot Energy",
    "Disable Bot Energy", "Set Bot Energy Speeds",
    // Dek Engine
    "Camera rotate on", "Camera rotate off", "Flash", "Invert", "Invert No", "Invert Color",
    // V-Slice charts, and P-Slice / H-Slice's conversions of them
    "Zoom Camera", "Target Camera", "Set Camera Bopping", "SetCameraBop", "Vslice Scroll Speed",
    // Denpa Engine
    "Change Zoom Interval",
    // Hortas edition
    "EpicEyeSore", "ClearEyeSore",
    // SCReboot
    "Toggle Blocked Glitch Effect",
    // Dek Engine (its finale)
    "Glitch Shader add", "Glitch Shader add (CamHud)",
};

// ------------------------------------------------------------------ Psych --

const char* const kBlammedLights = R"LUA(
-- Psych Engine 0.4-0.5 had "Blammed Lights" built in, on any stage: the stage
-- goes dark behind the characters, who glow in one of five colours. Value 1:
-- 1 blue, 2 green, 3 pink, 4 red, 5 orange, above 5 a random other one; 0,
-- empty or the light already on: back to normal.
local colors = {'31A2FD', '31FD8C', 'F794F7', 'F96D63', 'FBA633'}
local chars = {'boyfriend', 'gf', 'dad'}
local curLight = 0
local made = false
local function make()
    if made then return end
    made = true
    makeLuaSprite('blammedLightsBlack', '', -640, -360)
    makeGraphic('blammedLightsBlack', 2560, 1440, '000000')
    setProperty('blammedLightsBlack.alpha', 0)
    addLuaSprite('blammedLightsBlack', false)
    local first = -1
    for _, g in ipairs({'gfGroup', 'dadGroup', 'boyfriendGroup'}) do
        local o = getObjectOrder(g)
        if o ~= nil and o >= 0 and (first < 0 or o < first) then first = o end
    end
    if first >= 0 then setObjectOrder('blammedLightsBlack', first) end
end
local function has(ch) return getProperty(ch .. '.visible') ~= nil end
function onEventPushed(name)
    if name == 'Blammed Lights' then make() end
end
function onEvent(name, v1, v2)
    if name ~= 'Blammed Lights' then return end
    make()
    local id = math.floor(tonumber(v1) or 0)
    if id > 0 and id ~= curLight then
        if id > 5 then
            id = getRandomInt(1, 5, tostring(curLight))
            if id == curLight then id = id % 5 + 1 end
        end
        curLight = id
        local c = colors[id]
        local fade = getProperty('blammedLightsBlack.alpha') == 0
        if fade then
            doTweenAlpha('blammedLightsBlackTween', 'blammedLightsBlack', 1, 1, 'quadInOut')
        else
            cancelTween('blammedLightsBlackTween')
            setProperty('blammedLightsBlack.alpha', 1)
        end
        for _, ch in ipairs(chars) do
            if has(ch) then
                cancelTween('blammed_' .. ch)
                if fade then
                    setProperty(ch .. '.color', getColorFromHex('FFFFFF'))
                    doTweenColor('blammed_' .. ch, ch, c, 1, 'quadInOut')
                else
                    setProperty(ch .. '.color', getColorFromHex(c))
                end
            end
        end
    else
        if getProperty('blammedLightsBlack.alpha') ~= 0 then
            doTweenAlpha('blammedLightsBlackTween', 'blammedLightsBlack', 0, 1, 'quadInOut')
        end
        for _, ch in ipairs(chars) do
            if has(ch) then
                cancelTween('blammed_' .. ch)
                doTweenColor('blammed_' .. ch, ch, 'FFFFFF', 1, 'quadInOut')
            end
        end
        curLight = 0
    end
end
)LUA";

// --------------------------------------------- health and character fades --

const char* const kGain = R"LUA(
-- "Gain" (the Dave and Bambi fan mods' companion to "Drain"): the player gets
-- 0.02 health plus value 1 (2 is the whole bar).
function onEvent(name, v1, v2)
    if name ~= 'Gain' then return end
    setProperty('health', math.min(2, getProperty('health') + 0.02 + (tonumber(v1) or 0)))
end
)LUA";

// Opponent Fade / BF Fade: value 1 seconds, value 2 the alpha (0 to 1; above 1
// a percentage, as some charts give it). Their icon fades with them.
const char* const kFade = R"LUA(
local function num(v, def)
    local s = string.gsub(tostring(v or ''), ',', '.')  -- "10,38": a decimal comma
    return tonumber(s) or def
end
function onEvent(name, v1, v2)
    local who, icon
    if name == 'Opponent Fade' then who, icon = 'dad', 'iconP2'
    elseif name == 'BF Fade' then who, icon = 'boyfriend', 'iconP1'
    else return end
    local dur = math.max(0, num(v1, 0))
    local a = num(v2, 1)
    if a > 1 then a = a / 100 end
    a = math.max(0, math.min(1, a))
    if dur <= 0 then
        cancelTween(who .. 'FadeEventTween')
        cancelTween(icon .. 'FadeEventTween')
        setProperty(who .. '.alpha', a)
        setProperty(icon .. '.alpha', a)
    else
        doTweenAlpha(who .. 'FadeEventTween', who, a, dur, 'linear')
        doTweenAlpha(icon .. 'FadeEventTween', icon, a, dur, 'linear')
    end
end
)LUA";

// -------------------------------------------------------------- Dek Engine --

const char* const kThreeTwoOne = R"LUA(
-- Vs Dek (Dek Engine): "Three Two One Go Event" plays the countdown again, one
-- part per event (value 1: 0 "three", 1 "ready", 2 "set", 3 "go"), as Dek
-- Engine's source does it.
local pixel = false
local n = 0
function onCreatePost()
    pixel = getPropertyFromClass('PlayState', 'isPixelStage') == true
end
function onEvent(name, v1, v2)
    if name ~= 'Three Two One Go Event' then return end
    local i = math.floor(tonumber(v1) or -1)
    local suffix = pixel and '-pixel' or ''
    local sounds = {[0] = 'intro3', [1] = 'intro2', [2] = 'intro1', [3] = 'introGo'}
    if sounds[i] then playSound(sounds[i] .. suffix, 0.6) end
    local images = pixel and {'pixelUI/ready-pixel', 'pixelUI/set-pixel', 'pixelUI/date-pixel'} or {'ready', 'set', 'go'}
    local img = images[i]
    if not img then return end
    n = n + 1
    local tag = 'dekCountdown' .. n
    makeLuaSprite(tag, img, 0, 0)
    setScrollFactor(tag, 0, 0)
    if pixel then
        scaleObject(tag, 6, 6)
        setProperty(tag .. '.antialiasing', false)
    end
    screenCenter(tag)
    setProperty(tag .. '.y', getProperty(tag .. '.y') + 100)  -- Dek moves it down, then fades it
    addLuaSprite(tag, true)
    doTweenAlpha(tag, tag, 0, crochet / 1000, 'cubeInOut')
end
function onTweenCompleted(tag)
    if string.sub(tag, 1, 12) == 'dekCountdown' then removeLuaSprite(tag, true) end
end
)LUA";

const char* const kDekSubtitle = R"LUA(
-- Vs Dek (Dek Engine): "Subtitle", value 1 the line, value 2 "add" (show it)
-- or "remove".
local shown = false
function onEvent(name, v1, v2)
    if name ~= 'Subtitle' then return end
    if shown then
        removeLuaText('dekSubtitle', true)
        shown = false
    end
    if string.lower(stringTrim(v2 or '')) == 'remove' then return end
    makeLuaText('dekSubtitle', v1 or '', 1180, 50, 470)
    setTextSize('dekSubtitle', 32)
    setTextAlignment('dekSubtitle', 'center')
    setTextBorder('dekSubtitle', 2, '000000')
    setObjectCamera('dekSubtitle', 'other')
    addLuaText('dekSubtitle')
    shown = true
end
)LUA";

// ----------------------------------------------------------- Denpa Engine --

const char* const kAddSubtitle = R"LUA(
-- Denpa Engine: "Add Subtitle", value 1 the line, value 2 "colour,steps": the
-- colour in hex or a character (dad, bf, gf: their health bar colour), shown
-- for that many steps.
local n = 0
local function charColor(who)
    local map = {dad = 'dad', opponent = 'dad', bf = 'boyfriend', boyfriend = 'boyfriend', gf = 'gf', girlfriend = 'gf'}
    local ch = map[who]
    if not ch then return nil end
    local c = getProperty(ch .. '.healthColorArray')
    if type(c) ~= 'table' then return 'FFFFFF' end
    return string.format('%02X%02X%02X', math.floor(c[1] or 255), math.floor(c[2] or 255), math.floor(c[3] or 255))
end
function onEvent(name, v1, v2)
    if name ~= 'Add Subtitle' then return end
    local parts = stringSplit(v2 or '', ',')
    local col = stringTrim(parts[1] or '')
    local steps = tonumber(stringTrim(parts[2] or '')) or 4
    local hex = charColor(string.lower(col))
    if not hex then
        hex = string.gsub(string.gsub(col, '^0[xX]', ''), '^#', '')
        if #hex == 8 then hex = string.sub(hex, 3) end
        if #hex ~= 6 then hex = 'FFFFFF' end
    end
    n = n + 1
    if luaTextExists('denpaSubtitle') then removeLuaText('denpaSubtitle', true) end
    makeLuaText('denpaSubtitle', v1 or '', 1180, 50, 470)
    setTextSize('denpaSubtitle', 28)
    setTextAlignment('denpaSubtitle', 'center')
    setTextBorder('denpaSubtitle', 2, '000000')
    setTextColor('denpaSubtitle', hex)
    setObjectCamera('denpaSubtitle', 'other')
    addLuaText('denpaSubtitle')
    runTimer('denpaSubtitle' .. n, math.max(0.05, steps * stepCrochet / 1000))
end
function onTimerCompleted(tag)
    if tag == 'denpaSubtitle' .. n and luaTextExists('denpaSubtitle') then
        doTweenAlpha('denpaSubtitleOut', 'denpaSubtitle', 0, 0.2, 'linear')
    end
end
)LUA";

const char* const kStageTint = R"LUA(
-- Denpa Engine: "Stage Tint", value 1 how strong (0 to 1), value 2
-- "steps,colour": a colour over the stage, behind the characters, eased to
-- that strength over those steps.
local made = false
local function make()
    if made then return end
    made = true
    makeLuaSprite('denpaStageTint', '', 0, 0)
    makeGraphic('denpaStageTint', 1280, 720, 'FFFFFF')
    setScrollFactor('denpaStageTint', 0, 0)
    scaleObject('denpaStageTint', 4, 4)
    screenCenter('denpaStageTint')
    setProperty('denpaStageTint.alpha', 0)
    addLuaSprite('denpaStageTint', false)
    local first = -1
    for _, g in ipairs({'gfGroup', 'dadGroup', 'boyfriendGroup'}) do
        local o = getObjectOrder(g)
        if o ~= nil and o >= 0 and (first < 0 or o < first) then first = o end
    end
    if first >= 0 then setObjectOrder('denpaStageTint', first) end
end
function onEventPushed(name)
    if name == 'Stage Tint' then make() end
end
function onEvent(name, v1, v2)
    if name ~= 'Stage Tint' then return end
    make()
    local parts = stringSplit(v2 or '', ',')
    local steps = tonumber(stringTrim(parts[1] or '')) or 0
    local col = stringTrim(parts[2] or '')
    if col == '' then col = '000000' end
    setProperty('denpaStageTint.color', getColorFromHex(col))
    local a = math.max(0, math.min(1, tonumber(v1) or 0))
    if steps > 0 then
        doTweenAlpha('denpaStageTint', 'denpaStageTint', a, steps * stepCrochet / 1000, 'linear')
    else
        cancelTween('denpaStageTint')
        setProperty('denpaStageTint.alpha', a)
    end
end
)LUA";

const char* const kSetCamSpeed = R"LUA(
-- Denpa Engine "Set Cam Speed" and SCReboot "Change the Camera Speed": how
-- fast the camera follows (value 1, 1 normal).
function onEvent(name, v1, v2)
    if name == 'Set Cam Speed' or name == 'Change the Camera Speed' then
        setProperty('cameraSpeed', tonumber(v1) or 1)
    end
end
)LUA";

// ------------------------------------------------------------------ flashes --

const char* const kFlashCamera = R"LUA(
-- "Flash Camera" (Denpa Engine and others): value 1 seconds, value 2 "colour"
-- or "colour, camera" (white on the game camera when empty).
function onEvent(name, v1, v2)
    if name ~= 'Flash Camera' or not flashingLights then return end
    local parts = stringSplit(v2 or '', ',')
    local col = stringTrim(parts[1] or '')
    local cam = stringTrim(parts[2] or '')
    if col == '' then col = 'FFFFFF' end
    if cam == '' then cam = 'camGame' end
    cameraFlash(cam, col, tonumber(v1) or 1, true)
end
)LUA";

const char* const kCameraFlash = R"LUA(
-- "Camera Flash": value 1 seconds, value 2 the colour (white when empty).
function onEvent(name, v1, v2)
    if name ~= 'Camera Flash' or not flashingLights then return end
    local col = stringTrim(v2 or '')
    if col == '' then col = 'FFFFFF' end
    cameraFlash('camGame', col, tonumber(v1) or 0.5, true)
end
)LUA";

const char* const kRedFlash = R"LUA(
-- SCReboot: "thearchyRedFlash" / "Thearchy Red Flash", a red flash a beat long.
function onEvent(name, v1, v2)
    if (name == 'thearchyRedFlash' or name == 'Thearchy Red Flash') and flashingLights then
        cameraFlash('camGame', 'FF0000', crochet / 1000, true)
    end
end
)LUA";

// ----------------------------------------------------------------- camera --

const char* const kCameraZoom = R"LUA(
-- "CameraZoom" (Hortas edition): the camera's resting zoom becomes value 1.
function onEvent(name, v1, v2)
    if name == 'CameraZoom' and tonumber(v1) then setProperty('defaultCamZoom', tonumber(v1)) end
end
)LUA";

const char* const kDefaultZoom = R"LUA(
-- SCReboot: "Change the Default Camera Zoom" (and its "DO NOT Set Cam Zoom"
-- test copy): the resting zoom becomes value 1, eased over value 2 seconds
-- (at once when empty).
function onEvent(name, v1, v2)
    if name ~= 'Change the Default Camera Zoom' and name ~= 'DO NOT Set Cam Zoom (testing...)' then return end
    local z = tonumber(v1)
    if not z then return end
    local dur = tonumber(v2) or 0
    cancelTween('ghDefaultZoom')
    if dur <= 0 then
        setProperty('defaultCamZoom', z)
    else
        startTween('ghDefaultZoom', '', {defaultCamZoom = z}, dur, {ease = 'sineInOut'})
    end
end
)LUA";

const char* const kCameraMiddle = R"LUA(
-- SCReboot: "the camera move middle", value 1 1: the camera looks between the
-- two singers until value 1 0.
function onEvent(name, v1, v2)
    if name ~= 'the camera move middle' then return end
    if (tonumber(v1) or 0) == 1 then
        local x = (getMidpointX('dad') + 150 + getMidpointX('boyfriend') - 100) / 2
        local y = (getMidpointY('dad') + getMidpointY('boyfriend')) / 2 - 100
        setProperty('isCameraOnForcedPos', true)
        setProperty('camFollow.x', x)
        setProperty('camFollow.y', y)
    else
        setProperty('isCameraOnForcedPos', false)
    end
end
)LUA";

const char* const kCinematics = R"LUA(
-- "Cinematics": black bars at the top and bottom, the HUD fading out and the
-- arrows moving between the bars. Value 1 "1" (or "1,seconds") brings them
-- in, value 2 "2" (or "2,seconds") takes them away (3 seconds when not given).
local made = false
local function make()
    if made then return end
    made = true
    makeLuaSprite('UpperBar', '', 0, -120)
    makeGraphic('UpperBar', 1920, 120, '000000')
    setObjectCamera('UpperBar', 'hud')
    addLuaSprite('UpperBar', false)
    makeLuaSprite('LowerBar', '', 0, 720)
    makeGraphic('LowerBar', 1920, 240, '000000')
    setObjectCamera('LowerBar', 'hud')
    addLuaSprite('LowerBar', false)
end
function onCreatePost() make() end
local hud = {'healthBarBG', 'healthBar', 'scoreTxt', 'iconP1', 'iconP2', 'timeBar', 'timeBarBG', 'timeTxt'}
local function parse(v)
    local parts = stringSplit(v or '', ',')
    return tonumber(stringTrim(parts[1] or '')), tonumber(stringTrim(parts[2] or ''))
end
local function notes(y, a, dur, prefix)
    for i = 0, getProperty('strumLineNotes.length') - 1 do
        if getPropertyFromGroup('strumLineNotes', i, 'alpha') > 0 and getPropertyFromGroup('strumLineNotes', i, 'visible') then
            noteTweenY('NOTEMOVE' .. i, i, y, dur, 'expoOut')
            noteTweenAlpha(prefix .. i, i, a, dur, 'expoOut')
        end
    end
end
function onEvent(name, v1, v2)
    if name ~= 'Cinematics' then return end
    make()
    local start, d1 = parse(v1)
    local finish, d2 = parse(v2)
    local dur = d1 or d2 or 3
    if start == 1 then
        doTweenY('Cinematics1', 'UpperBar', downscroll and -50 or 0, dur, 'expoOut')
        doTweenY('Cinematics2', 'LowerBar', downscroll and 650 or 600, dur, 'expoOut')
        notes(downscroll and 520 or 120, 0.7, dur, 'byeNote')
        for i, o in ipairs(hud) do doTweenAlpha('CinematicsHud' .. i, o, 0, i <= 5 and 1 or 0.25) end
    end
    if finish == 2 then
        doTweenY('Cinematics1', 'UpperBar', -120, dur, 'expoOut')
        doTweenY('Cinematics2', 'LowerBar', 720, dur, 'expoOut')
        notes(downscroll and 570 or 50, 1, dur, 'backNote')
        for i, o in ipairs(hud) do doTweenAlpha('CinematicsHud' .. i, o, 1, i <= 5 and 1 or 0.25) end
    end
end
)LUA";

// -------------------------------------------------------------------- HUD --

const char* const kHudFades = R"LUA(
-- The HUD's visibility: "Goodbye Hud" (value 1 alpha, value 2 seconds),
-- SCReboot's "Hide or Show HUD elements" (value 1: 0 hide, 1 show; "with
-- Fade": over half a second) and "Hide Hud" (1 hide, 2 show).
local function hudTo(a, dur)
    cancelTween('ghHudAlpha')
    if dur <= 0 then setProperty('camHUD.alpha', a)
    else doTweenAlpha('ghHudAlpha', 'camHUD', a, dur, 'linear') end
end
function onEvent(name, v1, v2)
    local v = tonumber(v1)
    if name == 'Goodbye Hud' then
        if v then hudTo(math.max(0, math.min(1, v)), tonumber(v2) or 0) end
    elseif name == 'Hide or Show HUD elements' then
        if v == 0 then hudTo(0, 0) elseif v == 1 then hudTo(1, 0) end
    elseif name == 'Hide or Show HUD elements with Fade' then
        if v == 0 then hudTo(0, 0.5) elseif v == 1 then hudTo(1, 0.5) end
    elseif name == 'Hide Hud' then
        if v == 1 then hudTo(0, 0) elseif v == 2 then hudTo(1, 0) end
    end
end
)LUA";

const char* const kFadeScreen = R"LUA(
-- SCReboot: "FadeScreen", the screen goes to black: value 1 how dark (0 to 1),
-- value 2 seconds.
local made = false
function onEvent(name, v1, v2)
    if name ~= 'FadeScreen' then return end
    if not made then
        made = true
        makeLuaSprite('ghFadeScreen', '', 0, 0)
        makeGraphic('ghFadeScreen', 1280, 720, '000000')
        setObjectCamera('ghFadeScreen', 'other')
        setProperty('ghFadeScreen.alpha', 0)
        addLuaSprite('ghFadeScreen', true)
    end
    local a = math.max(0, math.min(1, tonumber(v1) or 0))
    local dur = tonumber(v2) or 0
    cancelTween('ghFadeScreen')
    if dur <= 0 then setProperty('ghFadeScreen.alpha', a)
    else doTweenAlpha('ghFadeScreen', 'ghFadeScreen', a, dur, 'linear') end
end
)LUA";

const char* const kTexts = R"LUA(
-- SCReboot's lines on screen. "FadeText": value 1 shown, fading out over value
-- 2 seconds. "Lyrics - Add/Edit": value 2 added to line value 1; "Lyrics -
-- Reset" clears them.
local lines = {}
local function lyricTag(i) return 'ghLyrics' .. i end
local function showLyric(i)
    local tag = lyricTag(i)
    if not luaTextExists(tag) then
        makeLuaText(tag, '', 1180, 50, 440 + (i - 1) * 40)
        setTextSize(tag, 30)
        setTextAlignment(tag, 'center')
        setTextBorder(tag, 2, '000000')
        setObjectCamera(tag, 'other')
        addLuaText(tag)
    end
    setTextString(tag, lines[i] or '')
end
function onEvent(name, v1, v2)
    if name == 'FadeText' then
        if luaTextExists('ghFadeText') then removeLuaText('ghFadeText', true) end
        if (v1 or '') == '' then return end
        makeLuaText('ghFadeText', v1, 1180, 50, 320)
        setTextSize('ghFadeText', 40)
        setTextAlignment('ghFadeText', 'center')
        setTextBorder('ghFadeText', 2, '000000')
        setObjectCamera('ghFadeText', 'other')
        addLuaText('ghFadeText')
        doTweenAlpha('ghFadeText', 'ghFadeText', 0, math.max(0.05, tonumber(v2) or 1), 'linear')
    elseif name == 'Lyrics - Add/Edit' then
        local i = math.max(1, math.min(4, math.floor(tonumber(v1) or 1)))
        lines[i] = (lines[i] or '') .. (v2 or '')
        showLyric(i)
    elseif name == 'Lyrics - Reset' then
        for i = 1, 4 do
            if lines[i] then
                lines[i] = ''
                showLyric(i)
            end
        end
    end
end
)LUA";

const Named kScripts[] = {
    {"Blammed Lights", kBlammedLights},
    {"Gain", kGain},
    {"Opponent Fade", kFade},
    {"BF Fade", kFade},
    {"Three Two One Go Event", kThreeTwoOne},
    {"Subtitle", kDekSubtitle},
    {"Add Subtitle", kAddSubtitle},
    {"Stage Tint", kStageTint},
    {"Set Cam Speed", kSetCamSpeed},
    {"Change the Camera Speed", kSetCamSpeed},
    {"Flash Camera", kFlashCamera},
    {"Camera Flash", kCameraFlash},
    {"thearchyRedFlash", kRedFlash},
    {"Thearchy Red Flash", kRedFlash},
    {"CameraZoom", kCameraZoom},
    {"Change the Default Camera Zoom", kDefaultZoom},
    {"DO NOT Set Cam Zoom (testing...)", kDefaultZoom},
    {"the camera move middle", kCameraMiddle},
    {"Cinematics", kCinematics},
    {"Goodbye Hud", kHudFades},
    {"Hide or Show HUD elements", kHudFades},
    {"Hide or Show HUD elements with Fade", kHudFades},
    {"Hide Hud", kHudFades},
    {"FadeScreen", kFadeScreen},
    {"FadeText", kTexts},
    {"Lyrics - Add/Edit", kTexts},
    {"Lyrics - Reset", kTexts},
};

}  // namespace

const char* eventLua(std::string_view name) {
    for (const Named& n : kScripts)
        if (name == n.name) return n.text;
    return nullptr;
}

bool engineEvent(std::string_view name) {
    if (eventLua(name)) return true;
    return std::any_of(std::begin(kNative), std::end(kNative), [&](const char* n) { return name == n; });
}

}  // namespace gh::builtin
