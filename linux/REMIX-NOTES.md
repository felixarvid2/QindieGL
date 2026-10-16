# RTX Remix under Wine/Proton (Anachronox, 32-bit game)

Findings from running the Remix bridge (remix-1.5.2) with this QindieGL build on Proton Experimental,
NVIDIA RTX 5060 laptop + AMD iGPU, driver 595, COSMIC/Wayland (XWayland).

## Fixed: server hang inside Present
Symptom: client logs `Still waiting on the Present semaphore`, server (`NvRemixBridge.exe`) stuck in
`IDirect3DSwapChain9_Present`, `dxvk-queue` polling `/dev/nvidia0`, GPU idle (P8).
Cause: DLSS frame generation is on by default in Remix (`rtx.dlfg.enable = True`, plus hardware present
metering). Add to `rtx.conf` in the game folder:

    rtx.dlfg.enable = False
    rtx.dlfg.enablePresentMetering = False

(Disabling Reflex, DLSS (`rtx.upscalerType = 0`), ray tracing or vsync did not help on their own.)
After this the message channel handshake completes, Remix finds the camera (`Camera cut detected`) and
the server keeps rendering. `presentSemaphoreEnabled = False` in `.trex/bridge.conf` was used while
debugging; not re-tested with it enabled.

## Fixed: window turns white after the first frame
Cause: Remix' new GUI input method (`rtx.useNewGuiInputMethod`, default True) creates "RTX Remix Overlay",
a WS_EX_LAYERED window with alpha 0 placed over the game's client area. On Windows DWM makes it invisible;
under Wine/X11 it only gets `_NET_WM_WINDOW_OPACITY = 0`, which COSMIC and a plain X server ignore, so it
is drawn as an opaque white window on top of the game (found with python-xlib in a nested Xwayland: an extra
640x480 toplevel owned by NvRemixBridge.exe at the game window's position). Fix in `rtx.conf`:

    rtx.useNewGuiInputMethod = False

How presenting works here: the 64-bit bridge server renders into an offscreen X window and Wine blits it
into the 32-bit game's window after each present ("other process window rendering");
`WINE_DISABLE_VULKAN_OPWR=1` makes surface creation fail ("Couldn't load renderer!"), so leave it unset.

Working setup (GE-Proton 11-9): rtx.conf with `rtx.dlfg.enable = False`, `rtx.dlfg.enablePresentMetering = False`,
`rtx.useNewGuiInputMethod = False`; `.trex/bridge.conf` with `presentSemaphoreEnabled = False`.
Do not use `PROTON_ENABLE_WAYLAND=1` with this setup: it crashed the desktop session.
Safer testing: run a nested `Xwayland :7 -geometry 1024x768 -decorate` and put `DISPLAY=:7` in the launch options.

## Debug tips
- Use the `debugoptimized` Remix zip + `logLevel = Trace`, `logApiCalls = True`, `logAllCommands = True`
  in `.trex/bridge.conf`; bridge32.log/bridge64.log show the last command (UID) each side processed.
- No gdb: read thread stacks from /proc/<pid>/task/*/syscall + /proc/<pid>/mem (SIGSTOP first), and
  `strace -p <tid>` for what a thread polls (`ls -l /proc/<pid>/fd/<n>`).
- `cosmic-screenshot --interactive=false --save-dir=<dir>` takes a screenshot on COSMIC.
