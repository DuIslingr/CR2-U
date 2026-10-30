# CR2 / CR3 scene reset audit

Targeted follow-up, 2026-10-29. These ports use a different event runtime from
CR1/Omega, so the field-task patch was not copied into them.

## Confirmed behavior and fix

`EventRuntime::reset()` cleared the scene, fades and visible dialogue, but retained
other input and presentation state. `FieldScript::enter` calls it when entering a
room; `EventRuntime::start` calls it when starting another scene/function.
CR3 also reuses `areaEvents` for successive area interactions in the viewer.

Both ports now clear the previous scene's map-animation pause, radial effect,
skip button/held skip state, pending tap and touch timer, message permissions,
item icon, message count, parsed message and execution-finale state. Host playback
policy, language, selected manual page, game state and existing native bindings
are retained. This prevents a previous scene from freezing a map, consuming the
next scene's first input or leaving new dialogue unable to advance.

CR3 additionally closes its previous choice/input/reward/collection/shop/Spaceport/
world-select UI, clears temporary text substitutions, banners and gift-effect
state, and discards its secondary vista map and map-selection flag. This avoids
showing an old menu or map in a new scene.

The change implements the runtime's existing documented contract: reset returns
presentation state to that of a new scene. It does not reset progression or
modify game assets/saves.

## Original-code evidence

Fresh REA/Ghidra static inspection, with functions selected by their exported
mangled symbols (older saved projects use different image bases):

| Target | SHA-256 |
|---|---|
| CR2 Android 2.1.2 `libChaosRings2.so` | `5534b905725b276887fecb2ac5c06f9d733a551a28841212c5e9c66c04d250ec` |
| CR3 Android 1.1.3 `libcrx.so` | `43950b32c2c064cf25c6324a75d2b89c0cfc243996dcbb20961ef855bd99bcf3` |

| Function | Observation | REA Evidence ID |
|---|---|---|
| CR2 `VistaMain::VistaMain` | The map-pause byte at +0x28 starts false. | `ev_70d09319f094182eb7e3f86b5cdbe56f02ee752dd0528581b409b6844516ace4` |
| CR2 `EvtUtilSetVistaMapAnimPause` | Writes the same +0x28 byte. | `ev_a244f849caa2e3480beb8686002228e85b5ac1839fb698d49362e77f3e487dae` |
| CR2 `Cr3Event::Init` | Allocates new event-window objects and script manager. | `ev_a445c7f16e5107e54802365a639e0e4304bb8397a51f35b63204e19447a7edf1` |
| CR2 `InterfaceMain::AllDelete` | Deletes the interface objects and clears their container/state. | `ev_90c028c65cdae519013ea57bca69bc8944ef8040f73d650f93bfd3f7b7c2e463` |
| CR3 `VistaMain::VistaMain` | Constructs fresh map models and initializes vista state. | `ev_7cd8618433419d1e2c9b5a1ce5460bb40bc165d807b13a244df2ecd41b779ed4` |
| CR3 `VistaMain::ChangeMap` | Toggles primary/secondary selection at +0x22a/+0x2aa. | `ev_fc335aa342ea33f0fc6989ba7a11f6a6d6f2235b114af2aa7d37fc332596652a` |
| CR3 `VistaMain::SetMapAnimPause` | Updates map-model pause flags. | `ev_34ed3ce518f2027fc384e1cc813ba301ff5d60edf58b9522c1eb55fd48f7a420` |
| CR3 `InterfaceMain::AllDelete` | Deletes interface objects and clears their container/state. | `ev_069e5ce2a453d4fa6468e24050f54b09cded00bc687b259cef1c9020f68607c5` |

CR3's existing local `VistaMain::Init` decompile explicitly clears the two map
selection flags before a vista starts. These are static observations, not traces
of an original game running. They support fresh-scene defaults; they do not prove
that the original calls global interface teardown on every individual dialogue.

Evidence bundles and logs are under `/tmp/cr-transition-audit/`, outside the
repositories; no original code or game data is included in this patch.

## Regression and asset checks

The new asset-free `event-reset` CTest uses real Squirrel native calls to dirty a
scene, resets the runtime, then checks behavior in the next scene: tap consumption,
dialogue reveal/advance, skip permission, map playback and menu visibility. It also
checks repeated resets and that bindings/configuration remain usable. Before the
fix it reported 8 failed checks in CR2 and 12 in CR3; after the fix both pass.

Both native desktop builds pass their complete CTest suites: CR2 4/4, CR3 6/6.
The same suites pass with AddressSanitizer/UndefinedBehaviorSanitizer instrumentation.
LeakSanitizer is disabled because the sandbox uses ptrace; ASan/UBSan remain on.

Asset replay under ASan/UBSan:

- CR2 Android, iOS and Vita: all 70 standalone events and 217 map-entry scripts
  complete per source. Each field sweep also completes 136 tappable interactions;
  no native stubs are reached. The two map entries intentionally leaving fades
  are reported by the existing checker, not classified as stuck screens.
- CR3 Android, iOS and Vita: four actual `t01` functions complete per source
  (12 replays): `s000_t01_00101_evt`, `s000_t01_00103_evt`,
  `s272_t01_00401`, `c111_s102_t01_00510`. These cover the prologue, its
  secondary-map scene, a quest/gift event and a clerk interaction. Headless
  scripts can reach existing viewer-only stubs; completion is not proof of every
  visual detail or actual touch behavior.

The inherited CR2-style `cr3tool play ... all` selects standalone `_evt.nut`
files, whereas CR3 stores its events as functions in area scripts. It selected
zero events and previously returned success. It now fails with instructions to
use `cr3tool events <release>` for enumeration or `play <release> script:function`
for a specific replay. Zero-event runs and the inherited CR2 map-table checker
are not counted as CR3 coverage.

## Limits

These checks run the ports on desktop with local release data; they do not run
original ARM code or replace Android-device activity/surface/orientation testing.
They establish cross-scene cleanup regressions, not the cause of the previously
reported CR1 crystal-exit device crash. CR3 coverage here is deliberately bounded
to the selected real functions and the reset regression; it is not an exhaustive
replay of all of CR3's events.
