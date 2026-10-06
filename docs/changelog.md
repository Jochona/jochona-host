# Changelog

## Jochona Host — 1.0.0

Jochona Host's own changes, tracked separately from upstream Sunshine's release history embedded below.

- **Capability manifest**: authenticated `GET /jochona/v1/capabilities` reports host identity, capacity, the caller's actual permission grant, proven Encoder Tuples, Virtual Display Adapter status and pool, runtime bitrate (unavailable, post-1.0), and Host Volume.
- **Exact Encoder Tuple proof and preflight**: an Encoder Tuple is advertised only after the exact codec/profile/chroma/resolution/fps/HDR combination has encoded real probe frames successfully; proof is invalidated on any GPU, driver, display-mode, virtual-display-adapter, or Host-build change. `POST /jochona/v1/probe` proves one exact combination outside of a live session so a fresh Host/Jochona Client pairing can complete its first launch without an existing proven tuple.
- **Requested-tuple enforcement**: `/launch` and `/resume` accept an optional `jochonaTuple` id and either honor it exactly or return a structured `encoder_tuple_unavailable`/`host_busy` rejection with verified alternatives; the pinned codec/dynamic-range/chroma is also enforced against the client's subsequent RTSP `ANNOUNCE`, not just accepted at launch time.
- **Observer-only enrollment**: Beacon-style read-only pairing (`jochona_permission=observer_only`) persists a restricted grant per certificate, exposed via `host.observe` in the manifest and `<jochona_family>`/`<jochona_permission>` markers on `/serverinfo`; observer certificates are denied `/launch`, `/resume`, `/cancel`, `/applist`, `/appasset`, Host Volume, and Encoder Tuple preflight.
- **Virtual Display Adapter lease lifecycle**: `/launch?virtualDisplay=1` leases and configures the signed Display Adapter's default pool slot for the session's requested mode/HDR, releasing it on disconnect, app exit, or explicit cancel; adapter installation and health are reported independently of pool capacity so physical capture keeps working when the adapter is absent.
- **Host Volume**: `GET`/`PUT /jochona/v1/volume` reads and sets the Host's output volume on platforms with a real endpoint, honestly reporting unavailable elsewhere rather than fabricating a range.
- **Structured failures**: every Jochona-specific rejection (busy, unavailable tuple, invalid parameter, unavailable Host Volume) is a JSON body with a stable `error` code and human-readable `detail`, never a silent fallback or a bare baseline GameStream status code.
- **Sensitive request logging**: GameStream session keys, pairing material, certificates, stable Client identifiers, cookies, and authorization headers are redacted before request diagnostics reach Host logs.
- **Baseline compatibility**: every Jochona extension is additive and optional; a client that never sends a Jochona-specific parameter gets exactly Sunshine's baseline GameStream behavior.
- **CI fixes for the fork**: `ci.yml`'s push trigger now watches `main` instead of upstream's `master` (this fork's default branch), so push-triggered builds actually run; the LizardByte-gated `release` job and the `localize`/`update-pages` (GH-Pages) workflows remain gated to `LizardByte/` repositories only, since they need Crowdin/`GH_BOT_TOKEN`, GitHub Pages, and Apple/Azure signing secrets this fork doesn't have. See [Building > CI on this fork](building.md#ci-on-this-fork) for details. `_codeql.yml` is centrally managed upstream and still references `master`; left unmodified.
- **Fork-owned release pipeline**: a separate `fork-release` job publishes a real, unsigned GitHub Release on this repository when a `v*` tag is pushed — Windows (AMD64 + ARM64), macOS (arm64 + x86_64), Linux AppImage, and Linux Flatpak (x86_64 + aarch64) artifacts plus a `SHA256SUMS` file. See [Building > Cutting a release](building.md#cutting-a-release).
- **Strict Doxygen build**: the documentation site (`docs/Doxyfile`) builds with zero warnings under the same strict configuration CI enforces — no bare HTML-like tags outside backticks, no dangling `\ref`/`\sa` targets, every Markdown doc wired into `INPUT`.
- **Windows DualSense (DS5) virtual gamepad backend**: `gamepad = ds5` (or `auto`, for a client that reports a PS5 controller) emulates a real DualSense — touchpad, motion, battery, player LEDs, rumble, and adaptive triggers — through the free, MIT-licensed [libvirtualgamepad](https://github.com/Nonary/libvirtualgamepad) VHF driver, ported from [Vibepollo](https://github.com/Nonary/Vibepollo); ViGEmBus remains the default backend and the automatic fallback when the VHF driver is not installed or fails to create a controller. See [Getting Started > DualSense on Windows](getting_started.md#dualsense-on-windows) for driver installation.

@htmlonly
<script type="module" src="https://md-block.verou.me/md-block.js"></script>
<md-block
  hmin="2"
  src="https://raw.githubusercontent.com/LizardByte/Sunshine/changelog/CHANGELOG.md">
</md-block>
@endhtmlonly

<div class="section_buttons">

| Previous                              |                          Next |
|:--------------------------------------|------------------------------:|
| [Getting Started](getting_started.md) | [Docker](../DOCKER_README.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
