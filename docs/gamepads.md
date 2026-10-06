# Gamepads

Jochona Host emulates virtual gamepads on the host so client-side controller input reaches games that don't know
they're being streamed to. This page covers the `gamepad` config key and the two Windows backends; see the
[Feature Compatibility](../README.md#-feature-compatibility) table in the README for the full per-platform matrix.

## The `gamepad` config key

| Value | Behavior |
|---|---|
| `auto` (default) | Picks a backend/profile per client-reported controller type — a PlayStation-reported pad becomes a DualSense where available, otherwise falls back per platform. |
| `x360` | Force Xbox 360. |
| `ds4` | Force DualShock 4 (PS4). |
| `ds5` | Force DualSense (PS5) — Linux via `inputtino`, Windows via the VHF driver (see below). |
| `switch` | Force Nintendo Switch Pro. |

## Linux

DualShock 4, DualSense (DS5), Switch Pro, and Xbox One/360 are all emulated via `inputtino` — this is baseline
upstream Sunshine behavior, unchanged by Jochona. No extra driver install is required.

## Windows: DualSense (DS5) via the VHF driver (experimental), ViGEmBus fallback

Upstream Sunshine's Windows gamepad emulation goes through [ViGEmBus](
https://github.com/nefarius/ViGEmBus), which only exposes Xbox 360/One and DualShock 4 (DS4) target types — it has
no DualSense (DS5) target. Jochona Host can optionally use a second backend, the free, MIT-licensed
[libvirtualgamepad](https://github.com/Nonary/libvirtualgamepad) VHF driver (ported from
[Vibepollo](https://github.com/Nonary/Vibepollo)), which is the **only** Windows backend that can present a real
DualSense: touchpad, motion sensors, battery, player LEDs, rumble, and adaptive triggers.

This backend is **experimental and advanced**. Jochona Host's installer does not bundle, sign, or install the VHF
driver — ViGEmBus is the only backend the installer sets up, and it stays the default and supported path for
Windows gamepad emulation. The VHF driver's own published release packages are intentionally unsigned for
consumer signing, so using it requires following libvirtualgamepad's own signing/trust documentation (today, that
means building and locally test-signing it yourself) before the setup tool can install it.

- `gamepad = ds5`, or `auto` when the client reports a PS5 controller, requests the VHF driver. If the VHF driver
  isn't installed or fails to create a controller, Jochona Host falls back to ViGEmBus automatically (logged as a
  warning) rather than failing the gamepad outright.
- `gamepad = auto`/`x360`/`ds4`/`switch` without a VHF-eligible request use ViGEmBus as before — **ViGEmBus remains
  the default backend** and must still be installed for any Windows gamepad emulation; the VHF driver is additive.
- Windows Server hosts do not support virtual gamepads at all (same restriction as upstream Sunshine).

Install steps for the VHF driver are in [Getting Started > DualSense on Windows](getting_started.md#dualsense-on-windows).

<div class="section_buttons">

| Previous                                 |                            Next |
|:------------------------------------------|---------------------------------:|
| [Jochona Overview](jochona/overview.md) | [App Examples](app_examples.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
