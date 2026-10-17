# Handoff: Anachronox + RTX Remix on Linux (for Codex)

This file is identical in both repositories:
- **felixarvid2/anoxrtx**: the Remix mod (configs, USD layers, tools, installer). Fork of vinevi1/anoxrtx.
  Local checkout: `/home/felix/Documents/jensen2/anoxrtx` (branch `main`).
- **felixarvid2/QindieGL**: the OpenGL→D3D9 wrapper that makes the game Remix-compatible, with a Linux
  cross-build. Fork of whisperglen/QindieGL. Local checkout: `/home/felix/Documents/jensen2/QindieGL` plus
  git worktrees `QindieGL-master` (normally used, branch `master`), `-flash`, `-maplights`, `-ml2`, `-texdump`.

The owner (Felix) writes in Swedish; answer in Swedish. Repo text, commits and code are in English
(`RESULTS.md`, `PLAN.md`, `TODO.md` in anoxrtx are Swedish).

## Working agreements
- After finished work: push a branch, open a PR, merge it (`gh pr create` + `gh pr merge --merge`). The owner
  expects this; ask only for things outside the two repos.
- Never commit game data (textures, DDS, BSPs, generated layers). Tools generate them from the user's files.
- QindieGL: nothing is done until the Linux build passes and still exports 385 functions:
  `cmake -S . -B build-linux -DCMAKE_TOOLCHAIN_FILE=linux/toolchain-i686-llvm-mingw.cmake && cmake --build build-linux -j8`
  `~/.local/llvm-mingw/bin/llvm-objdump -p build-linux/opengl32.dll | awk '/Ordinal +RVA +Name/{f=1;next} f&&NF==3{n++} END{print n}'`
- Do not use `PROTON_ENABLE_WAYLAND` outside the nested test session: it crashed the owner's desktop once.
- `pkill -f <pattern>` can kill your own shell when the pattern appears in the command line; use `[x]`
  bracket patterns or check pids.
- The machine has 14 GB RAM and an 8 GB RTX 5060 Laptop GPU. The game (Remix bridge ~5 GB) and ComfyUI (FLUX)
  together run out of memory; ComfyUI is currently stopped (`~/ComfyUI/start-comfyui.sh` starts it).
- Back up game files before replacing them (the installer does this into `<game>/anoxrtx-backup/`).

## Machine and game
- Game: `~/.steam/debian-installation/steamapps/common/Anachronox` (Steam app 242940), GE-Proton 11-9.
  Proton prefix: `~/.steam/debian-installation/steamapps/compatdata/242940/pfx`. Wine sees the game as
  `S:\steamapps\common\Anachronox`.
- Steam launch options (localconfig.vdf, app 242940):
  `__NV_PRIME_RENDER_OFFLOAD=1 __VK_LAYER_NV_optimus=NVIDIA_only __GLX_VENDOR_LIBRARY_NAME=nvidia VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json DXVK_LOG_LEVEL=info WINEDLLOVERRIDES="opengl32=n,b;d3d9=n,b" %command%`
  Steam must be shut down (`steam -shutdown`) before editing localconfig.vdf, or it overwrites the change.
- Installed now: QindieGL build from `master` (opengl32.dll), **Remix Plus 1.5.1** runtime (stock Remix 1.5.2 is
  kept as `.trex.remix152` + `d3d9.dll.remix152`), Chuncki's 4x texture pack (`anoxdata/textures`,
  `TEXTURES.dat` renamed `.orig`, the pack's `ref_gl.dll`), mod at `rtx-remix/mods/anoxrt` with sublayers
  hd_sky, liquids, emissive, models_hd, pbr, proctex. QindieGL.ini `[game.anox]`: remixapi, maplights,
  maplights_scale 100, dlights, proctex, smoothnormals, camera detection. `anox_customise.ini` next to anox.exe.
- Tools: llvm-mingw in `~/.local/llvm-mingw`; Python venv with Pillow, numpy, xxhash, usd-core, python-xlib:
  `/tmp/claude-1000/-home-felix-Documents-jensen2/bd228632-acec-45ae-b061-56839c96ea01/scratchpad/venv/bin/python`
  (temporary; `tools/install.sh` creates its own in `~/.cache/anoxrtx/venv`). Real-ESRGAN ncnn is downloaded by the
  installer. `gh` is logged in as felixarvid2.

## How to test in the game (anoxrtx `tools/anoxrig.py`)
The rig drives the game in a nested X server so the owner's desktop is not disturbed:
1. Start `Xwayland :7 -geometry 1920x1200 -decorate -noreset` if not running.
2. Shut Steam down, prefix the launch options with `DISPLAY=:7 ` (optionally append ` +map <name>` after
   `%command%` to start straight into a map, e.g. `+map sunstrip`, `+map hephlava`), start the game with
   `anoxrig.py start`.
