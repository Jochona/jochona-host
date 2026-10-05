# Vibepollo vs Jochona Host/Client — Feature Comparison & Controller Deep Dive

_Research date: 2026-10-05. Read-only investigation; no code changed. All claims cite a repo path or URL; items I could not verify directly are marked **[UNVERIFIED]**._

Sources:
- Vibepollo: https://github.com/Nonary/Vibepollo (README: https://raw.githubusercontent.com/Nonary/Vibepollo/master/README.md; file tree via `https://api.github.com/repos/Nonary/Vibepollo/git/trees/master?recursive=1`; GPL-3.0, forked from `ClassicOldSong/Apollo`, itself forked from `LizardByte/Sunshine`)
- Companion driver: https://github.com/Nonary/libvirtualgamepad (MIT License)
- Reference alternative: https://github.com/LizardByte/libvirtualhid (license "Other" — **commercial license required for Windows driver-backed gamepads and macOS gamepads**; Linux/FreeBSD backends are free)
- Jochona Host: `/Users/gogolb/Projects/jochona/host` (GPL-3.0, `host/LICENSE`; fork of `LizardByte/Sunshine` per `host/README.md:27`)
- Jochona Client: `/Users/gogolb/Projects/jochona/client` (Moonlight-qt fork, SDL2-based)

---

## 1. Vibepollo feature inventory (grouped by area)

### 1.1 Display / capture (Windows)
- Own bundled virtual-display driver (default) with SudoVDA kept as rollback — README "Native Virtualized Display".
- Windows Graphics Capture (WGC) run **in service mode** for full frame-rate capture of frame-generated titles, auto-switching capture methods so login screen/UAC prompts stay captured — README "Windows Graphics Capture in Service Mode".
- Display-setting automation: safeguards against "stuck" dummy-plug/virtual-display state across crashes/reboots — README "Display Setting Automation"; `src/display_device.cpp`, `src/display_device_policy.cpp`, `src/display_helper_builder.cpp`, `src/remote_display_topology.cpp`, `src/virtual_display_scale.h` (paths from repo tree).
- Headless auto-enable to avoid 503s / false HEVC-support detection (README).

### 1.2 Linux (beta) host
- Native Linux machine-wide service install for Arch/CachyOS + KDE Plasma Wayland, own virtual-display kernel driver — README "Linux (beta)", `docs/linux/install.md`.
- SteamOS "user bundle" packaging that leaves the read-only OS untouched, patched Gamescope HDR10 path — `packaging/linux/steamos/**` (large subtree: `README.md`, `AUDIT.md`, `gamescope/README.md`, `gamescope/VIRTUAL-DISPLAY.md`, `sysext/*`, `tests/*`).
- Linux DS4/DS5 controller emulation via UHID / a custom `vibeshine-ds5` composite USB gadget kernel module — `docs/linux/playstation-controllers.md`; `linux/vibeshine-ds5/*` in the libvirtualgamepad repo. Linux-only; not applicable to Jochona Host (Windows-only).

### 1.3 Launcher integrations (Playnite / Lutris / Steam)
- Playnite integration: recently-played sync, per-category rules, exclusions, manual add via Web UI, artwork, clean launch/termination incl. emulators — README "Playnite Integration"; `src/config_playnite.cpp/.h`, `src/config_playnite_parse.cpp`, `src/confighttp_playnite.cpp`.
- Lutris integration (Linux) — `src/config_lutris.cpp/.h`, `src/confighttp_lutris.cpp`, `src/lutris_artwork.cpp`, `src/lutris_auto_sync.cpp`, `src/lutris_integration.cpp`, `src/lutris_sync_policy.cpp`.
- Steam local-library discovery/launch (no Web API key needed), Steam Big Picture session game cleanup (SIGTERM→SIGKILL for games started during the session), Proton compatibility toggles — `src/config_steam.cpp/.h`, `src/confighttp_steam.cpp`, `src/steam_integration.cpp/.h`, `src/steam_artwork.cpp`, `src/steam_auto_sync.cpp`, `src/steam_process_tracker.cpp`, `src/steam_sync_policy.cpp`, `src/steam_big_picture_policy.h`; docs: `docs/linux/steam-big-picture.md`.

