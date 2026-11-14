# Shaders

God's Hand runs the fragment shaders of Psych Engine and Flixel mods, on sprites and on whole cameras. Options → GRAPHICS → **Shaders** turns them all off (faster on weak devices). See also: [Lua API → Shaders](Lua-API.md#shaders), [HScript](HScript.md).

## Writing one

A shader is `shaders/<name>.frag` in the mod, written as for Psych Engine: OpenFL's GLSL ES 1.0 style with `#pragma header`, `bitmap`, `openfl_TextureCoordv`, `openfl_TextureSize`, `flixel_texture2D` and `gl_FragColor`. The game turns it into GLSL 330 on computers and GLSL ES on Android, so the same file works everywhere.

```glsl
#pragma header
uniform float iTime;
uniform float amount;

void main() {
    vec2 uv = openfl_TextureCoordv;
    uv.x += sin(uv.y * 20.0 + iTime * 4.0) * amount;
    gl_FragColor = flixel_texture2D(bitmap, uv);
}
```

- Only the fragment shader is used: sprites are drawn with the game's own vertex shader (a `.vert` file is ignored).
- Uniforms keep their values until a script changes them. Nothing sets `iTime` by itself: scripts set it, as in Psych (`setShaderFloat('tag', 'iTime', os.clock())`; `os.clock()` counts real seconds on every system).
- Colours come out premultiplied by alpha, as in OpenFL.

## On a sprite

```lua
function onCreatePost()
    initLuaShader('wave')
    makeLuaSprite('bg', 'stageback', -600, -200)
    addLuaSprite('bg', false)
    setSpriteShader('bg', 'wave')
    setShaderFloat('bg', 'amount', 0.02)
end

function onUpdate(elapsed)
    setShaderFloat('bg', 'iTime', os.clock())
end
```

`setSpriteShader` works on Lua sprites and texts, stage objects and characters (`'boyfriend'`, `'dad'`, `'gf'`). `removeSpriteShader(tag)` takes it off. For the HUD, put a shader on the `'hud'` camera instead.

## Layers: several shaders on one sprite

God's Hand can stack shaders on a sprite: each **layer** works on what the layer before it drew, as nested filters do.

| Function | What it does |
|---|---|
| `addSpriteShader(tag, shader, position)` | Adds a layer, last or at position (1 is the first). Returns its position. |
| `removeSpriteShader(tag, layer)` | Removes one layer: its position, or every layer of that shader name. Without layer: all of them (as Psych). |
| `getSpriteShaders(tag)` | The layers' shader names, first first. |
| `moveSpriteShader(tag, from, to)` | Moves a layer. |
| `setSpriteShaderOrder(tag, {'a', 'b'})` | Those layers first, in that order; the rest after. |

The uniform functions (`setShaderFloat`, `setShaderInt`, `setShaderBool`, the array versions and `setShaderSampler2D`) take an optional last argument, the **layer**: its position or its shader name. Without it, they set every layer that has the uniform. The getters read the first layer, or the layer named.

```lua
initLuaShader('glow'); initLuaShader('wave'); initLuaShader('chroma')
addSpriteShader('boyfriend', 'wave')       -- 1
addSpriteShader('boyfriend', 'glow')       -- 2: glows the waved picture
addSpriteShader('boyfriend', 'chroma', 1)  -- now chroma, wave, glow
setShaderFloat('boyfriend', 'amount', 0.5, 'glow')   -- only the glow layer
setSpriteShaderOrder('boyfriend', {'wave', 'glow'})   -- wave, glow, chroma
```

Each layer costs one more drawing pass over the sprite's own size (not the whole screen).

## On a camera

- **Lua (JS Engine)**: `setCameraShader(camera, shader, index)` puts a mod shader on `'game'`, `'hud'` or `'other'`; set its uniforms with `setShaderFloat(index, ...)` (index defaults to the shader's name). `removeCameraShader(camera, index)` and `clearShadersFromCamera(camera)` take them off.
- **HScript (Psych)**: `game.camGame.setFilters([new ShaderFilter(game.createRuntimeShader('wave'))])`.

## JS Engine's screen effects

Ready-made camera effects with JS Engine's own GLSL, from Lua: `addChromaticAberrationEffect`, `addScanlineEffect`, `addGrainEffect`, `addTiltshiftEffect`, `addVCREffect`, `addGlitchEffect`, `addPulseEffect`, `addDistortionEffect`, `addInvertEffect`, `addGrayscaleEffect`, `addBloomEffect`, `addBlockedGlitchEffect`, `addWiggleEffect`, `add3DEffect`, `addGlitchShader`; `clearEffects(camera)` removes them. Their arguments are in the [Lua API](Lua-API.md#screen-effects-js-engine).