3. `anoxrig.py shot NAME` (screenshot to `tools/shots/`), `to-rowdys` (New Universe → Rowdy's bar, ~4 min),
   `timeline PFX T...`, `key`, `chord`, `sendkey`, `move`, `click`, `log`, `stop`.
4. Afterwards: stop the game, shut Steam down, remove `DISPLAY=:7 ` and `+map ...` again, kill the Xwayland
   on :7, restart Steam (`DISPLAY=:1 steam -silent`). Always restore this, also when a test fails (use a trap).
- Logs: `<game>/QindieGL.log` (MapLights, DLights, ProcTex, RTX.conf lines), `<game>/rtx-remix/logs/`
  (`remix-dxvk.log`, `bridge32.log` – "The RTX Remix Runtime has encountered an unexpected issue" = Remix crash).
- Known rig limits: keyboard input does not move the player (so places away from a map's start point
  cannot be reached); `start` prints "timeout waiting for Remix camera" with Remix Plus although the game runs.

## What exists (see README.md in each repo for details)
QindieGL options (all in `[game.anox]` of QindieGL.ini unless noted):
- `BorderlessFullscreen` ([Settings]), `smoothnormals`, `DumpTextures` (Remix hashes + TGA dumps).
- `maplights`: static Remix API lights per map from `rtx-remix/maplights/<map>.lights` (written by
  anoxrtx `tools/bsp2lights.py`, `format raw`), Quake 2 light styles, sun, spotlights. Light formula and Remix
  variables per map in `anox_customise.ini` (`[maplights.global|<map>]`, `[rtxconf.default|<map>]`); per-light
  hand fixes in `<map>.override`; live reload when these files are saved; camera position logged on reload.
  Map changes are applied 120 frames late. Code: `code/rmx_maplights.cpp`, hook in `idtech3_mixup/hooking.cpp`.
- `dlights`: hooks the non-exported `R_RenderView` in ref_gl.dll (found via its "NULL worldmodel" string,
  at +0x176bd); Anachronox refdef_t has num_dlights/dlights at +0x74/+0x78; drawn as Remix sphere lights.
- `proctex`: hooks anoxgfx.dll `TEX_BindTexture` + `TEX_GetTexName` to name GL textures; textures matching
  `proctex_names` (default `proc_lava`) are frozen to a flat colour from fnv1a32(name), giving a stable Remix
  hash (computed offline by anoxrtx `tools/proctex_layer.py`; QindieGL logs the real hash).
- Remix API: works with stock Remix and Remix Plus (Plus inserts functions in `remixapi_Interface`; QindieGL
  maps them by layout in `code/d3d_global.cpp`).

anoxrtx tools (`tools/`): `install.sh`/`install.py` (whole setup from the user's files, steps can be run alone),
`toggle.sh`, `adat.py`, `bspinfo.py`, `bsp2lights.py`, `lights_near.py`, `remixhash.py`, `make_layers.py`
(sky + liquids), `emissive_layer.py`, `pbr_layer.py`, `hd_layer.py`, `flux_refine.py` (ComfyUI FLUX posters),
`proctex_layer.py`, `rehash_textures.py` (+ `maps/chuncki_texmap.tsv`), `lightcal.py`, `anoxrig.py`.

## Pitfalls already solved (do not undo)
- `rtx.conf`: `rtx.dlfg.enable = False`, `rtx.dlfg.enablePresentMetering = False` (Remix hangs in Present under
  Wine), `rtx.useNewGuiInputMethod = False` (white window under Wine), `rtx.integrateIndirectMode = 0`
  (NRC cannot start under Wine; without this line any runtime `SetConfigVariable` re-enables NRC and crashes
  Remix), `rtx.upscalerType = 1` + `rtx.qualityDLSS = 5` (DLAA; DLSS works under GE-Proton).
- `.trex/bridge.conf`: `presentSemaphoreEnabled = False`, `exposeRemixApi = True`.
- Remix Plus first-start dialog blocks input and defaults crash/usage reports to NVIDIA to on. It is answered
  (data collection off, owner's choice) in `pfx/drive_c/users/steamuser/AppData/Local/NVIDIA/RTXRemix/user_settings/<xxh3(game root)>.conf`.
  Never use `rtx.automation.disableBlockingDialogBoxes`: it grants the consent.
- Numos sky (`rtx.skyMode = 1`, Remix Plus) must stay per map: globally it replaces the space/city skies.
  It is on for sunstrip, starport, comhills, baltown, baltownr (maps with an ArghRad sun), with the map light sun
  off there.
- Texture hashes: Remix hash = XXH3_64 of mip 0 in D3D format (BGRA, alpha 255 without alpha). The texture pack
  changes level texture hashes; the mod's hand-made materials are remapped with `rehash_textures.py`.

## In progress when this was written
- **Fog (idea from the second research round):** the game's GL fog reaches Remix. `rtx.enableFog` alone does
  almost nothing; adding `rtx.volumetrics.enableFogRemap = True` and `rtx.volumetrics.enableFogColorRemap = True`
  gives the original teal haze in `bricksb` (screenshots `tools/shots/fog_bricksb_{1,2,3}.png`). A comparison on
  rowdys, whitecaves-b (densest fog, 0.0025) and sunstrip was running (`fog_<map>_{1,2}.png`, 1 = without remap,
  2 = with). Next: check that rowdys/whitecaves are not too dense, tune `rtx.volumetrics.fogRemap*` bounds if
  needed, add the two lines to anoxrtx `rtx.conf`, README note, PR + merge.

## Not yet verified in the game
- Lava freeze (`proctex`): look in Levant for "ProcTex: ... frozen ..., Remix texture hash 0x561447B2DFA181FD"
  (proc_lava) / `0x26C0B031CCFA2266` (proc_lavafloe) in QindieGL.log; the rig cannot walk to the lava.
- Dynamic lights in combat (`dlights`); if they work, close QindieGL PR #6 (flashblend, superseded).
- PBR normal maps may give bright rims on sharp colour edges (`pbr_layer.py --strength`, default 2).
- Numos sun rotation convention (shadow direction) on the five outdoor maps.
- Map light brightness in environments other than Rowdy's.

## Ideas not done yet (from anoxrtx IDEAS2.md and the owner's last question)
1. In-game menu for lights/overrides (QindieGL already has ImGui code for other games: `qdx_imgui_*`).
2. Weather per map with Remix Plus presets (rain/snow where the game has them).
3. Remix variables driven by game state (cutscenes, underwater) – 2.2.
4. Anchor meshes (4.1), anti-culling control (4.2), complete `rtx.uiTextures`/particle/beam lists (TODO.md §3).
5. Make the rig able to move the player (would allow verifying the items above automatically).
