# DS2-FPS-Unlock-Vanilla-Scholar

High-FPS unlock with gameplay physics fixes for both editions of Dark Souls II: the original game (Vanilla, 32-bit DirectX 9) and Scholar of the First Sin (64-bit DirectX 11). Jumps, attack inputs, animation events, cloth and equipment durability are kept in step with how the game plays at 60 FPS. One source tree builds both editions.

The mod removes the game's own limiter. It does not install a new one. Cap the framerate with RTSS, the NVIDIA control panel, or another limiter, and set `PhysicsFPS` to that same number (default 120).

| Game | Download | DLL to install |
| --- | --- | --- |
| Scholar of the First Sin (64-bit, DirectX 11) | release `v1.1.0-SCHOLAR-DX11` | `xinput1_3.dll` from that archive |
| Original Dark Souls II, Vanilla (32-bit, DirectX 9) | release `v1.1.0-VANILLA-DX9` | `xinput1_3.dll` from that archive |

The game imports `XINPUT1_3.dll` by ordinal. The file has to keep that name, next to `DarkSoulsII.exe`, along with `DS2-FPS-Unlock.ini`. Controller calls are forwarded to the system DLL. The executable on disk is not modified.

The two games cannot share a DLL.

## DirectX 9

The original game's unlocked picture is fullscreen only. In windowed mode the counter can run ahead of the image, which stays at 60.

## DirectX 11

The Scholar build was tested with the Dark Souls II Seamless Co-op mod, and both were working together.

## Configuration

`FPSUnlock = false` leaves the game completely unchanged.

`FPSUnlock = true` turns the whole fix on. The individual fixes have their own switches, all `true` by default: `GroundSnapFix`, `ForwardAttackFix` and `TaeEventFix` (described below), `ClothFix` (cloth stepped with the real frame time) and `DurabilityFix` (durability loss scaled to the frame time). `GroundSnapFix = false` falls back to the old jump workaround that never snaps. The original mod's names `JumpHeightFix` and `ClothSpeedFix` are also accepted.

`PhysicsFPS` is the rate given to Havok as the expected maximum, 120 by default. Set it to your external cap. A 60 FPS frame is unchanged. Faster frames shorten the physics step and scale durability loss by `frameTime * 60` (clamped to 0.05–2), including hits from enemies, bosses, and other players, for weapons, armor, and rings.

`JumpTrace` (default `false`) is a diagnostic for jump bug reports. It writes the player's per-frame jump physics to `DS2-FPS-Unlock-jumptrace-*.csv` next to the game, one file per launch. With `FPSUnlock = false` it only observes the stock game, for comparison.

## What changes in game

Both editions sample the real frame time and then normally step the world at a fixed 1/60. This mod turns off that wait and passes the measured step into gameplay and cloth. Health, stamina, and movement that already multiply by delta stay consistent.

Three per-frame rules are made to behave as they do at 60 FPS, the rate the PC game was tuned for:

- **Ground snap and jumps** (`GroundSnapFix`). While the character is grounded, the game pulls it down onto the floor after every physics step (a 0.1 unit sweep). That keeps the body inside Havok's contact band, and while the body is moving up the game takes Havok's contact flag as "grounded", so the pull repeats. At 60 FPS a jump's takeoff carries the body out of the band in a single frame and the loop breaks; at a high frame rate each frame moves it a fraction of that, so on steep downhills the character never left the ground. The game now treats a frame as released exactly when, at 60 FPS, it would have been: the body is commanded upward at 2.4 units/s or more (the stock takeoff measures 3.45 to 4, the wind-up that stays held 1.4). On those frames the pull is skipped and the grounded and contact flags are cleared as the game would have seen them. Every other frame keeps the stock pull, so runs, landings (including the roll after a jump or a long drop) and the short, stalled jumps the stock game also gives on the steepest slopes are unchanged.
- **Guard break and jump attack** (`ForwardAttackFix`). Forward + R1 and forward + R2 need the stick pushed forward from neutral within a short time. The game looks for that in a history of the last 16 frames: 0.27 s at 60 FPS, but 0.13 s at 120 and 0.07 s at 240, so a normal push fell out of the history. Frames are now folded together so the history spans what it does at 60 FPS.
- **Animation events** (`TaeEventFix`). The game dispatches the animation (TAE) events of the current 1/30 s TAE frame on every update. A track that has two events running at once marks both as starting on every dispatch, so their start effects (damage, effects, spawned objects) happened once per TAE frame at 30 FPS, twice at 60, and about five times at 144. Those repeats are now limited to what 60 FPS gives. The events themselves still run every frame.

Anything else that runs once per call, instead of once per second of animation, can still speed up. Cloth speed is corrected; the cloth simulation itself is still not identical to 60 FPS. Online play uses the measured step as well. That is not a promise that it matches a peer running at 60.

## Build

Visual Studio 2022:

```
build.bat
```

`build\xinput1_3.dll` is Scholar. `build\vanilla\xinput1_3.dll` is the original game.

## License

MIT. See [LICENSE](LICENSE).
