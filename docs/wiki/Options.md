# Options

Every option of the Options menu, section by section (the text the game shows for each one).


## Gameplay

| Option | What it does | Values |
|---|---|---|
| Downscroll | Notes fall from the top towards receptors at the bottom. | on / off |
| Middlescroll | Centre your lanes and dim the opponent's. | on / off |
| Ghost tapping | When ON, pressing with no note to hit is not punished. | on / off |
| Auto pause | The song pauses when the game's window loses focus (Psych's Auto Pause). | on / off |
| Disable reset button | OFF: R during a song makes you lose at once, as in Psych. | on / off |
| Rating offset | How late or early you hit for a Sick!. Higher: you have to hit later (Psych's Rating Offset). | -30 to 30 |
| Sick! hit window | The time you have for the best rating (SICK!), either side of the note. | 15 to 45 |
| Good hit window | The time you have for a GOOD. | 15 to 90 |
| Bad hit window | The time you have for a BAD. | 15 to 135 |
| Safe frames | How many frames (of 1/60 s) early or late a note can still be hit (Psych's Safe Frames). | 2 to 10 |
| Scroll speed | 0 = use each chart's own speed. | 0 to 6 |
| Audio offset | Positive if you hear the music late. Hold SHIFT for steps of 10 ms. | -300 to 300 |
| Calibrate offset... | Tap along to a metronome to measure your offset. | - |
| Audio latency | The sound card's buffer: smaller is heard sooner (hit sounds, the song). Raise it if the sound crackles. Applied the next time the game starts. | Lowest (6 ms), Low (15 ms), Normal (30 ms), Safe (60 ms) |
| Show timing (ms) | Shows how early or late each hit was. | on / off |
| Opponent notes | OFF hides the opponent's receptors and notes, as Psych's Opponent Notes. | on / off |
| Opponent strums | Light up the opponent's receptors when they sing. | on / off |
| Note splashes | Bursts of light on SICK! hits. | on / off |
| Touch controls | Tap the lanes on a touchscreen (a mouse click counts as a tap). | on / off |
| Extra keys | Charts made for 1 to 18 keys keep them, as in Psych Extra Keys. OFF: every chart has 4 keys, like Psych and JS Engine. | on / off |

## Controls  (4 keys)

| Option | What it does | Values |
|---|---|---|
| Left lane | ENTER to rebind, LEFT/RIGHT to pick a slot, BACKSPACE to clear. | two keys |
| Down lane | ENTER to rebind, LEFT/RIGHT to pick a slot, BACKSPACE to clear. | two keys |
| Up lane | ENTER to rebind, LEFT/RIGHT to pick a slot, BACKSPACE to clear. | two keys |
| Right lane | ENTER to rebind, LEFT/RIGHT to pick a slot, BACKSPACE to clear. | two keys |
| Reset controls | Restore D F J K and the arrow keys. | - |
| Key bindings (1 to 18 keys)... | The key of every lane for charts of 1 to 18 keys, as their arrows show in a song. Works with a Bluetooth keyboard on Android too. | - |

## Graphics

| Option | What it does | Values |
|---|---|---|
| Fullscreen | Borderless fullscreen (F11 works anywhere). | on / off |
| VSync | Waits for the screen's refresh. OFF: the frame limit below decides (most precise input). | on / off |
| Low latency | With VSync: no frames waiting in the driver, and each one drawn just before the screen shows it while the keys are read every millisecond. What you press shows a refresh or more sooner. | on / off |
| Frame limit | Higher limits make notes smoother. Input is polled at ~1000 Hz regardless. | 6unlimited, 12unlimited, 144 fps, 165 fps, 24unlimited, 36unlimited, 50unlimited, unlimited |
| Show FPS | The frame rate at the top left. | on / off |
| FPS: memory | Under the frame rate: the memory the game uses, and the most it has used. | on / off |
| FPS: frame times | How long updating and drawing take, the slowest frame of the last second, and a graph. | on / off |
| FPS: CPU, GPU and RAM | How much of the processor the game uses, how busy the graphics card is and its memory, and the system's memory in use. | on / off |
| FPS: hardware | The processor, graphics card, OpenGL version and system the game runs on. | on / off |
| Low detail | Fewer particles and effects for slower machines. | on / off |
| Flashing lights | Screen flashes on some beats. | on / off |
| Camera zoom on beat | Only the beat bops: zoom events from charts and scripts always work. | on / off |
| Shaders | Mod shaders and screen effects. OFF is faster on weak devices. | on / off |
| Anti-aliasing | Smooth scaling of mod pictures. OFF: sharp pixels and a little faster (Psych's Anti-Aliasing). | on / off |
| Note detail | Spam charts: Adaptive draws notes stacked a few pixels apart once while the frame rate is under 60 (every note is still played). Full: every pixel. | Adaptive, Full |
| Note renderer | What draws the notes. Auto: the graphics card, or the processor's assembly when OpenGL itself runs on the processor (llvmpipe, SwiftShader...), where that is faster. GPU: the graphics card. CPU (ASM): the processor, with assembly (SSE2 on x86-64, NEON on ARM64 phones and Macs; other processors use C++). CPU (C++): the processor, in C++. Turned notes and mines stay on the GPU. F3 in a song shows the time it takes. | 3, 0, 1, 2 |

## Interface

| Option | What it does | Values |
|---|---|---|
| Note counter | Notes hit so far, under the score: your side only, or both sides added up. | Off, Your side, Both sides |
| Psych Engine interface | Psych's notes, ratings, health and time bars, fonts, pause menu and sounds, from your own copy. | on / off |
| Hit popups | Rating pictures and combo numbers that pop up on each hit. | on / off |
| Time bar | The bar at the top of the screen, as in Psych. | Time left, Time elapsed, Song name, Off |
| Health bar opacity | The health bar and the icons. | 0 to 1 |
| Hide HUD | Hides the health bar, the icons, the score line and the ratings, as Psych's Hide HUD. | on / off |
| Watermark | "God's Hand Engine v.(version) (system) Slop Edition" in the bottom-left corner of songs, as JS Engine shows its own. | on / off |
| Intro video | The engine's intro before the title screen. ENTER skips it (the SKIP button on a touchscreen). | on / off |
| Audio visualizer | Bars in the top-right corner of songs that move with the music. Their colour follows the chart: whose turn it is, how thick the notes are, your hits and misses, and your health. | on / off |
| Score text zoom on hit | The score line grows a little on each note you hit, as in Psych. | on / off |
| Botplay text | BOTPLAY (or REPLAY) on screen while the game plays by itself. | on / off |
| Combo stacking | Ratings and combo numbers pile up. OFF: only the latest one, easier to read. | on / off |
| Note splash opacity | How see-through the bursts on your best hits are. | 0 to 1 |
| Note skin... | The Psych interface's note skin, with its notes on show: Default and the noteSkins/ pictures of your Psych copy and of every installed mod. | - |
| Note colors... | The Psych interface's note colours, as in Psych's Note Colors menu. | - |
| Rating position X | Where the ratings pop up (Psych's Adjust Combo). | -600 to 600 |
| Rating position Y | - | -400 to 400 |
| Combo position X | Where the combo digits pop up. | -600 to 600 |
| Combo position Y | - | -400 to 400 |
| Pause screen song | The pause menu's music with the Psych Engine interface. | None, Breakfast, Tea Time |

## Render Mode Videos

| Option | What it does | Values |
|---|---|---|
| Video size | For RENDER MODE on the main menu: the song plays by itself and is recorded to a video in renders/, frame by frame, smooth even if the device cannot keep up. On a computer it needs ffmpeg (next to the game or installed); on Android the phone's own encoder makes it and it goes to Movies/GodsHand. | 1280x720, 1920x1080, 2560x1440, 3840x2160 |
| Video frame rate | - | 30, 60, 120, 240 |
| Video encoder | Auto: the graphics card's own encoder when this computer has one that works (much faster), else x264. x264 and x265 use the processor; the NVIDIA, AMD and Intel ones the graphics card. | Auto, H.264 (x264), H.265 (x265), VP9 (WebM), H.264 NVIDIA, H.265 NVIDIA, H.264 AMD, H.264 Intel |
| Video bitrate | Higher is sharper and bigger. Spam charts move a lot: 20 Mbps or more. | 5 Mbps, 10 Mbps, 20 Mbps, 40 Mbps, 80 Mbps |
| Video audio | The song (instrumental and voices) in the video. | on / off |

## Audio

| Option | What it does | Values |
|---|---|---|
| Master volume | - | 0 to 1 |
| Music volume | - | 0 to 1 |
| Sound effects | - | 0 to 1 |
| Hit sounds | A tick on every note you hit, on the note's own sample of the song: a fast spam becomes a tone at the rate of its notes instead of a louder, crackling pile. | 0 to 1 |

## Debug

| Option | What it does | Values |
|---|---|---|
| Lua errors | Errors of mod scripts on screen. Psych shows them only for scripts that set luaDebugMode. | Always, luaDebugMode, Off |
| Lua warnings | Missing images, sounds and objects that mods ask for. Psych: only with luaDebugMode. | Always, luaDebugMode, Off |
| Lua note callbacks | Scripts hear every hit and miss (goodNoteHit, noteMiss...). OFF is faster on spam charts: plain notes are not passed on, but notes with a note type always are. | on / off |

Settings live in the user folder (on Windows `%APPDATA%\GodsHand`), so a new version extracted elsewhere keeps them.