### 1.4 Frame pacing / GPU vendor integrations
- RTSS integration: applies correct frame limit + disables V-Sync to match client FPS — README "RTSS & NVIDIA Control Panel Integration"; `src/confighttp_rtss.cpp`, `src/app_framegen_config.cpp/.h`, `src/framegen_policy.h`.
- NVIDIA Control Panel automation (README, same section).
- Lossless Scaling auto-config + optional NVIDIA Smooth Motion on RTX 40-series+ — README "Lossless Scaling & NVIDIA Smooth Motion"; policy flags visible in `platf::steam::session_launch_policy_t` (`smooth_motion`, `smooth_motion_graphics_queue`) in `src/steam_integration.h`.
- Global "DualSense compatibility for Proton games" toggle that injects `PROTON_KEEP_SONY_AUDIO_ENDPOINT_VISIBLE=1` / `PROTON_SONY_WINDOWS_DEVICE_NAMES=1` — `docs/linux/playstation-controllers.md` §"Global Proton DualSense compatibility"; Linux-only.

### 1.5 Streaming / transport
- Optional WebRTC streaming path alongside the standard ENet/RTSP stack — `src/webrtc_stream.cpp/.h`, `cmake/dependencies/webrtc.cmake` (SignPath note in README confirms this ships in official builds).
- PyroWave wavelet video codec option — `src/pyrowave_host.cpp/.h`, `src/pyrowave_policy.cpp/.h`, `src/pyrowave_protocol.h`, `docs/linux/pyrowave.md`, `docs/pyrowave-protocol.md`.
- Host-to-client controller **PCM haptics** forwarding (distinct from HID rumble) for native DualSense waveform feedback over an unreliable ENet channel — `docs/linux/playstation-controllers.md` §"Native waveform feedback to Moonlight" (requires a coordinated Moonlight-fork client; **not** in stock Jochona Client — see §2.6).

### 1.6 Web UI / auth / ops
- Responsive, "dependency-light" config UI reorganized around common tasks — README "Focused Configuration Interface".
- API tokens scoped to specific methods (least-privilege automation) — README "API Token Management"; `src/http_auth.cpp/.h`, `src/http_auth_policy.cpp`, `src/http_auth_request_policy.cpp/.h`.
- Session-based auth with password-manager support and "remember me" — README "Session-Based Authentication"; `src/http_pairing_policy.cpp/.h`.
- Update notifications for new features/fixes — README "Update Notifications"; `src/update.cpp/.h`.
- Session history (connects/durations) persisted and queryable — `src/session_history*.cpp/.h` (writer, storage, sampler, policy, diagnostics — 7 files).
- Host stats service — `src/host_stats.cpp/.h`, `src/host_stats_service.cpp/.h`, `src/host_stats_types.h`.

### 1.7 Windows virtual-gamepad driver (own, MIT-licensed companion repo)
See §2 (deep dive) — this is the single largest controller-relevant feature and is covered in full below.

---

## 2. DEEP DIVE — Controller support

### 2.1 Backend architecture comparison

| | Jochona Host (Windows) | Vibepollo (Windows) |
|---|---|---|
| Virtual-gamepad backend | **ViGEmBus only** (third-party kernel driver, user-installed) | **ViGEmBus preferred, falls back to Vibepollo's own `libvirtualgamepad` (VHF) driver** |
| Controller types emulated | Xbox 360, DualShock 4 (`host/README.md` gamepad-emulation table: DS5 Windows = ❌) | Xbox Series, Xbox One, DualShock 4, **DualSense (DS5)**, Switch Pro |
| DualSense features on Windows | None (DS5 not emulatable) | Touchpad, motion (accel/gyro), battery, lightbar, **adaptive triggers**, rumble, player LEDs, microphone LED |
| Driver install model | Separate ViGEmBus MSI the user must install (`vigembus_not_installed_desc` in `host/src_assets/common/assets/web/public/assets/locale/en.json:454-459`); Jochona ships install/update prompts for it in the web UI | UMDF2 user-mode driver, no kernel bus driver, bundled/offered in the Vibepollo Windows installer (opt-in CMake flag) |

