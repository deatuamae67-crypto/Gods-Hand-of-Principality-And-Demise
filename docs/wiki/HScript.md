# HScript

HScript is the part of Haxe that Psych Engine runs as scripts. God's Hand has its own HScript interpreter in C++, wired to the same scene the Lua API uses, so Lua and HScript see and change the same objects. See also: [Lua API](Lua-API.md), [Callbacks](Callbacks.md).

## Where it runs

- **From Lua**: `runHaxeCode(code)` runs code and returns its last value; `runHaxeFunction(name, args)` calls a function defined that way. As in Psych 0.7, `createCallback(name, function)` in HScript makes a Lua function of that name.
- **Script files**: `.hx`, `.hscript` and `.hxs` files go in the same folders as Lua scripts (`scripts/`, `data/<song>/`, `stages/`, `characters/`, `custom_events/`, `custom_notetypes/`) and get the same [callbacks](Callbacks.md).

```haxe
// scripts/flash.hx
var flash:FlxSprite;

function onCreatePost() {
    flash = new FlxSprite(0, 0).makeGraphic(FlxG.width, FlxG.height, FlxColor.WHITE);
    flash.cameras = [game.camHUD];
    flash.alpha = 0;
    game.add(flash);
}

function onBeatHit() {
    if (curBeat % 4 == 0) {
        flash.alpha = 0.6;
        FlxTween.tween(flash, {alpha: 0}, 0.4, {ease: FlxEase.quadOut});
    }
}
```

## The language

- `var` and `final`, with or without types (`var x:Int = 0`); types are read and ignored, as HScript does.
- Functions: named, anonymous (`function(a) return a * 2`), arrow functions (`(a) -> a * 2`), closures that keep their variables, optional and default arguments, and bodies without braces (`function f() return 1;`).
- `if` / `else`, `while`, `do ... while`, `for (i in 0...10)`, `for (x in array)`, `for (k in map.keys())`, `break`, `continue`, `return`.
- `switch` with several values per `case`, `default`, and switch as an expression.
- `try` / `catch (e:Dynamic)`, `throw`.
- Operators: arithmetic, `%`, bit operators, `<<`, `>>`, `>>>`, comparisons, `&&`, `||`, `!`, `??`, `?.`, `? :`, `++`/`--`, compound assignments, `is` (`x is FlxSprite`), `cast`, `untyped`.
- Strings in single quotes interpolate: `'score: $score, half: ${score / 2}'`.
- Arrays (`push`, `pop`, `shift`, `unshift`, `insert`, `remove`, `splice`, `slice`, `concat`, `join`, `indexOf`, `contains`, `sort`, `reverse`, `map`, `filter`, `copy`, `resize`, `iterator`), maps (`["a" => 1]`, `exists`, `remove`, `keys`), anonymous objects (`{x: 1, y: 2}`).
- Comprehensions: `[for (i in 0...4) i * i]` and `[while (n > 0) n--]`.
- `import`, `using` and `package` lines are accepted and do nothing: every class below is already known.
- `class`, `enum`, `abstract`, `typedef` and macros are not supported.

