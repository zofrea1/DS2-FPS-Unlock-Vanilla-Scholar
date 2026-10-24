# DS2-FPS-Unlock-Vanilla-Scholar

High-FPS unlock with gameplay physics fixes for both editions of Dark Souls II: the original game (Vanilla, 32-bit DirectX 9) and Scholar of the First Sin (64-bit DirectX 11). Jumps, attack inputs, animation events, cloth and equipment durability are kept in step with how the game plays at 60 FPS. Fullscreen also fills 16:10 screens such as the Steam Deck's, without black bars. One source tree builds both editions.

The mod removes the game's own limiter. `FPSLimit` (default 0) is a cap in frames per second; 0 leaves the framerate uncapped. RTSS or the NVIDIA control panel can cap instead. On a Steam Deck, enable the performance overlay's "Disable Frame Limit" and set `FPSLimit` here: the Deck's own limiter waits in the compositor and adds input lag. Set `PhysicsFPS` to the cap you actually run at (default 120). `VSync` (default false) presents fullscreen immediately; set it true to wait for one vblank at `FullscreenRefreshRate`.

| Game | Download | DLL to install |
| --- | --- | --- |
| Scholar of the First Sin (64-bit, DirectX 11) | release `v1.2.0-SCHOLAR-DX11` | `xinput1_3.dll` from that archive |
| Original Dark Souls II, Vanilla (32-bit, DirectX 9) | release `v1.2.0-VANILLA-DX9` | `xinput1_3.dll` from that archive |

Put the DLL next to `DarkSoulsII.exe`, along with `DS2-FPS-Unlock.ini`. The executable on disk is not modified. The two games cannot share a DLL.

## DLL names

The DLL works under any of the names below, all of which the game loads at start-up. Rename it when `xinput1_3.dll` is already taken by another mod, or to leave the game's controller library alone:

| Name | Scholar of the First Sin | Original Dark Souls II | What the DLL does besides the mod |
| --- | --- | --- | --- |
| `xinput1_3.dll` (as shipped) | yes | yes | Passes controller calls on to the system's XInput (`xinput1_4`, the Windows 8 and later library; under Proton, Wine's controller backend) |
| `dxgi.dll` | yes | no | Passes DXGI calls on to the real `dxgi.dll` |
| `d3d9.dll` | no | yes | Passes Direct3D 9 calls on to the real `d3d9.dll` |
| `dinput8.dll` | yes (Windows only) | yes (Windows only) | Passes DirectInput calls on to the real `dinput8.dll` |

Under any name other than `xinput1_3.dll` the game reads its controller through its own `xinput1_3.dll`, untouched. The log (`DS2-FPS-Unlock.log`) states the name the mod was loaded as and, for `xinput1_3.dll`, the XInput library it uses and each controller slot as it connects.

If another mod needs the same name (for example ReShade as `dxgi.dll`), rename that mod's DLL (for example to `reshade-dxgi.dll`) and set `ChainDll = reshade-dxgi.dll` in `[Loader]`: this mod then passes the calls on to that DLL instead of to the system's, and both run.

