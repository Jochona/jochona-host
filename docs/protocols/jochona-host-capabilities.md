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
      { "id": "living-room-4k120", "width": 3840, "height": 2160,
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

An Encoder Tuple is advertised only after the exact path has encoded probe frames successfully. Static GPU-model assumptions and vendor capability bits alone are insufficient. Cached proof is invalidated by GPU, driver, display-mode, virtual-display-adapter, or Host-build change.

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

Jochona Client shows the failure and alternatives before relaunch. A pinned codec is never changed without consent.

## Virtual-display lifecycle

The administrator defines a named persistent Virtual Display Pool. A Session acquires an available member, applies its requested mode/HDR state, and restores/releases it on disconnect. Reconnect retains the same pool member when healthy. Jochona Host works with physical capture when the signed adapter is absent; capability fields report adapter installation and health separately from pool capacity.

## Capacity

The first release supports one active Session per Host. `ready` permits launch; `busy` includes the active Host Application and rejects a competing launch with `host_busy`. Future capacity may increase without changing this state contract.

## Runtime bitrate

Runtime bitrate is a post-1.0 optional control. When implemented, the manifest advertises range/step and the authenticated setter returns the applied value. Absence means unavailable; the client never emulates Runtime ABR by reconnecting.
