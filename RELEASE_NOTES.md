# Release notes

Draft for the next release. Not tagged or published.

The build identifies itself in `DS2-FPS-Unlock.log` as `DS2-FPS-Unlock v1.3.4`.

## Frame cap

`FPSLimitPoint` now defaults to `present`.

`present` waits just before the picture is shown. While the game is reaching the cap, each picture goes out on the cap's clock. That is the steadiest frame pacing.

`early` waits at the start of the frame, before the controller is read. Use it for the lowest input delay while the game is reaching the cap, or on a variable refresh rate (VRR) display, which can show small gaps between frames without the judder a fixed-refresh screen would.

Only the point you name waits. Restart after changing it.

## Steam Deck, Scholar of the First Sin

In fullscreen, turn on "Disable Frame Limit" in the performance overlay and set `FPSLimit`. The mod's cap then holds, and the Deck's own limiter stays off. The Deck limiter waits in the compositor and adds input lag.

`present` waits before the picture is shown. `early` waits at the start of the frame when that point is found in the game. If this build of Scholar does not contain the early wait, the cap waits before the picture instead, and the log says `FPSLimit waits before Present`. The original DirectX 9 game already applies either point with the Deck limiter disabled.

## Known issues (not fixed)

Original Dark Souls II, DirectX 9:

- Motion blur can look wrong while the FPS unlock is on.
- Lighting can flicker at aspect ratios far from 16:9. Seen at 9:16 with the monitor in portrait. 16:10 does not flicker.

16:10 gameplay is a true vertical-plus view on both editions. The 16:9 horizontal view stays, and the extra height shows more above and below. Most of the HUD and the FMV videos are not vertical-plus. They stay the shape the game draws them. That is acceptable for now.