A name that is not known (a class God's Hand does not have) is `null` instead of an error, so a script that uses one feature the game lacks still runs the rest.

## What scripts can use

| Name | What it is |
|---|---|
| `game`, `PlayState.instance` | The song being played: `game.boyfriend`, `game.dad`, `game.gf`, `game.camGame`, `game.camHUD`, `game.camOther`, `game.iconP1`, `game.healthBar`, `game.scoreTxt`, `game.notes`, `game.unspawnNotes`, `game.strumLineNotes`, `game.playerStrums`, `game.opponentStrums`, `game.members`, `game.variables`, `game.add(obj)`, `game.insert(pos, obj)`, `game.remove(obj)`, `game.health`, `game.songScore`... (the same paths as `getProperty`) |
| `add`, `insert`, `remove`, `addBehindGF`, `addBehindBF`, `addBehindDad` | Psych's helpers to put objects in the scene |
| `setVar`, `getVar`, `removeVar` | Variables shared with every script (Lua too) |
| `getLuaObject(tag)` | A sprite or text made by Lua |
| `debugPrint`, `trace` | Text on screen / in the log |
| `FlxG` | `FlxG.width`, `FlxG.height`, `FlxG.camera`, `FlxG.cameras`, `FlxG.sound` (`FlxG.sound.play`, `FlxG.sound.music`), `FlxG.keys`, `FlxG.mouse`, `FlxG.random`, `FlxG.save`, `FlxG.fullscreen`, `FlxG.timeScale`... |
| `FlxSprite`, `FlxText`, `FlxSkewedSprite`, `FlxBackdrop`, `ModchartSprite` | Sprites and texts: `new FlxSprite(x, y)`, `loadGraphic`, `frames`, `animation.addByPrefix`, `animation.play`, `makeGraphic`, `setGraphicSize`, `updateHitbox`, `screenCenter`, `velocity`, `acceleration`... |
| `FlxGroup`, `FlxTypedGroup`, `FlxSpriteGroup`, `FlxTypedSpriteGroup`, `FlxContainer`, `FlxSpriteContainer`, `FlxTypedContainer` | Groups: `add`, `remove`, `members`, `forEach`, `length` |
| `FlxCamera` | Cameras: `zoom`, `angle`, `alpha`, `scroll`, `shake`, `flash`, `fade`, `setFilters` |
| `FlxTween`, `FlxEase`, `FlxTimer` | `FlxTween.tween(obj, {x: 100}, 1, {ease: FlxEase.sineInOut, onComplete: f})`, `FlxTween.color`, `new FlxTimer().start(1, f, loops)` |
| `FlxColor` | Colours: `FlxColor.RED`, `FlxColor.fromRGB(r, g, b)`, `FlxColor.fromString('#FF0000')`... |
| `FlxMath`, `FlxPoint`, `FlxBasePoint` | `FlxMath.lerp`, `FlxMath.bound`, `FlxMath.roundDecimal`...; points |
| `Character`, `Boyfriend` | `new Character(x, y, name, isPlayer)`: one more character (multicharacter scripts) |
| `FlxRuntimeShader`, `ShaderFilter` | Shaders: `new FlxRuntimeShader(code)`, `game.createRuntimeShader(name)`, `camera.setFilters([new ShaderFilter(shader)])`. See [Shaders](Shaders.md) |
| `MP4Handler`, `VideoHandler`, `FlxVideo`, `FlxVideoSprite`, `VideoSprite`, `MP4Sprite` | Videos, as the video libraries of Psych mods |
| `Paths` | `Paths.image`, `Paths.sound`, `Paths.music`, `Paths.font`, `Paths.getSparrowAtlas`, `Paths.getPackerAtlas`, `Paths.txt`, `Paths.json`... |
| `Conductor`, `ClientPrefs` | `Conductor.songPosition`, `Conductor.bpm`, `Conductor.crochet`...; the player's options (`ClientPrefs.data.downScroll`...) |
| `CoolUtil`, `Application`, `Lib` | Psych and OpenFL helpers scripts use |
| `Math`, `Std`, `StringTools`, `Reflect`, `Type`, `Sys`, `Date` | The Haxe standard library: `Std.int`, `Std.parseFloat`, `Std.string`, `Std.random`, `Std.isOfType`, `StringTools.replace`, `Reflect.field`, `Reflect.setProperty`, `Type.getClassName`, `Sys.time`, `Date.now()`... |
| `Map`, `StringMap`, `IntMap`, `ObjectMap`, `Array` | Collections |
| `Function_Continue`, `Function_Stop`, `Function_StopLua`, `Function_StopHScript`, `Function_StopAll` | What callbacks return |
| `openfl.*`, `flixel.*`, `lime.*`, `haxe.*`, `states.*`, `backend.*`, `objects.*`, `psychlua.*`, `funkin.*` | Full class paths (`flixel.tweens.FlxTween`) reach the same classes |

Callbacks get their usual arguments, except the note callbacks, which get a note object (`note.noteData`, `note.mustPress`...; see [Callbacks](Callbacks.md)).
