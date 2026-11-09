#pragma once

// Start-up notice and logos (INI SkipIntro).
//
// The title screen is a state machine (FeStateTitle) of sub-states with numeric ids. Its first sub-state,
// FeSubStateTitleInitBranch, picks where the title starts. On its first entry in a session (a global "booted"
// byte is still 0) it goes to the start-up screens, otherwise (back to the title from the game) straight to
// the title screen:
//   Scholar of the First Sin: 1 FeSubStateWarningNoCopy (a notice, 3 s, cannot be skipped) -> logos 0x13..0x16
//     (FeSubStateTitleLogo, 1.5/1.5/2/2 s) -> 0x17 FeSubStateTitleMain ("press any button").
//   Original game: 2 a system message box (messages 100/101, closes after 10 s or a button), or 1
//     FeSubStateWarningNoCopy -> logos 0x14..0x17 -> 0x18 FeSubStateTitleMain.
// InitBranch registers those two choices as transition objects in vtable slot 5 (Scholar fn 0x1400FDFC0,
// original fn 0x5945D0): {target id, watched field = InitBranch+0x10 (original +0xC), value 0 = first entry,
// 1 = returning}. With SkipIntro the slot is wrapped and the first-entry transition gets the returning one's
// target, so the game opens the title screen at once, the way it does when you quit to the title.
// The vtable is found through its RTTI name, so nothing depends on fixed addresses.
bool intro_install();
