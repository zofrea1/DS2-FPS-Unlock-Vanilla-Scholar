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

`FPSUnlock = true` turns the whole fix on. Cloth, jump height, and durability are part of that and are not separate switches.

`PhysicsFPS` is the rate given to Havok as the expected maximum. Set it to your external cap. A 60 FPS frame is unchanged. Faster frames shorten the physics step and scale durability loss by `frameTime * 60` (clamped to 0.05–2), including hits from enemies, bosses, and other players, for weapons, armor, and rings.

## What changes in game

Both editions sample the real frame time and then normally step the world at a fixed 1/60. This mod turns off that wait and passes the measured step into gameplay and cloth. The ground check that shortens jumps once frames get smaller is skipped. Health, stamina, and movement that already multiply by delta stay consistent.

Anything that runs once per call, instead of once per second of animation, can still speed up. Backstabs and similar animation events can repeat. Downhill rolls can still end early. Guard-break and jump-attack windows are still counted in frames. Cloth speed is corrected; the cloth simulation itself is still not identical to 60 FPS. Online play uses the measured step as well. That is not a promise that it matches a peer running at 60.

## Build

Visual Studio 2022:

```
build.bat
```

`build\xinput1_3.dll` is Scholar. `build\vanilla\xinput1_3.dll` is the original game.

## License

MIT. See [LICENSE](LICENSE).
