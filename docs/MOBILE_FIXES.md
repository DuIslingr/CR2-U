# CR2 mobile issue audit (2026-10-28)

Checked the CR1/Omega report against CR2's separate Squirrel/MVGL viewer and event runtime.

## Changes

- Items, Sopia, equipment, the manual and Display & Graphics accept vertical swipes and mouse-wheel scrolling. A swipe
  changes the focused row without choosing it; a stationary touch chooses on release.
  Scrolling clamps at the ends, and a menu change cancels the gesture.
- Story `Cr3Sound.PlayVoice` replaces previous dialogue speech. Advancing past a completed
  message stops its voice; the first tap that reveals the text keeps it playing. Intermediate
  pages retain the voice. Battle voice mixing remains in the audio player's separate API.
- Lock Orientation is available in the settings list, applies immediately on Android, and
  persists both its enabled state and landscape direction in `settings.ini`. Unlocked mode
  permits both landscape directions. Android's Activity changes orientation on its UI thread.
- Vita ATRAC9 decoding uses unsigned bit arithmetic and handles zero-width bit reversal.
  Both streamed music and decoded SXD clips receive a padded compressed superframe for the
  decoder's Huffman lookahead, with consumed-byte bounds checked against the actual frame.
- The original Options screen's Lock Orientation On/Off buttons are wired to the same setting.

## Verification

- Desktop and Android arm64 native viewers compile; the Android GameActivity and SDL Java
  sources compile against Android API 34.
- `ctest --test-dir engine/build --output-on-failure -R 'mobile-input|atrac9-bits'` covers
  gesture thresholds, horizontal-drag cancellation, list-end clamping, landscape hints,
  actual Squirrel dialogue voice interruption, and clearing an old white fade on reset.
  ATRAC9 tests cover 2,433,024 bit-reader/sign-extension cases.
- The same tests pass with AddressSanitizer and UndefinedBehaviorSanitizer applied to both
  C and C++. Leak detection is disabled because the local sandbox does not support it.
- A temporary viewer integration harness checked real menu cursor routing, release after a
  swipe without activation, list bounds, and all four lock/direction save/reload combinations
  in an isolated profile. No user saves were used.
- Android, iOS and Vita data each rendered a field and the settings UI in a headless desktop
  viewer under those sanitizers, using isolated profiles.
- `cr2tool sound <Vita resources>` passed for 391 effects, 3,249 voices and 37 music tracks,
  with no ASan/UBSan errors. Music loop-seam measurements are diagnostic, not failures.

## Findings that do not share the CR1/Omega cause

Equipment tap rectangles are derived from the posed native UI joints. The CR1/Omega static
row-hitbox defect was not found here. Area entry resets EventRuntime fades (CR3's 3D areas
start a fresh event runtime), so the old white arrival-mask defect does not apply to these
paths. The original report's crystal-room crash has not been reproduced in CR2.

Physical Android touch and rotation testing remains necessary before claiming device-level
verification. Rendering Android/iOS/Vita data on desktop is not a build or device test of the
iOS/Vita platforms. These changes are included in v0.4.2.
