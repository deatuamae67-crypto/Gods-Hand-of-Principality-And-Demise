# Lua API

Every function God's Hand gives Lua scripts (311), as Psych Engine names them, with what God's Hand adds.
Scripts go in a mod's `scripts/` (every song), `data/<song>/` (one song), `stages/<stage>.lua`, `custom_events/` and `custom_notetypes/`.
See also: [Callbacks](Callbacks.md), [HScript](HScript.md), [Shaders](Shaders.md).

**Contents:** [Sprites and objects](#sprites-and-objects) · [Animations](#animations) · [Text](#text) · [Properties](#properties) · [Tweens and timers](#tweens-and-timers) · [Cameras](#cameras) · [Shaders](#shaders) · [Screen effects (JS Engine)](#screen-effects-js-engine) · [Sounds and music](#sounds-and-music) · [Gameplay](#gameplay) · [Input](#input) · [Scripts](#scripts) · [Files and saves](#files-and-saves) · [Videos, dialogue and substates](#videos-dialogue-and-substates) · [Strings, numbers and colours](#strings-numbers-and-colours) · [Old engines (Kade, Psych 0.4-0.5)](#old-engines-kade-psych-04-05)

## Sprites and objects

| Function | What it does |
|---|---|
| `addInstance(tag, inFront)` | Puts an object made by createInstance in the game. |
| `addLuaSprite(tag, inFront)` | Puts the sprite in the game: behind the characters, or in front of everything. |
| `createInstance(tag, className, args)` | Psych 0.7.3: an object as `new className(args)` would make it: `Character` (x, y, name, isPlayer), `FlxSprite`, `FlxText`. |
| `getGraphicMidpointX(tag)` | The X of its picture's centre. |
| `getGraphicMidpointY(tag)` | The Y of its picture's centre. |
| `getMidpointX(tag)` | The X of its centre. |
| `getMidpointY(tag)` | The Y of its centre. |
| `getObjectOrder(tag)` | Its place in the drawing order. |
| `getPixelColor(tag, x, y)` | Always 0: pictures are not read back from the graphics card. |
| `getScreenPositionX(tag, camera)` | Where it is on screen (X). |
| `getScreenPositionY(tag, camera)` | Where it is on screen (Y). |
| `loadFrames(tag, image, spriteType)` | Another atlas for the sprite. |
| `loadGraphic(tag, image, gridX, gridY)` | Another picture for the sprite (a grid cuts it into frames). |
| `luaSpriteExists(tag)` | Whether a sprite with that tag exists. |
| `makeAnimatedLuaSprite(tag, image, x, y, spriteType)` | A sprite with a Sparrow (.xml) or Packer (.txt) atlas, for animations. |
| `makeGraphic(tag, width, height, color)` | Fills the sprite with one colour (`'FF0000'`, `'0xFFFF0000'`, a colour name). |
| `makeLuaSprite(tag, image, x, y)` | A sprite from `images/<image>.png` (no image: an empty one for makeGraphic). |
| `makeSprite(tag, image, x, y)` | Old name of makeLuaSprite. |
| `objectsOverlap(tag1, tag2)` | Whether the two objects overlap. |
| `precacheImage(image)` | Loads a picture now so it shows without a hitch later. |
| `removeFromGroup(group, index, dontDestroy)` | Takes a member out of a group (notes, strums...). |
| `removeLuaSprite(tag, destroy)` | Takes it out of the game (destroy: forget it too). |
| `scaleObject(tag, x, y, updateHitbox)` | Scales it (and its hitbox, unless updateHitbox is false). |
| `screenCenter(tag, axis)` | Centres it on screen: `'x'`, `'y'` or `'xy'`. |
| `setBlendMode(tag, mode)` | `'add'`, `'multiply'`, `'screen'`, `'subtract'` or `'normal'`. |
| `setGraphicSize(tag, width, height, updateHitbox)` | Scales it to a size in pixels (0 keeps the proportion). |
| `setObjectCamera(tag, camera)` | `'game'`, `'hud'` or `'other'`: the camera that draws it. |
| `setObjectOrder(tag, position)` | Its place in the drawing order (0 is the back). A sprite made but not added is added there, as in Psych. |
| `setScrollFactor(tag, x, y)` | How much it moves with the camera (0: stays on screen, 1: with the world). |
| `updateHitbox(tag)` | Recomputes its size and offset after a scale change. |
| `updateHitboxFromGroup(group, index)` | Accepted for compatibility; does nothing in God's Hand. |

## Animations

| Function | What it does |
|---|---|
| `addAnimation(tag, name, frames, fps, loop)` | An animation from frame numbers. |
| `addAnimationByIndices(tag, name, prefix, indices, fps, loop)` | An animation from some frames of a prefix (`'0,1,2,3'` or a table). |
| `addAnimationByIndicesLoop(tag, name, prefix, indices, fps)` | The same, looping. |
| `addAnimationByPrefix(tag, name, prefix, fps, loop)` | An animation from the atlas frames whose name starts with prefix. |
| `addOffset(tag, anim, x, y)` | The offset used while that animation plays. |
| `characterDance(character)` | The character goes back to its idle dance. |
| `characterPlayAnim(character, anim, forced)` | `'boyfriend'`, `'dad'` or `'gf'` plays an animation. |
| `objectPlayAnimation(tag, anim, forced, startFrame)` | Old name of playAnim. |
| `playAnim(tag, anim, forced, reversed, startFrame)` | Plays an animation (characters keep their own offsets). |

## Text

| Function | What it does |
|---|---|
| `addLuaText(tag)` | Puts the text in the game. |
| `getTextFont(tag)` | Its font. |
| `getTextSize(tag)` | Its size. |
| `getTextString(tag)` | Its text. |
| `getTextWidth(tag)` | Its width. |
| `luaTextExists(tag)` | Whether a text with that tag exists. |
| `makeLuaText(tag, text, width, x, y)` | A text (on the HUD camera). |
| `removeLuaText(tag, destroy)` | Takes it out. |
| `setTextAlignment(tag, align)` | `'left'`, `'center'` or `'right'`. |
| `setTextBorder(tag, size, color)` | Its outline. |
| `setTextColor(tag, color)` | Its colour. |
| `setTextFont(tag, font)` | A font from `fonts/`. |
| `setTextItalic(tag, italic)` | Italic or not. |
| `setTextSize(tag, size)` | Its size. |
| `setTextString(tag, text)` | Its text. |
| `setTextWidth(tag, width)` | Its box width (0: as wide as the text). |

## Properties

| Function | What it does |
|---|---|
| `callMethod(path, args)` | Psych 0.7: calls a method of an object (`'boyfriend.playAnim'`, {'hey', true}). |
| `callMethodFromClass(class, method, args)` | Psych 0.7: calls a static method. |
| `getProperty(path)` | Any value by path: `'boyfriend.x'`, `'camGame.zoom'`, `'iconP1.alpha'`, `'tag.scale.x'`, `'playerStrums.members[0].y'`... |
| `getPropertyFromClass(class, field)` | A static field: `ClientPrefs.downScroll`, `Conductor.songPosition`, `flixel.FlxG.camera.zoom`... |
| `getPropertyFromGroup(group, index, field)` | A field of a member: `notes`, `unspawnNotes`, `playerStrums`, `opponentStrums`, `strumLineNotes`... |
| `getVar(name)` | Reads it. |
| `removeVar(name)` | Forgets it. |
| `setProperty(path, value)` | Changes it (numbers given as text, and true/false, are converted as in Psych). |
| `setPropertyFromClass(class, field, value)` | Changes it. |
| `setPropertyFromGroup(group, index, field, value)` | Changes it. |
| `setVar(name, value)` | A variable shared by every script (Lua and HScript). |

## Tweens and timers

| Function | What it does |
|---|---|
| `cancelTimer(tag)` | Stops a timer. |
| `cancelTween(tag)` | Stops a tween. |
| `doTweenAlpha(tag, object, value, duration, ease)` | Moves the object's alpha to value over duration seconds (ease: `'linear'`, `'quadOut'`, `'circInOut'`...). Reaches HUD objects too (`iconP1`, `healthBar`, `scoreTxt`...); onTweenCompleted(tag) when done. |
| `doTweenAngle(tag, object, value, duration, ease)` | Moves the object's angle to value over duration seconds (ease: `'linear'`, `'quadOut'`, `'circInOut'`...). Reaches HUD objects too (`iconP1`, `healthBar`, `scoreTxt`...); onTweenCompleted(tag) when done. |
| `doTweenColor(tag, object, color, duration, ease)` | Its colour to another. |
| `doTweenScale(tag, object, value, duration, ease)` | Both scales at once. |
| `doTweenScaleX(tag, object, value, duration, ease)` | Moves the object's scale.x to value over duration seconds (ease: `'linear'`, `'quadOut'`, `'circInOut'`...). Reaches HUD objects too (`iconP1`, `healthBar`, `scoreTxt`...); onTweenCompleted(tag) when done. |
| `doTweenScaleY(tag, object, value, duration, ease)` | Moves the object's scale.y to value over duration seconds (ease: `'linear'`, `'quadOut'`, `'circInOut'`...). Reaches HUD objects too (`iconP1`, `healthBar`, `scoreTxt`...); onTweenCompleted(tag) when done. |
| `doTweenX(tag, object, value, duration, ease)` | Moves the object's x to value over duration seconds (ease: `'linear'`, `'quadOut'`, `'circInOut'`...). Reaches HUD objects too (`iconP1`, `healthBar`, `scoreTxt`...); onTweenCompleted(tag) when done. |
| `doTweenY(tag, object, value, duration, ease)` | Moves the object's y to value over duration seconds (ease: `'linear'`, `'quadOut'`, `'circInOut'`...). Reaches HUD objects too (`iconP1`, `healthBar`, `scoreTxt`...); onTweenCompleted(tag) when done. |
| `doTweenZoom(tag, object, value, duration, ease)` | Moves the object's zoom to value over duration seconds (ease: `'linear'`, `'quadOut'`, `'circInOut'`...). Reaches HUD objects too (`iconP1`, `healthBar`, `scoreTxt`...); onTweenCompleted(tag) when done. |
| `noteTweenAlpha(tag, note, value, duration, ease)` | A receptor's alpha (0-3 the opponent's, 4-7 yours). |
| `noteTweenAngle(tag, note, value, duration, ease)` | A receptor's angle (0-3 the opponent's, 4-7 yours). |
| `noteTweenDirection(tag, note, value, duration, ease)` | A receptor's direction (0-3 the opponent's, 4-7 yours). |
| `noteTweenScaleX(tag, note, value, duration, ease)` | A receptor's scale.x (0-3 the opponent's, 4-7 yours). |
| `noteTweenScaleY(tag, note, value, duration, ease)` | A receptor's scale.y (0-3 the opponent's, 4-7 yours). |
| `noteTweenX(tag, note, value, duration, ease)` | A receptor's x (0-3 the opponent's, 4-7 yours). |
| `noteTweenY(tag, note, value, duration, ease)` | A receptor's y (0-3 the opponent's, 4-7 yours). |
| `runTimer(tag, time, loops)` | onTimerCompleted(tag, loopsDone, loopsLeft) after time seconds, loops times (0: forever). |
| `startTween(tag, object, values, duration, options)` | Psych 0.7: several fields at once (`{x = 100, ['scale.x'] = 2}`); options: ease, startDelay, onComplete. |

