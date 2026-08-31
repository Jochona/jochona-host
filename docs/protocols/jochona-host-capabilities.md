# Jochona Host capability and control manifest

This contract extends, but never replaces, baseline GameStream. Every endpoint requires the client certificate already pinned during GameStream pairing. Unknown fields are ignored; unknown major schema versions disable Jochona extensions while baseline streaming remains available.

This document is authoritative for Jochona Host and the Client's Host adapter.

## Manifest

`GET /jochona/v1/capabilities`

```json
{
  "schema": { "major": 1, "minor": 0 },
  "host": {
    "software": "Jochona Host",
    "build": "0.1.0",
    "platform": "windows",
    "identity": "host-unique-id",
    "capacity": {
      "state": "ready",
      "maxSessions": 1,
      "activeApplication": null
    }
  },
  "permissions": [
    "session.launch",
    "session.stop",
    "host.volume.read",
    "host.volume.write"
  ],
  "encoderTuples": [
    {
      "id": "nvenc-av1-main10-420-3840x2160-120-hdr",
      "codec": "av1",
      "profile": "main10",
      "bitDepth": 10,
      "chroma": "420",
      "width": 3840,
      "height": 2160,
      "fps": 120,
      "hdr": {
        "supported": true,
        "metadata": ["mastering-display", "max-cll", "max-fall"]
      },
      "capture": ["physical", "virtual"],
      "proof": {
        "method": "vendor-query+probe-frames",
        "gpu": "pci-vendor-device-id",
        "driver": "driver-version",
        "displayMode": "3840x2160@120-hdr",
        "virtualDisplayAdapter": "1.0",
        "hostBuild": "0.1.0",
        "verifiedAt": "2026-08-27T00:00:00Z"
      }
    }
  ],
  "virtualDisplay": {
    "adapter": "jochona-windows-display",
    "version": "1.0.0",
    "installed": true,
    "healthy": true,
    "pool": [
      { "id": "default", "width": 3840, "height": 2160,
        "fps": 120, "hdr": true, "state": "available" }
    ]
  },
  "runtimeControls": {
    "bitrate": {
      "available": false,
      "minKbps": null,
      "maxKbps": null,
      "stepKbps": null
    },
    "hostVolume": { "available": true, "min": 0, "max": 100 }
  },
  "audio": {
    "channelLayouts": ["stereo", "5.1", "7.1"],
    "microphoneInput": false
  }
}
```

An Encoder Tuple is advertised only after the exact path has encoded probe frames successfully. Static GPU-model assumptions and vendor capability bits alone are insufficient. Cached proof is invalidated by GPU, driver, display-mode, virtual-display-adapter, or Host-build change. `proof.gpu` is a stable PCI identity (`vendor:device:subsystem:revision` in lowercase hex) when the Host can resolve the encoding adapter, never a fabricated placeholder.

### Bootstrap proof

`POST /jochona/v1/probe?codec=<h264|hevc|av1>&profile=<main8|main10>&chroma=<420|444>&width=<uint>&height=<uint>&fps=<uint>&hdr=<0|1>&capture=<physical|virtual>`

A fresh Host has no proven Encoder Tuples until a session has actually launched once, but Jochona Client refuses to launch without a matching advertised tuple. This endpoint breaks that deadlock: it runs the exact same proof sequence a real `/launch` or `/resume` call uses for the requested combination (display reconfiguration, encoder re-selection, and encoding real probe frames), and reverts any display or virtual-display-lease state it changed before returning, since no session follows a probe. A successful probe is recorded exactly like a successful launch would be and appears in the next `GET /jochona/v1/capabilities` fetch.

- `200`: exactly one `encoderTuples[]` entry -- the tuple this call just proved and cached.
- `409`: the same `encoder_tuple_unavailable` or `host_busy` shape `/launch` returns (see "Session request" below); `host_busy` when an app or session is already active, since probing reconfigures live display/capture state.
- `400`: `{"error": "invalid_parameter", "detail": "..."}` for a missing, out-of-enum, or non-numeric parameter.

