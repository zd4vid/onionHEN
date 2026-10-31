# Standalone FPS overlay

`fps_counter.elf` is the **only payload to load**. It samples the scanout
counter (/dev/dce, no game or kernel memory access), publishes it to
`/system_tmp/onionhen/fps_sample`, and injects an overlay-only ShellUI module
(built with `ONIONHEN_FPS_OVERLAY_ONLY` and embedded in `fps_counter.elf`) that
draws two plain white text rows, top-left, regular weight, no background panel:

    FPS: 59.94
    CPU: 43°C / GPU: 35°C

Build: `export PS5_PAYLOAD_SDK=...; ./standalone_fps/build.sh`

Output: `standalone_fps/build/bin/fps_counter.elf` (the only file in `build/bin/`).
The overlay module `shellui.elf` is an intermediate under `build/internal/`;
never load it separately (it is already embedded, and loading it on its own
fails).

kstuff is optional. The payload logs whether it is running; without it the
overlay's hooks fall back to a kernel mprotect. It also skips injection if
SceShellUI already has the overlay, and waits 5 s before the first injection
(change it by writing a number of seconds to `/data/onion_fps_delay`; 0
disables the wait).

All values, FPS included, refresh together every second, and immediately when
a game opens or quits.

Running next to OnionHEN: the overlay signals its own ready marker
(`fps_overlay_ui`), not OnionHEN's `toolbox` one, so OnionHEN still injects its
toolbox into SceShellUI. While OnionHEN is running and its toolbox is not in
SceShellUI yet, the payload waits (up to 5 s) instead of injecting, so the two
injections never overlap; the log says `OnionHEN is running; waiting for its
toolbox`. If OnionHEN's own FPS overlay is also enabled you will see two
overlays; turn one of them off.

This branch contains only what the standalone build compiles (`standalone_fps/`
plus the `source/` libraries and the ShellUI module it reuses).

Log: `/data/onion_fps.log` - look for `kstuff detected` (or the warning that it
was not), then `injecting fps overlay` followed by `fps overlay ready`.

Appearance is fixed in code: layout in `layout_bar_labels()`
(`source/shellui/src/prx_overlay.cpp`), font size in
`overlay::kStandaloneFontPt` (`source/shellui/include/overlay_layout.hpp`),
colour/weight in `source/shellui/src/shellui_overlay_widgets.cpp`, enabled
metrics/position in `fps_overlay_only_main()` (`source/shellui/src/prx.cpp`).

Stopping the payload: on a clean exit (SIGTERM/SIGINT) it publishes a shutdown
marker and the overlay removes all its widgets at once. If the payload is
killed outright, the overlay notices that the heartbeat (the payload publishes
several times a second) stopped for 1 s and removes its widgets too. Starting
the payload again brings the overlay back without re-injecting.

## Stability diagnostics (flag files in /data, no rebuild needed)

- `onion_fps_noinject`: payload samples only, never injects (isolates injection).
- `onion_fps_nodce`: payload injects but does not read /dev/dce (isolates the sampler).
- `onion_fps_narrow_auth`: elevate the authid only while the detour is written (untested). Default keeps it elevated for the whole setup, the behaviour known to work.
- `onion_fps_patch_main_check`: also patches CheckRunningOnMainThread (off by default; the overlay only runs on the UI thread).

The injector tries at most 3 times per SceShellUI pid and never injects a second copy after a successful injection.