### 2.2 Jochona Host's existing ViGEm backend — exact files/functions
- `host/src/platform/windows/input.cpp` — includes `<ViGEm/Client.h>` (line 25); `client_t`/`target_t` RAII wrappers (lines 50–57); `gamepad_context_t` struct holding ViGEm target + feedback queue + last rumble/RGB state (lines 108–127); `vigem_t::init()` probes ViGEm at startup (lines 265–280); `vigem_t::alloc_gamepad_internal()` creates an X360 or DS4 target, seeds DS4 motion defaults, and registers notification callbacks `x360_notify`/`ds4_notify` (lines 289–343); `vigem_t::free_target()` detaches and disconnects when the last gamepad is freed (lines 356–386); `vigem_t::rumble()` / `set_rgb_led()` push feedback back through `feedback_queue` (lines 394–439); `alloc_gamepad()` (line 1240) implements the **DS4-vs-X360 auto-selection policy**: explicit `config::input.gamepad` override (`x360`/`ds4`), else client-reported type (`LI_CTYPE_PS`/`LI_CTYPE_XBOX`), else `config::input.motion_as_ds4` / `config::input.touchpad_as_ds4` heuristics, else X360 default (lines 1249–1270), with warnings when a DS4-only capability (motion/touchpad/RGB LED) can't be represented on X360 (lines 1272–1289).
- `ds4_update_motion()` (line 198) converts client accel (m/s²) / gyro (°/s) into ViGEm's DS4 fixed-point scale, with calibration-inverse constants matching ViGEmBus's own Ds4Pdo calibration (lines 208–227; comment cites `https://github.com/ViGEm/ViGEmBus/.../Ds4Pdo.cpp#L153-L164`).
- `host/src/input.cpp` — per-session `gamepad_t` struct (one per controller slot, lines 179–206) holding `platf::gamepad_state_t`, the global slot id, and HOME/BACK emulation state; `alloc_id`/`free_id` manage a `std::bitset<MAX_GAMEPADS>` of global slots (lines 78–98); `passthrough(..., PSS_CONTROLLER_ARRIVAL_PACKET)` (line 1054) and the legacy `PNV_MULTI_CONTROLLER_PACKET` path (line 1319) both implement **hot-plug**: allocate a global id, call `platf::alloc_gamepad(...)`, free the id on failure; `free_gamepad()` on disconnect (line 169); `passthrough(..., PSS_CONTROLLER_TOUCH_PACKET)` (line 1223) and `..._MOTION_PACKET` (line 1256) and `..._BATTERY_PACKET` (line 1288) forward touchpad/motion/battery events to the platform backend, each gated by `config::input.controller`.
- `host/src/stream.cpp` — `control_adaptive_triggers_t` wire struct (lines 243–259) and `IDX_SET_ADAPTIVE_TRIGGERS` (line 52): **the Apollo-compatible adaptive-trigger control message already exists at the protocol level** (`gamepad_feedback_e::set_adaptive_triggers` branch, lines 1038–1051) — it is just never populated on Windows because ViGEm's DS4 target has no adaptive-trigger concept.
- `host/src/platform/common.h` — `gamepad_feedback_msg_t::make_adaptive_triggers()` (line 202) and the `adaptive_triggers` union member (lines 241–243) are shared, platform-neutral plumbing already present in Jochona Host.
- Global config only, no per-client/per-app override: `host/src/config.h:281-282` (`touchpad_as_ds4`, `ds5_inputtino_randomize_mac` — the latter is Linux-only inputtino config, dead on Windows), `host/src/config.cpp:853-854,1796-1797`. Web UI: `host/src_assets/common/assets/web/configs/tabs/Inputs.vue:25-48` (gamepad-type `<select>` offers only `ds4`/`x360` under the `#windows` template, lines 42–45) plus `motion_as_ds4`/`ds4_back_as_touchpad_click` checkboxes (lines 66–89). No per-paired-client or per-app gamepad-type override exists anywhere in `host/src` or `host/src_assets` (grep for `per_client|client_gamepad|steam_input|hidhide|player.?slot` across `host/src;host/src_assets` returned **no matches**).
- Linux side (not Jochona's runtime target, FYI): `host/src/platform/linux/input/inputtino_gamepad.cpp` already emulates DS5 (lines 65–121) via the `inputtino` library — so the Sunshine codebase Jochona forked from already has DS5 logic on Linux; it was simply never built for the Windows ViGEm path, because ViGEmBus itself has no DualSense target type.

### 2.3 Vibepollo's VHF backend — exact files/functions
- `src/platform/windows/vhf_gamepad.h` (raw: https://raw.githubusercontent.com/Nonary/Vibepollo/master/src/platform/windows/vhf_gamepad.h) — `vhf_profile_e` enum: `automatic, xbox_series, xbox_one, dualshock4, dualsense, switch_pro`; `vhf_gamepad_t` class: `probe()`, `available()`, `alloc(id, feedback_queue, desired_profile)`, `slot_has_sensors(nr)`, `free(nr)`, `update(nr, state)`, `touch(nr, event)`, `motion(nr, event)`, `battery(nr, event)` — a drop-in parallel API to Jochona's existing `vigem_t`.
- `src/platform/windows/vhf_gamepad_policy.h` — pure, platform-header-free translation layer (testable in isolation): `select_automatic_backend(vigem_available, vhf_available)` (ViGEm preferred, VHF fallback, else unavailable); `normalized_state_t`; `trigger_effect_t` (adaptive-trigger program: mode + 10-byte parameter block, matching `host/src/stream.cpp`'s `DS_EFFECT_PAYLOAD_SIZE`); `rumble_rgb_t` decoded feedback (rumble, RGB, trigger rumble, and adaptive-trigger effects, each independently "changed" flagged to avoid redundant wire traffic); `select_automatic_profile()`; `make_input_state()`; `decode_rumble_rgb()`; touch/motion/battery protocol converters.
- `src/platform/windows/vhf_gamepad.cpp` (raw: https://raw.githubusercontent.com/Nonary/Vibepollo/master/src/platform/windows/vhf_gamepad.cpp) — `static_assert`s pin Vibepollo's button-flag values to `lvg::button_mask::*` (lines ~44–66) so a protocol drift fails the build instead of silently remapping buttons; `select_profile()` honors an explicit profile request or refuses (never silently substitutes, lines ~122–165); `is_playstation()`/`has_motion()` helpers; `impl_t` holds a `std::shared_mutex lifetime`, `lvg::client client`, `std::array<slot_t, MAX_GAMEPADS> slots`, and a dedicated **feedback poll thread** (`k_feedback_poll_interval = 8ms`, chosen to stay under one frame at 120 FPS) that calls `raise_feedback()` to diff-and-forward rumble / RGB / adaptive-trigger / trigger-rumble events back to the client's `feedback_queue` (lines ~230–300), reusing the exact same `gamepad_feedback_msg_t::make_rumble/make_rgb_led/make_adaptive_triggers/make_rumble_triggers` calls Jochona's ViGEm path already uses.
- Companion repo https://github.com/Nonary/libvirtualgamepad (MIT License) — the actual UMDF2/VHF driver + client protocol library. Its own README states the profile contract precisely: **Xbox Series implemented** (native report shape + hardware ID for the inbox `xinputhid.sys` filter); **Xbox One implemented**; **Xbox 360 explicitly unreachable from VHF** (needs a real XUSB bus child VHF cannot create — this is *why* Vibepollo keeps ViGEm as the preferred backend rather than replacing it outright); **DualShock 4 implemented** (report shape, touchpad, motion, battery, lightbar, calibration/pairing/firmware features); **DualSense implemented** (DS4 feature set **plus adaptive triggers, player LEDs, microphone LED**); **Switch Pro implemented**.
- Packaging: `cmake/packaging/windows_virtual_gamepad_contract.cmake` (raw fetched) pins a specific immutable GitHub release (`v0.1.0-beta.6`, SHA-256-verified archive) of `Nonary/libvirtualgamepad`, bundled into the installer only when `SUNSHINE_BUNDLE_VHF_GAMEPAD_DRIVER=ON` (default OFF). The driver DLL is deliberately never Authenticode-signed (the signed catalog hashes it instead); the catalog + setup tool are signed as part of Vibepollo's own MSI SignPath request. `packaging/windows/wix/ask_remove_gamepad.vbs` handles uninstall-time driver removal prompting.
- Hot-plug / player-slot handling: identical shape to Jochona — per-slot `slot_t` (active flag, `client_relative_index`, `lvg::profile`, per-pointer touch-contact map, feedback queue) indexed by the same `gamepad_id_t{globalIndex, clientRelativeIndex}` scheme already used in `host/src/input.cpp`'s `gamepad_t`/`alloc_id`/`free_id`. No architectural change to Jochona's hot-plug/slot code would be needed to add a VHF backend alongside ViGEm — it is a second implementation of the same `platf::alloc_gamepad/free_gamepad/gamepad_update/gamepad_touch/gamepad_motion/gamepad_battery` surface Jochona's `host/src/platform/common.h` already defines.
- Steam Input interaction: **no explicit Steam-Input-hiding/HidHide code found** in Vibepollo's Windows controller path (no `hidhide`/`steam_input` hits in the Windows-relevant tree). Vibepollo's Steam-related work (`steam_integration.*`, `steam_big_picture_policy.h`, `confighttp_steam.cpp`) is about **library discovery and launch/session cleanup**, not controller routing; the only Steam+controller crossover found is Linux-only (Proton DualSense env-var injection, `docs/linux/playstation-controllers.md`). **[UNVERIFIED]** whether any undiscovered Windows-specific Steam Input interaction exists elsewhere in the tree — not found via the searches performed.
- Per-client gamepad settings: Vibepollo's gamepad-type selection is the same **global** `config::input.gamepad` + auto-detection heuristic model as upstream Sunshine/Jochona (`select_automatic_backend`/`select_profile` operate per-connection-request, not per-paired-client persisted preference). **No per-paired-client stored gamepad-type override was found** in the portions of `src/config*.cpp/h` inspected. **[UNVERIFIED]** for the full `docs/configuration.md` (4946 lines; only the first 300 were read) — a per-app or per-client override may exist further in that doc or in `src_assets/common/assets/web` and was not exhaustively ruled out.

### 2.4 Client-side (Jochona Client, SDL2) — already covered, zero porting needed
`client/app/streaming/input/gamepad.cpp` already implements **every DualSense capability** the host side needs to drive:
- Capability advertisement to the host: `LI_CCAP_TOUCHPAD`/`LI_CCAP_DUAL_TOUCHPAD` (`SDL_GameControllerGetNumTouchpads`, lines 876–880), `LI_CCAP_ACCEL`/`LI_CCAP_GYRO` (`SDL_GameControllerHasSensor`, lines 882–887), `LI_CCAP_BATTERY_STATE` (line 888–889), `LI_CCAP_RGB_LED` (`SDL_GameControllerHasLED`, line 891–892), `LI_CCAP_RUMBLE`/`LI_CCAP_TRIGGER_RUMBLE` (lines 870–874), `LI_CTYPE_PS` type detection (line ~896+).
- Receiving feedback from host: `rumble()` (line 1044, `SDL_GameControllerRumble`), `rumbleTriggers()` (line 1102, `SDL_GameControllerRumbleTriggers`), `setMotionEventState()` (line 1116, enables/disables accel/gyro sensor streaming per host request), `setControllerLED()` (line 1144, `SDL_GameControllerSetLED`), and critically **`setAdaptiveTriggers(controllerNumber, DualSenseOutputReport*)`** (line ~1158) which calls `SDL_GameControllerSendEffect()` when `SDL_GameControllerGetType(...) == SDL_CONTROLLER_TYPE_PS5` — i.e., **the client already round-trips Jochona Host's existing `control_adaptive_triggers_t` protocol message end-to-end for any physical DualSense plugged into the Bazzite client**, it is purely the *host's* Windows DS5-emulation gap that prevents adaptive triggers from reaching games.
- Battery reporting (`SDL_JoystickCurrentPowerLevel` → `LiSendControllerBatteryEvent`, lines 272–309) is likewise already implemented.
- **Conclusion: the client needs no changes to consume a Windows DS5/VHF backend on the host.** The entire controller gap is contained to `host/src/platform/windows/input.cpp` (and a thin slice of `host/src/input.cpp`'s `alloc_gamepad` type-selection policy, plus `host/src_assets/.../Inputs.vue` web UI and locale strings for a `ds5` option on Windows).

### 2.5 Alternative to porting Vibepollo's own driver: LizardByte/libvirtualhid
https://github.com/LizardByte/libvirtualhid (72★, actively developed, "Other" license) is LizardByte's own cross-platform successor covering the same ground: Windows UMDF2/VHF gamepads (Xbox 360 via a broker-owned XUSB personality + Xbox One/Series/DS4/DualSense/Switch Pro via VHF), Linux `uhid`/`uinput`, macOS via a signed broker, plus keyboard/mouse/touch/pen. Its own comparison table (README) explicitly lists itself against ViGEmBus, HIDMaestro, inputtino, and WinUHid. **Critical catch: "A license is required for Windows driver-backed devices and macOS virtual gamepads... Yearly and lifetime options available"** — i.e., it is **not freely redistributable** for the Windows gamepad path the way `Nonary/libvirtualgamepad` (MIT) is. Since Jochona Host is GPL-3.0 and aims to stay freely distributable, `libvirtualhid`'s commercial licensing term makes it a **worse fit than directly adopting/vendoring `Nonary/libvirtualgamepad`** (MIT) unless Jochona is willing to pay for a libvirtualhid license, in which case its cross-platform scope (keyboard/mouse/touch too) would be attractive longer-term. **[UNVERIFIED]**: exact libvirtualhid pricing/terms beyond the README badge; not required for this comparison's scope.

### 2.6 Haptics-PCM forwarding (DualSense waveform audio)
Vibepollo's `docs/linux/playstation-controllers.md` describes a **non-standard Moonlight-protocol extension** (`LI_CCAP_HAPTICS_PCM = 0x8000`, SDP `ML_FF_HAPTICS_PCM`, control type `0x5601` on ENet channel `0x08`) requiring **a coordinated/patched Moonlight client fork** to forward raw DualSense actuator PCM samples instead of RMS-reduced rumble. Jochona Client is a stock Moonlight-qt fork and does **not** implement this capability bit or control message. This is the one controller feature that would require real client protocol work (new ENet control message handling + SDL audio-channel plumbing) rather than being "already covered" like §2.4. It is niche (only benefits specific titles like 007 First Light with native `libScePad.dll` haptics) and high-effort; **not recommended for near-term porting** (see ranking).

---

## 3. What Jochona Host already has vs. what's missing

### 3.1 Already present (verified in `host/src`)
- Full ViGEm-based Xbox 360 / DualShock 4 emulation incl. motion, touchpad-as-DS4, RGB LED, rumble, battery passthrough, hot-plug, multi-slot — `host/src/platform/windows/input.cpp`, `host/src/input.cpp`.
- The **Apollo-compatible adaptive-trigger wire protocol** already exists end-to-end at the Sunshine/Jochona protocol layer (`host/src/stream.cpp:243-259,1038-1051`, `host/src/platform/common.h:191-244`) — it is simply never populated because no Windows backend currently emulates a device with adaptive triggers.
- DS5 (DualSense) emulation already exists for **Linux** via `inputtino` (`host/src/platform/linux/input/inputtino_gamepad.cpp`), confirming the protocol/data-model side of DS5 support is already proven elsewhere in the same codebase — only the Windows backend is missing it.
- Jochona's own extensions (`host/src/jochona/*`: capability manifest, encoder tuples, display-adapter client/ABI, host volume) are unrelated to controllers and orthogonal to this comparison.
- Jochona Client already fully round-trips every DualSense feature SDL2 exposes, including adaptive triggers (§2.4).

### 3.2 Missing (confirmed by grep across `host/src;host/src_assets`, zero matches for `per_client|client_gamepad|steam_input|hidhide|player.?slot`, and by the README gamepad-emulation table showing DS5/Windows = ❌)
- **Windows DualSense (DS5) emulation** — no backend; ViGEmBus has no DS5 target type.
- **Windows adaptive-trigger population** — protocol exists, nothing drives it on Windows.
- A **second/fallback virtual-gamepad backend** when ViGEmBus is absent/outdated (today Jochona's only degrade path is "show an install/update prompt", `host/src_assets/.../en.json:454-459`; Vibepollo falls back to VHF automatically).
- **Per-client or per-app gamepad-type override** — neither codebase appears to have this; config is global in both Jochona and (as far as verified) Vibepollo.
- **Steam-Input-hiding (HidHide-style) controller routing** — not found in either codebase for Windows.
- Non-controller Vibepollo areas entirely absent from Jochona Host: Playnite/Lutris/Steam launcher integration, RTSS/NVCP/Lossless-Scaling/Smooth-Motion frame-pacing automation, WebRTC/PyroWave alternate transports, scoped API tokens, session history/host-stats services, update notifications, and the Windows service-mode WGC capture + display-automation safeguards (Jochona's display/VDD work under `host/src/jochona/display_adapter_*` is a different, independently developed subsystem, not a port of Vibepollo's).

---

## 4. Per-feature port recommendations

License note: Vibepollo itself is GPL-3.0 (same as Jochona Host), so **directly porting Vibepollo's own `.cpp`/`.h` source is license-clean** (same license, carry forward copyright/attribution per GPL-3.0 §5). The companion `Nonary/libvirtualgamepad` driver is **MIT-licensed** — also clean to vendor/link from a GPL-3.0 host. `LizardByte/libvirtualhid` requires a **paid commercial license** for the Windows gamepad path — treat as a cost decision, not a pure license-compatibility blocker.

| # | Feature | Effort | License | Risk | Host files to touch | Client files to touch |
|---|---|---|---|---|---|---|
| 1 | **Windows DualSense (DS5) emulation via VHF backend** | **Large** (new driver dependency, installer/signing pipeline, new backend class, policy wiring) | MIT (`Nonary/libvirtualgamepad`) *or* paid (`LizardByte/libvirtualhid`) — recommend MIT | Medium: new kernel-adjacent (UMDF2) driver install path, signing/catalog trust, uninstall flow; mitigated by ViGEm-preferred fallback pattern Vibepollo already proved | New `host/src/platform/windows/vhf_gamepad.{cpp,h}` + `vhf_gamepad_policy.{cpp,h}` (port/adapt from Vibepollo, same license); extend `host/src/platform/windows/input.cpp`'s `alloc_gamepad()` to try VHF when ViGEm is unavailable or `ds5` is requested; add `ds5` option to Windows branch of `host/src_assets/common/assets/web/configs/tabs/Inputs.vue:42-45` and `en.json` (+ other locales) `gamepad_ds5*` strings (already present for Linux, just needs enabling for `#windows`); new `cmake/` contract file mirroring `windows_virtual_gamepad_contract.cmake`; installer hook akin to `ask_remove_gamepad.vbs` | **None** — `client/app/streaming/input/gamepad.cpp` already supports DualSense end-to-end (§2.4) |
| 2 | **Populate adaptive-trigger messages on Windows** | **Small**, *contingent on #1* | GPL-3.0 (host protocol code already Jochona's own) | Low — protocol path is already proven (used by Linux inputtino) | `host/src/platform/windows/input.cpp` (DS5 branch calling `gamepad_feedback_msg_t::make_adaptive_triggers`, mirroring `host/src/stream.cpp:1038-1051`) | None — `setAdaptiveTriggers()` already implemented |
| 3 | **ViGEm-unavailable graceful fallback (VHF or clearer degrade path)** | Medium, *shares work with #1* | MIT | Low | `host/src/platform/windows/input.cpp` (`alloc_gamepad`/backend selection, mirroring Vibepollo's `select_automatic_backend`) | None |
| 4 | **RTSS + NVIDIA Control Panel frame-pacing integration** | Medium | GPL-3.0 (direct port candidate) | Medium — touches third-party RTSS API/process injection, Windows-only | New `host/src/rtss_*` (none exist today; port `src/confighttp_rtss.cpp`, `src/app_framegen_config.*`, `src/framegen_policy.h`), config.cpp/h additions, Inputs/AudioVideo.vue-equivalent UI tab | None |
| 5 | **Playnite integration** | Large | GPL-3.0 | Medium — large new subsystem (process launch, artwork, sync) | New `host/src/*playnite*` (port `src/config_playnite*.cpp/.h`, `src/confighttp_playnite.cpp`), new web UI tab, `apps.json` schema extension | None |
| 6 | **Scoped API tokens** | Medium | GPL-3.0 | Low–Medium (auth-surface change, needs careful review) | `host/src/httpcommon.cpp/.h`, new `http_auth*` files (port `src/http_auth.cpp/.h`, `src/http_auth_policy.cpp`), confighttp.cpp token-management endpoints, web UI | None |
| 7 | **Session-based auth w/ "remember me"** | Medium | GPL-3.0 | Medium — security-sensitive, needs careful review per project rules (never weaken auth) | `host/src/httpcommon.cpp`, new `http_pairing_policy.cpp/.h` (port), web UI login flow | None |
| 8 | **Update notifications** | Small | GPL-3.0 | Low | New `host/src/update.cpp/.h` (port), system_tray.cpp hook, web UI banner | None |
| 9 | **WGC service-mode capture + display-automation safeguards** | Large | GPL-3.0 | High — deep interaction with Jochona's own `display_adapter_*`/VDA lease subsystem (`host/src/jochona/display_adapter_*`), real risk of conflicting with Jochona's independently-built display work; needs design reconciliation, not a drop-in port | `host/src/platform/windows/display_*.cpp`, `host/src/jochona/display_adapter_client.cpp` (conflict-prone) | None |
| 10 | **WebRTC / PyroWave alternate transports** | Very large | GPL-3.0 (WebRTC dep itself is BSD-ish via libwebrtc, PyroWave is Vibepollo's own) | High — new transport stack, cross-cuts `host/src/stream.cpp`/`rtsp.cpp` and client network code | `host/src/webrtc_stream.cpp/.h` (port), `rtsp.cpp`, `stream.cpp` | `client/app/streaming/*` network/session layer — substantial new work |

(Haptics-PCM forwarding, §2.6, and the full Steam/Lutris/HidHide surface were evaluated but excluded from the top-10 ranking below as niche/high-effort/low-value for Jochona's two-box Windows-Host + Bazzite-Client deployment.)

---

## 5. Ranked top 10 (controller support first)

1. **Windows DualSense (DS5) virtual-gamepad backend** (vendor/port `Nonary/libvirtualgamepad` MIT driver + a new `vhf_gamepad.{cpp,h}`/`vhf_gamepad_policy.{cpp,h}` pair in `host/src/platform/windows/`, wired into `alloc_gamepad()` in `host/src/platform/windows/input.cpp`). Client needs **zero changes**. Biggest capability gap, cleanest license story, and the client is already proven to consume every feature it unlocks.
2. **Populate adaptive triggers on Windows** once #1 lands — the protocol (`host/src/stream.cpp:243-259,1038-1051`) and client (`setAdaptiveTriggers`) are already done; this is wiring, not new design.
3. **ViGEm-unavailable automatic fallback to the new VHF backend** — directly reuses #1's new code; removes the current "go install ViGEmBus yourself" dead-end (`en.json:454-459`).
4. **DS5 option surfaced in the Windows Web UI** (`Inputs.vue` + locale strings already have the Linux `ds5` i18n keys; just needs the `#windows` template branch and `gamepad_ds5_manual` options enabled) — small, high user-visibility payoff once #1 exists.
5. **RTSS + NVIDIA Control Panel frame-pacing automation** — highest non-controller user-visible win (smoother frame pacing is a common pain point for living-room streaming setups); GPL-3.0 direct-port candidate.
6. **Scoped API tokens** — security/ergonomics win for anyone automating Jochona Host (e.g., Beacon or future tooling), moderate effort, GPL-3.0 port.
7. **Update notifications** — small, low-risk, improves operability for a project with "no stable releases" (per the verified facts already on record for this repo).
8. **Session-based auth w/ "remember me"** — worthwhile but security-sensitive; should get extra review before shipping.
9. **Playnite integration** — valuable for a living-room/Bazzite-Client use case but large scope; good candidate for a dedicated follow-up project rather than an incremental port.
10. **WGC service-mode capture + display automation** — highest potential value but highest risk of colliding with Jochona's own already-built `display_adapter_*`/VDA lease subsystem; needs a design reconciliation pass before any porting, not a blind port.

---

## Appendix: items I could not fully verify
- Full contents of Vibepollo's `docs/configuration.md` (4,946 lines; only lines 1–300 read) — a per-client/per-app gamepad override could theoretically exist later in that document; my grep of the file tree and the code files I did read found none.
- Exact current pricing/terms of a `LizardByte/libvirtualhid` Windows-gamepad license.
- Whether any Windows-specific Steam Input hiding/HidHide code exists somewhere in Vibepollo's tree outside the paths searched (`src/steam_*`, `docs/linux/*`) — none found, but the 12,875-line file tree was not exhaustively read file-by-file.
- `libvirtualgamepad`'s exact battery/microphone-LED wire details beyond what its README states (driver source itself, e.g. `driver/src/dualsense.cpp`, was not read).