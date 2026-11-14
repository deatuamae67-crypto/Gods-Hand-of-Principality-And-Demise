# Callbacks

Functions a script defines and the game calls, with the same names and arguments as Psych Engine 0.7.3. A Lua script defines them as global functions; an HScript (`.hx`) file as top-level functions. See also: [Lua API](Lua-API.md), [HScript](HScript.md).

```lua
function onCreate()
    makeLuaSprite('sky', 'sky', -600, -300)
    addLuaSprite('sky', false)
end

function goodNoteHit(id, direction, noteType, isSustainNote)
    if noteType == 'Hurt Note' then addHealth(-0.1) end
end
```

## What a callback returns

Most callbacks return nothing. Those marked **stoppable** below can return `Function_Stop` to keep the game from doing what comes next. Any callback can return `Function_StopLua`: the scripts after this one are not called for this event. `Function_Continue` (or nothing) lets everything go on.

## Song

| Callback | When |
|---|---|
| `onCreate()` | The script has loaded. Stage, characters and HUD may not exist yet. |
| `onCreatePost()` | Everything is made: stage, characters, HUD, notes. |
| `onUpdate(elapsed)` | Every frame, before the game updates; elapsed is in seconds. |
| `onUpdatePost(elapsed)` | Every frame, after the game updates. |
| `onStartCountdown()` | **Stoppable**: the countdown is about to start. Return `Function_Stop` to hold it (a cutscene, a dialogue...) and call `startCountdown()` later. |
| `onCountdownStarted()` | The countdown has started. |
| `onCountdownTick(counter)` | Each beat of the countdown: 0 (three), 1 (two), 2 (one), 3 (go). |
| `onSongStart()` | The music starts. |
| `onStepHit()` | A new step (a quarter of a beat); `curStep` is up to date. God's Hand also passes the step as an argument. |
| `onBeatHit()` | A new beat; `curBeat` is up to date. God's Hand also passes the beat. |
| `onSectionHit()` | A new section; `curSection`, `mustHitSection`, `gfSection` and `altAnim` are up to date. |
| `onMoveCamera(focus)` | A section moves the camera to `'boyfriend'`, `'dad'` or `'gf'`. |
| `onEndSong()` | **Stoppable**: the song is over. Return `Function_Stop` to stay (an ending cutscene) and call `endSong()` later. |
| `onDestroy()` | The song is being left (ended, exited, restarted). |

## Notes

| Callback | When |
|---|---|
| `goodNoteHit(id, direction, noteType, isSustainNote)` | You hit a note. direction is 0 to 3 (left, down, up, right; more on charts with more keys); noteType is `''` for normal notes; isSustainNote is true for each piece of a hold. |
| `opponentNoteHit(id, direction, noteType, isSustainNote)` | The opponent hits a note. |
| `noteMiss(id, direction, noteType, isSustainNote)` | You miss a note; isSustainNote is true when a hold is let go too early. |
| `noteMissPress(direction)` | You press a key with no note to hit (with Ghost tapping off). |
| `onKeyPress(key)` | A note key goes down (0 to 3). |
| `onKeyRelease(key)` | A note key goes up. |

The id is the note's place in the chart. HScript gets a note object instead of the four values: `note.ID`, `note.noteData`, `note.noteType`, `note.isSustainNote`, `note.mustPress`, `note.strumTime`, `note.hitByOpponent`.

Options → DEBUG → **Lua note callbacks** OFF stops these four note callbacks (spam charts of millions of notes run faster; mods that count notes or drain health with them stop doing it).

## Score

| Callback | When |
|---|---|
| `preUpdateScore(miss)` | **Stoppable**: the score text is about to change (miss is true after a miss). Return `Function_Stop` to keep the text as it is (a script writes its own). |
| `onUpdateScore(miss)` | The score text has changed. |

## Events

| Callback | When |
|---|---|
| `onEventPushed(name, value1, value2, strumTime)` | For each event of the chart, when the song loads (to preload what it needs). |
| `onEvent(name, value1, value2, strumTime)` | An event of the chart happens (or `triggerEvent` runs one). Custom events are scripts in `custom_events/<name>.lua` that define this. |

## Pause, game over

| Callback | When |
|---|---|
| `onPause()` | **Stoppable**: the game is about to pause. |
| `onResume()` | The game goes on after a pause. |
| `onGameOver()` | **Stoppable**: health has run out. Return `Function_Stop` to stay alive. |
| `onGameOverStart()` | The game over screen starts. |

## Timers, tweens, sounds, videos

| Callback | When |
|---|---|
| `onTimerCompleted(tag, loops, loopsLeft)` | A `runTimer` timer fires (every loop). |
| `onTweenCompleted(tag)` | A tween (`doTweenX`, `startTween`, `noteTweenAlpha`...) has finished. |
| `onSoundFinished(tag)` | A sound played with a tag has finished. |
| `onVideoFinished(tag)` | A video has finished: its tag, or `'cutscene'` for `startVideo`. |

## Custom substates

Opened with `openCustomSubstate(name, pause)`:

| Callback | When |
|---|---|
| `onCustomSubstateCreate(name)` | The substate opens. |
| `onCustomSubstateCreatePost(name)` | After it opened. |
| `onCustomSubstateUpdate(name, elapsed)` | Every frame while it is open. |
| `onCustomSubstateUpdatePost(name, elapsed)` | After that frame. |
| `onCustomSubstateDestroy(name)` | It closes. |

## Where scripts run

| Folder | Runs |
|---|---|
| `scripts/` | Every song of the mod |
| `data/<song>/` | That song |
| `stages/<stage>.lua` | Songs on that stage |
| `characters/<name>.lua` | Songs with that character |
| `custom_events/<event>.lua` | Songs whose chart has that event |
| `custom_notetypes/<type>.lua` | Songs whose chart has that note type |

Each folder takes `.lua`, `.hx`, `.hscript` and `.hxs` files.
