# New game and retry state audit

CR2's `newGame()` previously only applied system data and initialized starting
items, equipment and sopia. The title's caller separately reset GameState, but
the direct-play Return to Title fallback called `newGame()` alone. That path
could retain previous story progress, inventory, playtime and old UI state.
CR3's legacy field flow shared the same split initialization.

`newGame()` now begins with an explicit session reset: discard old menus,
pending import results, runtime scripts, pointer state and scheduled scene
audio, construct fresh game state and reset playtime. System settings and
unlocks are applied afterward. The title/retry caller no longer duplicates the
game-state reset. Explicit New Game Plus carryover and save loading still run
after initialization, so their chosen data supplies the new session's progress.

CR3's native title previously reset game state but did not reset playtime when
selecting an empty file. It now uses the same reset boundary, preserving its
offline database and Sync callback. Its one/two-handed control setting remains
unchanged. The native path does not apply legacy system story flags.

The new `viewer-new-session` CTest seeds old story progress, inventory, time,
menus, gestures, delayed sounds and an asynchronous picker, then enters through
`newGame()`. It verifies a fresh session with global unlocks and preferences
retained. CR3 also checks its native reset and Sync callback. The opt-in
`CR_SELFTEST_NEW_SESSION=1` real-data check runs the actual native title's
selected-file transition into the prologue and checks fresh progress and zero
time. Test profiles are isolated; no player saves are modified.

CR1 and Omega already construct fresh GameState on their direct new-game
paths; their scripted pair/scenario initialization intentionally handles the
shared flags and clear-data carryover separately. No changes to those games
are made in this pass. Their shared playtime semantics were not changed.

These are source and desktop runtime checks, not physical-device validation.
The work remains local and uncommitted; no release is published.

Validation: all 30 CTests for the changed games pass in native and GLES
ASan/UBSan builds (CR2 14, CR3 16), including CR3's New Game Plus regression.
CR2's actual text-save load checks pass with Android and iOS data in both builds
(four runs). CR3's native selected-empty-file transition enters the prologue
with fresh progress and zero playtime for Android, iOS and Japanese Vita data
in both builds (six runs). LeakSanitizer is disabled for the sandbox tracing
limitation. Diff whitespace checks pass in both repositories.