Never a bulk scan: exactly the one combination requested is proved.

## Session request

The client selects one advertised `encoderTuples[].id` after resolving Effective Settings within Host Policy and sends the exact tuple ID with the baseline launch request. Jochona Host either accepts that tuple or returns a structured error; it never silently changes codec/profile/chroma/bit depth.

```json
{
  "error": "encoder_tuple_unavailable",
  "requested": "nvenc-av1-main10-420-3840x2160-120-hdr",
  "stage": "encoder_initialize",
  "detail": "NVENC rejected the capture format for this display mode.",
  "alternatives": [
    "nvenc-hevc-main10-420-3840x2160-120-hdr",
    "nvenc-av1-main8-420-3840x2160-120-sdr"
  ]
}
```

Jochona Client shows the failure and alternatives before relaunch. A pinned codec is never changed without consent. Host enforces this at the RTSP layer too: once a `jochonaTuple` id is accepted, the subsequent RTSP `ANNOUNCE` must request exactly that tuple's codec, dynamic range, and chroma sampling, or Host rejects it -- a client is not obligated to actually request the tuple it pinned, so Host never relies on it doing so.

## Virtual-display lifecycle

Protocol v1.0 exposes exactly one stable Virtual Display Pool slot per Host, reported as pool member `"default"`. A Session leases it, applies its requested mode/HDR state, and restores/releases it on disconnect. Reconnect retains the lease when healthy. Jochona Host works with physical capture when the signed adapter is absent; capability fields report adapter installation and health separately from pool capacity. A future protocol version MAY raise the pool to multiple, administrator-named members without changing this per-member schema.

## Host volume

`GET /jochona/v1/volume` and `PUT /jochona/v1/volume?level=<0-100>`, gated by `host.volume.read`/`host.volume.write`.

```json
{ "available": true, "min": 0, "max": 100, "current": 42 }
```

`available: false` reports `min`/`max`/`current` as `null` rather than a fabricated range. `PUT` requires `level` in `[0, 100]`, applies it, and returns the same shape with the value Host actually read back after applying it (never the client-requested value verbatim). Failure responses:

- `400`: `{"error": "invalid_parameter", "detail": "..."}` -- `level` missing, non-numeric, or outside `[0, 100]`.
- `409`: `{"error": "host_volume_unavailable", "detail": "..."}` -- unsupported platform, or the platform call failed.

## Capacity

The first release supports one active Session per Host. `ready` permits launch; `busy` includes the active Host Application and rejects a competing launch with `host_busy`. Future capacity may increase without changing this state contract.

## Runtime bitrate

Runtime bitrate is a post-1.0 optional control. When implemented, the manifest advertises range/step and the authenticated setter returns the applied value. Absence means unavailable; the client never emulates Runtime ABR by reconnecting.

## Permissions and observer-only enrollment

`permissions` is the exact grant for the requesting certificate, not a static list. A GameStream client that completes ordinary pairing (no extra parameters) receives the default full-control grant: `session.launch`, `session.stop`, `host.volume.read`, `host.volume.write`.

Beacon instead requests read-only, observer-only enrollment by sending `jochona_permission=observer_only` on the pairing handshake's `getservercert` phase. Host persists that request against the resulting certificate and grants it `host.observe` alone: read-only `/serverinfo` and the capacity/capability fields of `GET /jochona/v1/capabilities`. An observer certificate never derives any Apollo-compatible control bit and is denied `/launch`, `/resume`, `/cancel`, `/applist`, `/appasset`, `GET`/`PUT /jochona/v1/volume`, and `POST /jochona/v1/probe`.

`GET /serverinfo` also carries two markers so Beacon can classify a Host without first pairing:

- `<jochona_family>1</jochona_family>`: always present (HTTP or HTTPS, paired or not). Identifies this Host as Jochona family.
- `<jochona_permission>observer_only|full_control</jochona_permission>`: present only on an authenticated HTTPS request from a recognized paired certificate; reports that certificate's actual grant.
