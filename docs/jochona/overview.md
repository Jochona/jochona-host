# What is Jochona Host?

Jochona Host is a fork of [LizardByte/Sunshine](https://github.com/LizardByte/Sunshine) that stays fully
baseline-GameStream compatible — any stock Moonlight, Apollo, or Sunshine client pairs with it and streams exactly
as it would against upstream Sunshine — while adding a set of authenticated, versioned, opt-in extensions that the
Jochona Client speaks natively. Every extension is additive: a client that never sends a Jochona-specific parameter
gets exactly Sunshine's stock behavior.

The authoritative, machine-checkable specification for the extensions below is the
[Jochona Host Capabilities protocol spec](https://github.com/Jochona/jochona-host/blob/main/docs/protocols/jochona-host-capabilities.md);
this page is the plain-language tour.

## Capability manifest

`GET /jochona/v1/capabilities` reports host identity, capacity, the caller's actual permission grant, proven
Encoder Tuples, Virtual Display Adapter status, and Host Volume, all gated behind the same certificate auth as
baseline GameStream. See the [changelog](../changelog.md) for the full field list.

## Encoder Tuples

An Encoder Tuple (codec/profile/chroma/resolution/fps/HDR) is advertised only after that exact combination has
encoded real probe frames successfully — proof is invalidated on any GPU, driver, display-mode,
virtual-display-adapter, or Host-build change. `POST /jochona/v1/probe` proves one combination outside of a live
session, and `/launch`/`/resume` accept an optional `jochonaTuple` id that is either honored exactly or rejected
with a structured error and verified alternatives.

## Virtual display

`/launch?virtualDisplay=1` leases and configures a slot on Jochona Display Adapter — a
separate, MIT-licensed UMDF2/IddCx Windows driver — for the session's requested mode/HDR, releasing it on
disconnect, app exit, or explicit cancel. Adapter installation and health are reported independently of pool
capacity, so physical-display capture keeps working when the adapter is absent or not installed. See
[Jochona Display Adapter](https://github.com/Jochona/jochona-display-adapter) for the driver itself and its own
protocol v1.0 contract.

## Gamepads, including an experimental DualSense backend on Windows

Baseline Sunshine already emulates a DualShock/DualSense-class controller on Linux (via `inputtino`). On Windows,
Jochona Host can optionally use a real DualSense (DS5) backend — touchpad, motion, battery, player LEDs, rumble,
and adaptive triggers — through the free, MIT-licensed [libvirtualgamepad](https://github.com/Nonary/libvirtualgamepad)
VHF driver. This backend is experimental and advanced: it is not bundled, signed, or installed by Jochona Host, and
ViGEmBus remains the default, supported Windows gamepad backend. See [`docs/gamepads.md`](../gamepads.md) for the
full platform/backend matrix, install story, and configuration.

## Host Volume

`GET`/`PUT /jochona/v1/volume` reads and sets the Host's output volume on platforms with a real endpoint (currently
Windows), honestly reporting unavailable elsewhere rather than fabricating a range.

## Observer-only pairing

Beacon-style read-only pairing (`jochona_permission=observer_only`) persists a restricted grant per certificate:
observer certificates can poll `/serverinfo` to learn online/offline state but are denied `/launch`, `/resume`,
`/cancel`, `/applist`, `/appasset`, Host Volume, and Encoder Tuple preflight. [Jochona Beacon](
https://github.com/Jochona/jochona-beacon) uses exactly this grant for its Wake-on-LAN presence polling.

## PyroWave: not yet in Jochona Host

Jochona Client ships an experimental PyroWave (GPU wavelet codec) decoder, but it is **client-side only** and
**macOS/Apple-Silicon-Metal only** as shipped — see the Client's own README. Jochona Host has no PyroWave encoder
today; porting a cross-platform (Vulkan) PyroWave encoder into Host is tracked as post-1.0 work in the
[1.0 plan](https://github.com/Jochona/jochona-constellation/blob/main/docs/plan-1.0.md#post-10-roadmap-ranked).
Selecting PyroWave against a Jochona Host will not work until that lands.

## The rest of the Jochona ecosystem

- [Jochona Client](https://github.com/Jochona/jochona-client) — the controller-first streaming client this Host is
  built for.
- [Jochona Display Adapter](https://github.com/Jochona/jochona-display-adapter) — the virtual-display driver Host
  leases for headless/virtual-display sessions.
- [Jochona Beacon](https://github.com/Jochona/jochona-beacon) — optional Linux LAN daemon for Wake-on-LAN and
  presence, using Host's observer-only pairing.
- [Jochona Constellation](https://github.com/Jochona/jochona-constellation) — the planned, owner-hosted management
  plane; see its [1.0 plan](https://github.com/Jochona/jochona-constellation/blob/main/docs/plan-1.0.md) for scope
  and the [cross-repo getting-started walkthrough](
  https://github.com/Jochona/jochona-constellation#getting-started-windows-host--bazzite-client) for setting up a
  Host + Client pair end to end.

<div class="section_buttons">

| Previous                              |                                   Next |
|:---------------------------------------|----------------------------------------:|
| [Getting Started](../getting_started.md) | [Gamepads](../gamepads.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
