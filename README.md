# DS2-FPS-Unlock

Unlocks the framerate in Dark Souls II and keeps gameplay, cloth, jumps, and equipment durability in step with the real frame time. One source tree builds both editions.

The mod removes the game's own limiter. It does not install a new one. Cap the framerate with RTSS, the NVIDIA control panel, or another limiter, and set `PhysicsFPS` to that same number.

| Game | Download | DLL to install |
| --- | --- | --- |
| Scholar of the First Sin (64-bit, DirectX 11) | release `v1.0.0-DX11` | `xinput1_3.dll` from that archive |
| Original Dark Souls II (32-bit, DirectX 9) | release `v1.0.0-DX9` | `xinput1_3.dll` from that archive |

The game imports `XINPUT1_3.dll` by ordinal. The file has to keep that name, next to `DarkSoulsII.exe`, along with `DS2-FPS-Unlock.ini`. Controller calls are forwarded to the system DLL. The executable on disk is not modified.

The two games cannot share a DLL.

## DirectX 9

The original game's unlocked picture is fullscreen only. In windowed mode the counter can run ahead of the image, which stays at 60.

## DirectX 11

The Scholar build was tested with the Dark Souls II Seamless Co-op mod, and both were working together.

## Configuration

`FPSUnlock = false` leaves the game completely unchanged.

`FPSUnlock = true` turns the whole fix on. The individual fixes have their own switches, all `true` by default: `GroundSnapFix`, `ForwardAttackFix` and `TaeEventFix` (described below), `ClothFix` (cloth stepped with the real frame time) and `DurabilityFix` (durability loss scaled to the frame time). `GroundSnapFix = false` falls back to the old jump workaround that never snaps. The original mod's names `JumpHeightFix` and `ClothSpeedFix` are also accepted.

`PhysicsFPS` is the rate given to Havok as the expected maximum. Set it to your external cap. A 60 FPS frame is unchanged. Faster frames shorten the physics step and scale durability loss by `frameTime * 60` (clamped to 0.05–2), including hits from enemies, bosses, and other players, for weapons, armor, and rings.

## What changes in game

Both editions sample the real frame time and then normally step the world at a fixed 1/60. This mod turns off that wait and passes the measured step into gameplay and cloth. Health, stamina, and movement that already multiply by delta stay consistent.

Three per-frame rules are made to behave as they do at 60 FPS, the rate the PC game was tuned for:

- **Ground snap and jumps** (`GroundSnapFix`). After each physics step the game sweeps 0.1 units down and pulls the character onto any floor it finds. At a high frame rate a jump rises only a few hundredths of a unit per frame, so the snap caught the takeoff and the character stayed on the ground. The snap is now skipped while the character's jump is in progress (the game's own jump counter, which also controls the jump's velocity) and while the body is rising, and runs unchanged the rest of the time, so downhill runs, landings and the roll after a drop keep it.
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