## Cameras

| Function | What it does |
|---|---|
| `addCamera(tag)` | Accepted for compatibility; does nothing in God's Hand: the game draws with its three cameras (game, hud, other). |
| `addCameraFollowPoint(x, y)` | Moves that point. |
| `addCameraScroll(x, y)` | Moves it. |
| `cameraFade(camera, color, duration, forced)` | Fades it to a colour. |
| `cameraFlash(camera, color, duration, forced)` | Flashes a colour over it. |
| `cameraSetTarget(target)` | `'boyfriend'`, `'dad'` or `'gf'`: where the camera looks. |
| `cameraShake(camera, intensity, duration)` | Shakes `'game'`, `'hud'` or `'other'`. |
| `createCamera(tag)` | Accepted for compatibility; does nothing in God's Hand: the game draws with its three cameras (game, hud, other). |
| `getCameraFollowX()` | Where the camera heads (X). |
| `getCameraFollowY()` | Where the camera heads (Y). |
| `getCameraScrollX()` | Its scroll (X). |
| `getCameraScrollY()` | Its scroll (Y). |
| `removeCamera(tag)` | Accepted for compatibility; does nothing in God's Hand. |
| `setCameraFollowPoint(x, y)` | Where the camera heads. |
| `setCameraScroll(x, y)` | Where the camera is now. |