**Linux and the Steam Deck (Proton).** Wine uses its own copy of a system DLL unless the launch options name the DLL, so set them to the name you installed: `WINEDLLOVERRIDES="xinput1_3=n,b" %command%`, `WINEDLLOVERRIDES="dxgi=n,b" %command%` or `WINEDLLOVERRIDES="d3d9=n,b" %command%`. Several DLLs (this mod plus another mod's) go in one list: `WINEDLLOVERRIDES="xinput1_3=n,b;dxgi=n,b" %command%`. `dxgi.dll` and `d3d9.dll` rely on DXVK, which Proton uses by default. `dinput8.dll` does not work under Proton: Wine's own `dinput8` cannot be reached from a DLL with that name.

## DirectX 9

The original game's unlocked picture is fullscreen only. In windowed mode the counter can run ahead of the image, which stays at 60.

## DirectX 11

The Scholar build was tested with the Dark Souls II Seamless Co-op mod, and both were working together.

## 16:10 screens and the Steam Deck

In exclusive fullscreen both editions draw the game in the largest 16:9 rectangle of the screen and copy it into the middle of the display, so a 16:10 screen (the Steam Deck's 1280x800, 1920x1200, 2560x1600) gets black bars at the top and bottom. In a window the same game renders at the window's full size: the cameras take their aspect from it and the HUD keeps its 1280x720 layout, scaled to fit and anchored to the screen edges. That is why widescreen tools only worked in windowed mode.

`FixAspect = true` (the default) makes fullscreen behave like the window: the game area is the whole backbuffer at any aspect ratio. A 16:9 screen is unchanged. Wider screens (21:9) also fill, keeping the vertical view and showing more at the sides, with the HUD at the edges; that has not been checked in game. `FixAspect` works whether or not `FPSUnlock` is on.

On a screen taller than 16:9 the game keeps the horizontal view it has at 16:9 and opens the vertical one ("Vert+"), so the extra height shows more of the scene above and below instead of less at the sides. At 16:10 the default 55-degree vertical view becomes about 60 degrees. The log states the change the first time it applies. Most of the HUD and the FMV videos are not Vert+. They stay the shape the game draws them, on both editions. That is the intended result for now.

- **Scholar of the First Sin** builds every perspective in one function. Screen cameras (those whose aspect is the game area's) get the wider vertical field of view there, so rendering, culling, shadows and on-screen markers all use the same view.
- **Original Dark Souls II** has that code inlined where it is used. The screen cameras' copies are in four functions: the camera's projection, two camera-operator projections, and a target pick that finds the candidate nearest the screen centre. Each is hooked at its entry and reads the wider field of view for the length of the call. The other inlined copies build light frusta and are left alone.

Fullscreen lists every resolution the display offers. Pick the display's own resolution (1280x800 on the Deck) in the game's options; a 16:9 resolution on a 16:10 display is still letterboxed by the display or by the Deck's scaler. Windowed mode only offers 16:9 sizes, so on the Deck only fullscreen fills the screen. The log names the display's resolution at start-up, and the original game's log names the fullscreen game area it uses.

**Fullscreen frame rate.** Both games ask for a 60 Hz display mode in exclusive fullscreen and wait for vsync. On Windows that runs the screen at 60 Hz; on the Steam Deck, DXVK (which Proton uses for both games) caps a fullscreen game with vsync to the refresh rate of the mode it asked for, so Scholar stayed at 60 FPS in fullscreen even with the Deck at 90 Hz, while a window was not capped. With `FPSUnlock = true`, fullscreen now asks for your desktop's refresh rate (or `FullscreenRefreshRate`). `VSync = false` presents without waiting for vsync, in both games. `VSync = true` waits for one vblank at that rate, so the picture is tear-free and the framerate follows the screen. Cap the frame rate with `FPSLimit` or RTSS.

On the Steam Deck, turn on "Disable Frame Limit" in the performance overlay and set `FPSLimit` to the rate you want. The Deck's own limiter then stays off. Scholar applies the cap before the picture is shown when it cannot find the early wait in that build, and the log says so. That is what keeps fullscreen from running uncapped once the Deck limiter is off. `present` does this whenever it is selected. `early` does it from the start of the frame when that wait is found. The original DirectX 9 game already applies either point with the Deck limiter disabled.

Steam Deck (Proton): set the game's launch options to `WINEDLLOVERRIDES="xinput1_3=n,b" %command%` so Wine loads this DLL instead of its own `xinput1_3` (or the option for the name you installed, see [DLL names](#dll-names)). Controller input is forwarded to Wine's `xinput1_4`; forwarding to `xinput1_3` would reach this DLL again. Version 1.1.0 forwarded to the system `xinput1_3`, which under Proton leaves the controller dead while the keyboard still works.

## Configuration

`FPSUnlock = false` leaves the framerate and gameplay unchanged. `FixAspect` (above) is separate.

`FPSUnlock = true` turns the whole fix on. The individual fixes have their own switches, all `true` by default: `GroundSnapFix`, `ForwardAttackFix` and `TaeEventFix` (described below), `ClothFix` (cloth stepped with the real frame time) and `DurabilityFix` (durability loss scaled to the frame time). `GroundSnapFix = false` falls back to the old jump workaround that never snaps. The original mod's names `JumpHeightFix` and `ClothSpeedFix` are also accepted.

`FPSLimit` (`[Framerate]`, default `0`) caps the unlocked framerate in windowed mode and in fullscreen. `0` does not cap. Any other number is frames per second. The frame clock includes the wait, and the gameplay fixes step at the capped rate. A frame that already ran longer than the cap is not delayed, and a hitch does not make the following frames rush to catch up.

`FPSLimitPoint` (`[Framerate]`, default `present`) chooses where that wait happens. `present` waits just before the picture is shown, after the controller has been read. While the game is reaching the cap, that puts each picture on the cap's clock, which is the steadiest frame pacing. `early` waits at the start of the frame, where the game's own 60 FPS sleep used to, so the controller is read after the idle time. Use `early` for the lowest input delay while the game is reaching the cap, or on a variable refresh rate display, which can show the small gaps between frames without the judder a fixed-refresh screen would. Only the named one waits. Restart the game after changing it. If Scholar cannot find the early wait, it waits before the picture instead and the log says so, so the cap still holds.

`VSync` (`[Display]`, default `false`) is fullscreen only. `false` presents immediately, which is how the unlock gets past the screen's refresh. `true` waits for one vblank at `FullscreenRefreshRate` (the desktop rate, or the rate you set) instead of the 60 Hz mode the games ask for. The framerate then follows the screen, unless `FPSLimit` is lower. Windowed presentation is left as the game set it. The original game's picture is still fullscreen only; its window stays on the blit path. Both keys apply when `FPSUnlock` is on.

`FullscreenRefreshRate` (`[Display]`, default `0`) is the refresh rate exclusive fullscreen asks for with `FPSUnlock = true`: `0` is your desktop's own rate, a number asks for that rate. `ChainDll` (`[Loader]`, default empty) is another mod's DLL to pass this DLL's calls on to, see [DLL names](#dll-names).

`PhysicsFPS` is the rate given to Havok as the expected maximum, 120 by default. Set it to `FPSLimit` when that is not 0, to your external cap, or to the refresh rate when `VSync` is what holds the framerate. A 60 FPS frame is unchanged. Faster frames shorten the physics step and scale durability loss by `frameTime * 60` (clamped to 0.05–2), including hits from enemies, bosses, and other players, for weapons, armor, and rings.

`JumpTrace` (default `false`) is a diagnostic for jump bug reports. It writes the player's per-frame jump physics to `DS2-FPS-Unlock-jumptrace-*.csv` next to the game, one file per launch. With `FPSUnlock = false` it only observes the stock game, for comparison.

## What changes in game

Both editions sample the real frame time and then normally step the world at a fixed 1/60. This mod turns off that wait and passes the measured step into gameplay and cloth. Health, stamina, and movement that already multiply by delta stay consistent.

Three per-frame rules are made to behave as they do at 60 FPS, the rate the PC game was tuned for:

- **Ground snap and jumps** (`GroundSnapFix`). While the character is grounded, the game pulls it down onto the floor after every physics step (a 0.1 unit sweep). That keeps the body inside Havok's contact band, and while the body is moving up the game takes Havok's contact flag as "grounded", so the pull repeats. At 60 FPS a jump's takeoff carries the body out of the band in a single frame and the loop breaks; at a high frame rate each frame moves it a fraction of that, so on steep downhills the character never left the ground. The game now treats a frame as released exactly when, at 60 FPS, it would have been: the body is commanded upward at 2.4 units/s or more (the stock takeoff measures 3.45 to 4, the wind-up that stays held 1.4). On those frames the pull is skipped and the grounded and contact flags are cleared as the game would have seen them. Every other frame keeps the stock pull, so runs, landings (including the roll after a jump or a long drop) and the short, stalled jumps the stock game also gives on the steepest slopes are unchanged.
- **Guard break and jump attack** (`ForwardAttackFix`). Forward + R1 and forward + R2 need the stick pushed forward from neutral within a short time. The game looks for that in a history of the last 16 frames: 0.27 s at 60 FPS, but 0.13 s at 120 and 0.07 s at 240, so a normal push fell out of the history. Frames are now folded together so the history spans what it does at 60 FPS.
- **Animation events** (`TaeEventFix`). The game dispatches the animation (TAE) events of the current 1/30 s TAE frame on every update. A track that has two events running at once marks both as starting on every dispatch, so their start effects (damage, effects, spawned objects) happened once per TAE frame at 30 FPS, twice at 60, and about five times at 144. Those repeats are now limited to what 60 FPS gives. The events themselves still run every frame.

Anything else that runs once per call, instead of once per second of animation, can still speed up. Cloth speed is corrected; the cloth simulation itself is still not identical to 60 FPS. Online play uses the measured step as well. That is not a promise that it matches a peer running at 60. Compatibility against other online players playing at 60 FPS seems good, but I am not a consistent DS2 PvP player and am not the best judge as to whether compatibility or fairness are perfect or merely acceptable.

## Known issues

Original Dark Souls II (DirectX 9). Seen in play, and not fixed:

- Motion blur can look wrong while the FPS unlock is on.
- Lighting can flicker when the aspect ratio is far from 16:9. Seen at 9:16 with the display turned to portrait. 16:10 does not flicker.

## Build

Visual Studio 2022:

```
build.bat
```

`build\xinput1_3.dll` is Scholar. `build\vanilla\xinput1_3.dll` is the original game. The exports for the other DLL names are listed in `exports_x64.def` and `exports_x86.def`.

## License

MIT. See [LICENSE](LICENSE).
