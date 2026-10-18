# Anachronox RTX status — 2026-10-18

The initial review reconciled anoxrtx `9a8dc3d` and QindieGL `bf25ebc8`.
The 2026-10-18 follow-up reproduced and corrected startup lighting across
graphics-device replacement, added capture/comparison tools, and ran controlled
Rowdy's comparisons. See [runtime evidence](https://github.com/felixarvid2/anoxrtx/blob/main/docs/startup-lighting-check.md).
The Linux cross-build retains 385 exports; all 61 anoxrtx Python tests pass. These
checks do not establish MSVC, stock Remix or other-map visual correctness.

The implementation order is maintained in
[anoxrtx PLAN.md](https://github.com/felixarvid2/anoxrtx/blob/main/PLAN.md).
[QindieGL ROADMAP.md](https://github.com/felixarvid2/QindieGL/blob/master/ROADMAP.md)
assigns wrapper work. Historical test reports retain the settings and failures
observed at their dates; they are not the current backlog.

| Area | Implemented / recorded verification | Remaining work |
| --- | --- | --- |
| Linux/Proton foundation | Windows DLL cross-build; 385 exports recorded; GE-Proton 11-9; stock Remix 1.5.2 and Remix Plus 1.5.1 integration | MSVC and broader runtime/game coverage |
| Image and upscaling | Borderless fullscreen; DLSS works, DLAA default | Aspect ratio, render resolution and frame-time baselines |
| Wine compatibility | Frame generation/present metering off; old GUI input; present semaphore off; NRC disabled with `rtx.integrateIndirectMode = 0` | Recheck on runtime upgrades; no claim NRC works |
| Map lights | BSP point/spot/sun lights, per-map formulas/settings, overrides and hot reload | Calibration outside Rowdy's; broader device/reset/runtime coverage |
| Light identity correction | QindieGL #21 merged; fresh identities fixed the reproduced reload failure; both saved views show scale 0/1 contribution | Startup device replacement now releases/rebuilds lights; stock/MSVC coverage remains |
| Animated / event lights | Timer-driven Quake styles 1–11 exist; frame dlights hook exists | Game-controlled styles/switches, pause/save synchronization, visible combat-light verification |
| Emissive / PBR | Static emissive generator; approximate normal/roughness/metallic and glass generation; visible normals and five sign emissions in two Rowdy's views | Dynamic emission, better classification, close-up rims and other environments |
| Textures / procedural lava | Chuncki hash remapping; HD sky/model/liquid generators; stable procedural replacement hashes | Visible lava freeze/replacement, water geometry/transparency and outdoor HD skies |
| Fog / sky | GL fog remapping; haze observed in bricksb, sunstrip and Rowdy's; per-map Numos sky on five outdoor maps | State-driven transitions, sun/shadow direction and whitecaves-b fog |
| Rig | Focused movement, verified save loading; physical XKB console input fix (#27 merged), 33 offline tests and both saved views recorded | Capture manifests and offline multi-frame comparison now implemented; outdoor/water/lava/combat locations remain |
| Debug UI | Generic ImGui overlay exists in QindieGL | Anachronox state/light/culling diagnostics and current Proton input validation |
| Culling / anchors / weather | Existing culling-related config experiments; Remix mesh/material API pointers | Anachronox BSP/PVS policy, anchor subsystem, wetness and rain are planned |
| Installation | `tools/install.sh`, `tools/install.py`, `tools/toggle.sh` exist | Repeat installation/recovery checks when those paths change |

## Evidence and open proposals

- [Console input acceptance](https://github.com/felixarvid2/anoxrtx/blob/main/docs/console-input-check.md):
  physical US key positions fix Swedish-symbol corruption; both saves, stale
  prompt clearing, repeat rejection and interrupted key release verified.
- [Map-light API investigation](https://github.com/felixarvid2/anoxrtx/blob/main/docs/maplight-api-check.md)
  and [saved-view follow-up](https://github.com/felixarvid2/anoxrtx/blob/main/docs/saved-view-lighting-check.md):
  successful API calls alone do not prove visible lighting.
- [Material comparison](https://github.com/felixarvid2/anoxrtx/blob/main/docs/rowdys-material-check.md)
  and [test locations](https://github.com/felixarvid2/anoxrtx/blob/main/docs/test-locations.md):
  current confirmed stationary gameplay views are indoors in Rowdy's.
- QindieGL #6 (flashblend) is already merged. Test the later `dlights` path
  first; do not enable both paths without checking duplicate illumination.
- [anoxrtx #28](https://github.com/felixarvid2/anoxrtx/pull/28) and
  [QindieGL #23](https://github.com/felixarvid2/QindieGL/pull/23) remain open
  external-model routing proposals at this review. They are separate from this
  feature roadmap; their proposed policy is not reported as merged.

## Source checkpoints

QindieGL: `code/rmx_maplights.cpp` implements timer styles, light creation and
draws; `idtech3_mixup/hooking.cpp` captures Anachronox frame dlights;
`code/rmx_gen.cpp` applies per-map config; `code/d3d_texture.cpp` freezes named
procedural uploads; `code/win_imgui.cpp` contains the existing overlay.
Heretic 2's PVS code is not an Anachronox/Linux implementation.

anoxrtx: `tools/bsp2lights.py` exports styles 1–11, skips START_OFF lights and
does not export a complete switch/event mapping. `tools/pbr_layer.py` already
classifies by basename substrings using first-match rules, with a fallback.
`tools/emissive_layer.py` generates static emission. Extend these paths rather
than treating the requested features as entirely new pipelines.

## Local checkout and runtime discipline

The original `anoxrtx`, `QindieGL` and `QindieGL-master` directories can lag
their default branches. Inspect refs and working trees before selecting work;
preserve local drafts and other worktrees. Documentation worktrees for this
update are `anoxrtx-roadmap` and `QindieGL-roadmap` under
`/home/felix/Documents/jensen2/`.

Machine paths, installed versions and power must be checked again before runtime
work. Keep worktrees, backups, logs and screenshots on persistent local storage;
serialize Steam/game/GPU use and restore settings on every exit. Saves, game
assets, generated layers, binaries and credentials stay outside commits.