## Shaders

| Function | What it does |
|---|---|
| `addSpriteShader(object, shader, position)` | One more shader layer (last, or at position from 1): each layer works on what the one before it drew. Returns its position. |
| `clearShadersFromCamera(camera)` | Every shader off a camera. |
| `createRuntimeShader(name)` | Same as initLuaShader. |
| `getShaderBool(object, uniform, layer)` | Reads it (the first layer, or the one named). |
| `getShaderBoolArray(object, uniform, layer)` | Reads it (the first layer, or the one named). |
| `getShaderFloat(object, uniform, layer)` | Reads it (the first layer, or the one named). |
| `getShaderFloatArray(object, uniform, layer)` | Reads it (the first layer, or the one named). |
| `getShaderInt(object, uniform, layer)` | Reads it (the first layer, or the one named). |
| `getShaderIntArray(object, uniform, layer)` | Reads it (the first layer, or the one named). |
| `getSpriteShaders(object)` | The shader names, first layer first. |
| `initLuaShader(name)` | Loads `shaders/<name>.frag`: true when found. Sprites are drawn with the game's own vertex shader (a `.vert` is not used). Shaders show while Options → GRAPHICS → Shaders is on. |
| `moveSpriteShader(object, from, to)` | A layer to another position. |
| `removeCameraShader(camera, index)` | Takes it off. |
| `removeSpriteShader(object, layer)` | Every shader (Psych), or only one layer: its position, or every layer of a shader name. |
| `setCameraShader(camera, shader, index)` | JS Engine: a mod shader on a camera; index names it for setShaderFloat (default: its name). |
| `setShaderBool(object, uniform, value, layer)` | A bool uniform of the object's shaders (every layer, or the layer named: its position or shader name). |
| `setShaderBoolArray(object, uniform, value, layer)` | A boolArray uniform of the object's shaders (every layer, or the layer named: its position or shader name). |
| `setShaderFloat(object, uniform, value, layer)` | A float uniform of the object's shaders (every layer, or the layer named: its position or shader name). |
| `setShaderFloatArray(object, uniform, value, layer)` | A floatArray uniform of the object's shaders (every layer, or the layer named: its position or shader name). |
| `setShaderInt(object, uniform, value, layer)` | A int uniform of the object's shaders (every layer, or the layer named: its position or shader name). |
| `setShaderIntArray(object, uniform, value, layer)` | A intArray uniform of the object's shaders (every layer, or the layer named: its position or shader name). |
| `setShaderSampler2D(object, uniform, image)` | A texture uniform (an image of the mod). |
| `setSpriteShader(object, shader)` | The object's only shader (replaces every layer). |
| `setSpriteShaderOrder(object, names)` | Those layers first, in that order (`{'glow', 'wave'}`), the others after. |

## Screen effects (JS Engine)

| Function | What it does |
|---|---|
| `add3DEffect(camera, xrotation, yrotation, zrotation, depth)` | A 3D tilt of the whole camera. |
| `addBlockedGlitchEffect(camera, resolution, time, colorMultiplier, colorTransform)` | Blocky glitch, as JS Engine draws it (only with colorTransform on). |
| `addBloomEffect(camera, intensity, blurSize)` | Glow (0.35, 1). |
| `addChromaticAbberationEffect(camera, offset)` | The same (JS Engine's spelling). |
| `addChromaticAberrationEffect(camera, offset)` | Red and blue pulled apart by offset (0.005). |
| `addDistortionEffect(camera, waveSpeed, waveFrq, waveAmp)` | Distortion. |
| `addGlitchEffect(camera, waveSpeed, waveFrq, waveAmp)` | Wavy glitch. |
| `addGlitchShader(camera, amount)` | Glitch. |
| `addGrainEffect(camera, grainSize, lumAmount, lockAlpha)` | Film grain. |
| `addGrayscaleEffect(camera)` | Black and white. |
| `addGreyscaleEffect(camera)` | The same. |
| `addInvertEffect(camera, lockAlpha)` | Inverted colours. |
| `addPulseEffect(camera, waveSpeed, waveFrq, waveAmp)` | Pulse. |
| `addScanlineEffect(camera, lockAlpha)` | CRT lines. |
| `addTiltshiftEffect(camera, blurAmount, center)` | Tilt-shift blur. |
| `addVCREffect(camera, glitchFactor, distortion, perspectiveOn, vignetteMoving)` | VHS look: glitches, a curved picture, a moving vignette and scanlines. |
| `addWiggleEffect(camera, type, speed, frequency, amplitude, verticalStrength, horizontalStrength)` | Wavy picture: type `'dreamy'`, `'wavy'`, `'heat_wave_horizontal'`, `'heat_wave_vertical'`, `'heat_wave_both'` or `'flag'`. |
| `clearEffects(camera)` | Every effect off. |

## Sounds and music

| Function | What it does |
|---|---|
| `addPlaybackSpeed(amount)` | Accepted for compatibility; does nothing in God's Hand. |
| `getPlaybackSpeed()` | Its speed. |
| `getSoundPitch(tag)` | Its pitch. |
| `getSoundTime(tag)` | Where it is (ms). |
| `getSoundVolume(tag)` | Its volume. |
| `luaSoundExists(tag)` | Whether it exists. |
| `musicFadeIn(duration, from, to)` | The music fades in (volumes 0 to 1). |
| `musicFadeOut(duration, to)` | The music fades out. |
| `pauseSound(tag)` | Pauses it. |
| `playMusic(sound, volume, loop)` | Music from `music/`. |
| `playSound(sound, volume, tag)` | A sound from `sounds/`; with a tag it can be stopped and changed; onSoundFinished(tag). |
| `precacheMusic(music)` | Accepted for compatibility; does nothing in God's Hand: music loads when first played. |
| `precacheSound(sound)` | Accepted for compatibility; does nothing in God's Hand: sounds load when first played, fast enough not to hitch. |
| `resumeSound(tag)` | Plays on. |
| `setPlaybackSpeed(rate)` | Accepted for compatibility; does nothing in God's Hand: the speed is chosen in Freeplay (getPlaybackSpeed reads it). |
| `setSoundPitch(tag, pitch)` | Its pitch. |
| `setSoundTime(tag, ms)` | Jumps. |
| `setSoundVolume(tag, volume)` | Its volume. |
| `soundFadeCancel(tag)` | Stops a fade. |
| `soundFadeIn(tag, duration, from, to)` | Fades it in. |
| `soundFadeOut(tag, duration, to)` | Fades it out. |
| `stopSound(tag)` | Stops it. |

## Gameplay

| Function | What it does |
|---|---|
| `addCharacterToList(name, type)` | Loads a character ahead (for Change Character events). |
| `addCombo(value)` | Adds to the combo. |
| `addHealth(value)` | Adds to it. |
| `addHits(value)` | Adds to them. |
| `addMisses(value)` | Adds to them. |
| `addNPS(value)` | Accepted for compatibility; does nothing in God's Hand: the game counts the notes per second itself. |
| `addScore(value)` | Adds to it. |
| `changeCharacter(type, name)` | Swaps a character. |
| `changeMaxHealth(value)` | Accepted for compatibility; does nothing in God's Hand: the most health stays 2. |
| `endSong()` | Ends the song now. |
| `exitSong()` | Back to the menus. |
| `getCharacterX(type)` | Where a character is. |
| `getCharacterY(type)` | Where a character is. |
| `getHealth()` | Health (0-2). |
| `getHits()` | Hits. |
| `getMaxHealth()` | The most there can be. |
| `getMisses()` | Misses. |
| `getScore()` | The score. |
| `getScreenHeight()` | The game's height: 720. |
| `getScreenWidth()` | The game's width: 1280 (the window scales it). |
| `getSongPosition()` | Where the song is (ms). |
| `loadSong(name, difficulty)` | Plays another song of the mod (difficulty: its number, or the current one). |
| `loopTheSong(ms)` | JS Engine: the song goes back to ms (0: its start), with its notes and events. |
| `playsAsBF()` | Whether you play the boyfriend's side. |
| `restartSong()` | Starts the song again. |
| `setCharacterX(type, x)` | Moves a character. |
| `setCharacterY(type, y)` | Moves a character. |
| `setHealth(value)` | Sets it. |
| `setHealthBarColors(left, right)` | The health bar's colours. |
| `setHits(value)` | Sets them. |
| `setMisses(value)` | Sets them. |
| `setRatingFC(text)` | The FC text shown. |
| `setRatingName(name)` | The rating name shown. |
| `setRatingPercent(value)` | The accuracy shown. |
| `setScore(value)` | Sets it. |
| `setTimeBarColors(left, right)` | The time bar's colours. |
| `startCountdown()` | Starts the countdown (after onStartCountdown returned Function_Stop). |
| `triggerEvent(name, value1, value2)` | Runs a chart event now. |

## Input

| Function | What it does |
|---|---|
| `anyGamepadJustPressed(button)` | Any gamepad. |
| `anyGamepadPressed(button)` | Any gamepad. |
| `anyGamepadReleased(button)` | Any gamepad. |
| `changeCursor(image)` | Accepted for compatibility; does nothing in God's Hand: the system cursor stays. |
| `gamepadAnalogX(id, leftStick)` | A stick (X), -1 to 1: the left one unless leftStick is false. |
| `gamepadAnalogY(id, leftStick)` | A stick (Y), -1 to 1. |
| `gamepadJustPressed(id, button)` | A gamepad button. |
| `gamepadPressed(id, button)` | Held. |
| `gamepadReleased(id, button)` | Released. |
| `getMouseX(camera)` | The mouse (X), in game pixels (1280 wide). |
| `getMouseY(camera)` | The mouse (Y), in game pixels (720 high). |
| `keyboardJustPressed(key)` | A key by name (`'SPACE'`, `'A'`, `'F1'`...). |
| `keyboardPressed(key)` | Held. |
| `keyboardReleased(key)` | Released. |
| `keyJustPressed(name)` | Psych controls: `'left'`, `'down'`, `'up'`, `'right'`, `'accept'`, `'back'`, `'pause'`, `'reset'`, `'space'`: pressed this frame. |
| `keyPressed(name)` | Held. |
| `keyReleased(name)` | Released this frame. |
| `mouseClicked(button)` | `'left'`, `'right'` or `'middle'`. |
| `mousePressed(button)` | Held. |
| `mouseReleased(button)` | Released. |

## Scripts

| Function | What it does |
|---|---|
| `addHaxeLibrary(class, package)` | Returns true: HScript already knows the classes it supports (see HScript). |
| `addLuaScript(path)` | Runs another Lua script. |
| `callOnLuas(function, args)` | Calls a function in every Lua script. |
| `callOnScripts(function, args)` | In every script (Lua and HScript). |
| `callScript(script, function, args)` | Calls a function of another script. |
| `close()` | Stops this script. |
| `debugPrint(...)` | Text on screen (and in the log). |
| `getGlobalFromScript(script, name)` | A global of another script. |
| `getModSetting(name, mod)` | A value of the mod's data/settings.json. |
| `getRunningScripts()` | The scripts running. |
| `isRunning(script)` | Whether a script runs. |
| `removeLuaScript(path)` | Stops it. |
| `require(module)` | A Lua module of the mod. |
| `runHaxeCode(code)` | Runs HScript (see the HScript page) and returns its last value. |
| `runHaxeFunction(name, args)` | Calls an HScript function. |
| `setGlobalFromScript(script, name, value)` | Sets it. |
| `setOnHScript(name, value)` | In every HScript. |
| `setOnLuas(name, value)` | A global in every Lua script. |
| `setOnScripts(name, value)` | In every script. |
| `trace(...)` | Text in the log. |

## Files and saves

| Function | What it does |
|---|---|
| `checkFileExists(path)` | Whether the mod (or the game) has that file. |
| `deleteFile(path)` | Deletes a file; only inside the mods folder. |
| `directoryFileList(path)` | The files in a folder. |
| `eraseSaveData(name)` | Deletes it. |
| `flushSaveData(name)` | Writes it. |
| `getDataFromSave(name, field, default)` | Reads a field. |
| `getTextFromFile(path)` | A text file of the mod. |
| `initSaveData(name, folder)` | A save of the mod. |
| `saveFile(path, content)` | Writes a file; only inside the mods folder. |
| `setDataFromSave(name, field, value)` | Writes a field. |

## Videos, dialogue and substates

| Function | What it does |
|---|---|
| `closeCustomSubstate()` | Closes it. |
| `insertToCustomSubstate(tag)` | Puts an object in the open custom substate. |
| `makeVideoSprite(tag, video, x, y, camera, loop)` | A video as a sprite. |
| `openCustomSubstate(name, pause)` | A substate of your own (its callbacks: onCustomSubstate...). |
| `startDialogue(file, music)` | No dialogue boxes yet: the song goes on as if the dialogue had been read. |
| `startVideo(name)` | Plays `videos/<name>.mp4` (onVideoFinished when done). |

## Strings, numbers and colours

| Function | What it does |
|---|---|
| `FlxColor(name)` | A colour by name, as FlxColor. |
| `getColorFromHex(hex)` | A colour number from `'FF0000'`. |
| `getColorFromName(name)` | A colour by name. |
| `getColorFromString(text)` | A colour from any form. |
| `getRandomBool(chance)` | true chance% of the time. |
| `getRandomFloat(min, max, exclude)` | A random number. |
| `getRandomInt(min, max, exclude)` | A random whole number (exclude: `'1,2'`). |
| `stringEndsWith(text, end)` | Whether it ends with. |
| `stringSplit(text, separator)` | A table of the parts. |
| `stringStartsWith(text, start)` | Whether it starts with. |
| `stringTrim(text)` | Without spaces at the ends. |

## Old engines (Kade, Psych 0.4-0.5)

| Function | What it does |
|---|---|
| `changeDiscordClientID(id)` | Accepted for compatibility; does nothing in God's Hand (no Discord status). |
| `changeDiscordPresence(details, state)` | Accepted for compatibility; does nothing in God's Hand (no Discord status). |
| `changePresence(details, state)` | Accepted for compatibility; does nothing in God's Hand (no Discord status). |
| `getActorAlpha(actor)` | Kade. |
| `getActorAngle(actor)` | Kade. |
| `getActorHeight(actor)` | Kade. |
| `getActorWidth(actor)` | Kade. |
| `getActorX(actor)` | Kade: getProperty(actor .. '.x'). |
| `getActorY(actor)` | Kade. |
| `getPropertyLuaSprite(tag, field)` | Psych 0.4: getProperty(tag .. '.' .. field). |
| `luaSpriteAddAnimationByIndices(tag, name, prefix, indices, fps)` | Psych 0.4. |
| `luaSpriteAddAnimationByPrefix(tag, name, prefix, fps, loop)` | Psych 0.4. |
| `luaSpriteMakeGraphic(tag, w, h, color)` | Psych 0.4: makeGraphic. |
| `luaSpritePlayAnimation(tag, name, forced)` | Psych 0.4. |
| `scaleLuaSprite(tag, x, y)` | Psych 0.4: scaleObject. |
| `setActorAlpha(alpha, actor)` | Kade. |
| `setActorAngle(angle, actor)` | Kade. |
| `setActorFlipX(flip, actor)` | Kade. |
| `setActorScale(scale, actor)` | Kade. |
| `setActorScaleXY(x, y, actor)` | Kade. |
| `setActorX(x, actor)` | Kade. |
| `setActorY(y, actor)` | Kade. |
| `setCamAngle(angle)` | Kade. |
| `setCamZoom(zoom)` | Kade. |
| `setHudAngle(angle)` | Kade. |
| `setHudZoom(zoom)` | Kade. |
| `setLuaSpriteCamera(tag, camera)` | Psych 0.4: setObjectCamera. |
| `setLuaSpriteScrollFactor(tag, x, y)` | Psych 0.4: setScrollFactor. |
| `setPropertyLuaSprite(tag, field, value)` | Psych 0.4: setProperty. |
| `tweenAngle(actor, angle, time, onComplete)` | Kade. |
| `tweenCameraAngle(angle, time, onComplete)` | Kade. |
| `tweenCameraPos(x, y, time, onComplete)` | Kade. |
| `tweenCameraZoom(zoom, time, onComplete)` | Kade. |
| `tweenFadeIn(actor, alpha, time, onComplete)` | Kade. |
| `tweenFadeOut(actor, alpha, time, onComplete)` | Kade. |
| `tweenHudAngle(angle, time, onComplete)` | Kade. |
| `tweenHudZoom(zoom, time, onComplete)` | Kade. |
| `tweenPos(actor, x, y, time, onComplete)` | Kade. |
| `tweenPosXAngle(actor, x, angle, time, onComplete)` | Kade. |
| `tweenPosYAngle(actor, y, angle, time, onComplete)` | Kade. |

## Variables

Set before onCreate and kept up to date (as in Psych 0.7.3):

- **Song:** `songName`, `songPath`, `curBpm`, `bpm`, `scrollSpeed`, `crochet`, `stepCrochet`, `songLength`, `curStage`, `isStoryMode`, `difficulty`, `difficultyName`, `difficultyPath`, `week`, `weekRaw`, `seenCutscene`, `hasVocals`, `playbackRate`, `inChartEditor`, `version` (`'0.7.3'`).
- **Timing:** `curSection`, `curBeat`, `curStep`, `curDecBeat`, `curDecStep`, `songPosition`, `startedCountdown`, `mustHitSection`, `altAnim`, `gfSection`, `inGameOver`.
- **Score:** `score`, `misses`, `hits`, `combo`, `rating`, `ratingName`, `ratingFC`, `healthGainMult`, `healthLossMult`, `instakillOnMiss`, `guitarHeroSustains`, `botPlay`, `practice`.
- **Positions:** `defaultBoyfriendX/Y`, `defaultOpponentX/Y`, `defaultGirlfriendX/Y`, `defaultPlayerStrumX0`..`3` and `Y0`..`3`, `defaultOpponentStrumX0`..`3` and `Y0`..`3`, `cameraX`, `cameraY`, `screenWidth`, `screenHeight`.
- **Characters:** `boyfriendName`, `dadName`, `gfName`.
- **Options:** `downscroll`, `middlescroll`, `framerate`, `ghostTapping`, `hideHud`, `timeBarType`, `scoreZoom`, `cameraZoomOnBeat`, `flashingLights`, `noteOffset`, `healthBarAlpha`, `noResetButton`, `lowQuality`, `shadersEnabled`, `noteSkin`, `noteSkinPostfix`, `splashSkin`, `splashSkinPostfix`, `splashAlpha`, `currentModDirectory`.
- **Scripts:** `scriptName`, `luaDebugMode`, `luaDeprecatedWarnings`, and what callbacks return: `Function_Continue`, `Function_Stop`, `Function_StopLua`, `Function_StopHScript`, `Function_StopAll`.

Lua is Lua 5.4 with its base, table, string, math, utf8 and coroutine libraries, plus the old names Psych mods use (`unpack`, `loadstring`, `bit`, `math.pow`, `table.getn`...). `io`, `debug`, `dofile` and `loadfile` are not there; `os` has only `clock` (seconds since the game started, as on Windows), `time` and `date`; `require` loads modules of the mod. A call that runs too long is stopped with a warning instead of freezing the game.
