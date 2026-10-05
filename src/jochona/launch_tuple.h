/**
 * @file src/jochona/launch_tuple.h
 * @brief Session-setup glue between a successful encoder probe, the proven
 *        tuple store, the optional `/launch?jochonaTuple=<id>` request, and
 *        the optional `/launch?virtualDisplay=1` Jochona Display Adapter
 *        session lease.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// local includes
#include "encoder_tuples.h"

namespace rtsp_stream {
  struct launch_session_t;
}

namespace jochona::launch {

  /**
   * @brief Structured rejection body for a jochonaTuple-aware `/launch` call.
   *
   * Serializes to exactly the shape described in
   * docs/protocols/jochona-host-capabilities.md's "Session request" section
   * (`error`, `requested`, `stage`, `detail`, `alternatives`), or, for the
   * busy case, the `host_busy` shape (`error`, `detail`, no `requested`/
   * `stage`/`alternatives`).
   */
  struct tuple_rejection_t {
    std::string error;  ///< "encoder_tuple_unavailable" | "host_busy".
    std::string requested;  ///< The requested tuple id; empty for "host_busy".
    std::string stage;  ///< Failure stage; empty for "host_busy".
    std::string detail;  ///< Human-readable explanation.
    std::vector<std::string> alternatives;  ///< Verified alternative tuple ids; empty for "host_busy".
  };

  /**
   * @brief Serialize a tuple_rejection_t to the exact JSON body Jochona
   *        Client expects on HTTP 409.
   *
   * @param rejection Rejection to serialize.
   * @return The HTTP 409 JSON body for `rejection`.
   */
  std::string to_json(const tuple_rejection_t &rejection);

  /**
   * @brief The `host_busy` rejection body for a jochonaTuple-aware launch
   *        attempted while a session is already active.
   *
   * @return The `host_busy` rejection body.
   */
  tuple_rejection_t host_busy_rejection();

  /**
   * @brief Compute the host-wide proof-environment fingerprint for one mode.
   *
   * Reads the configured GPU, best available driver identity, active display
   * mode, Jochona Display Adapter version, and Host build.
   *
   * @param width Probed display width in pixels.
   * @param height Probed display height in pixels.
   * @param fps Probed display refresh rate.
   * @param hdr Whether the active display mode uses HDR.
   * @return The proof-environment fingerprint for the given mode.
   */
  encoder::environment_fingerprint_t current_environment_fingerprint(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t fps,
    bool hdr
  );

  /**
   * @brief Probe and record every enabled tuple for one exact display mode.
   *
   * Each advertised tuple must pass video::probe_encoder_config() at the
   * requested resolution, refresh rate, dynamic range, and chroma mode.
   *
   * @param width Probed display width in pixels.
   * @param height Probed display height in pixels.
   * @param fps Probed display refresh rate.
   * @param stream_hdr Whether the requested stream uses HDR.
   * @param capture_virtual True when capture uses a leased virtual display.
   */
  void record_probe_success(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t fps,
    bool stream_hdr,
    bool capture_virtual
  );

  /**
   * @brief Resolve a client-requested `jochonaTuple` id against the proven
   *        store and the session's actual requested mode.
   *
   * Validates that: (1) the id names a currently proven tuple, (2) its
   * resolution/fps/HDR match the session's requested mode exactly, and (3)
   * it has been proven for the session's requested capture target
   * (physical unless `capture_virtual` is set). On success, pins the
   * session's codec selection by narrowing video::active_hevc_mode/
   * active_av1_mode so RTSP/serverinfo codec negotiation cannot silently
   * substitute a different codec than the one the client pinned, and
   * records the exact codec/dynamic-range/chroma constraints on
   * `session.pinned_encoder_tuple` so cmd_announce() can reject an RTSP
   * ANNOUNCE that requests anything else, since a client is not obligated
   * to actually request the tuple it pinned.
   *
   * @param requested_id Client-requested `jochonaTuple` id.
   * @param session Launch session to validate/pin against.
   * @param capture_virtual True when the session requests a leased virtual display.
   * @return std::nullopt when the tuple is accepted and pinned; otherwise
   *         an `encoder_tuple_unavailable` rejection with verified
   *         alternatives at the same resolution/fps/HDR shape.
   */
  [[nodiscard]] std::optional<tuple_rejection_t> resolve_requested_tuple(std::string_view requested_id, rtsp_stream::launch_session_t &session, bool capture_virtual);

  /**
   * @brief Lease and configure the default Jochona Display Adapter slot for
   *        a `/launch?virtualDisplay=1` session.
   *
   * Leases slot 0 under an owner id derived from this Host's stable
   * `http::unique_id`, pins the driver's render adapter to
   * `config::video.adapter_name`'s DXGI LUID when resolvable, and
   * configures the slot with the session's width/height/fps/HDR. The
   * resulting lease is held in process-wide session state (capacity is 1
   * active session) until release_active_virtual_display_lease() is called.
   *
   * @param session Launch session whose requested mode is leased/configured.
   * @return std::nullopt on success; otherwise an `encoder_tuple_unavailable`
   *         rejection (stage "virtual_display_lease") describing why the
   *         adapter could not be leased/configured.
   */
  [[nodiscard]] std::optional<tuple_rejection_t> acquire_virtual_display_for_session(const rtsp_stream::launch_session_t &session);

  /**
   * @brief Release the currently held virtual-display lease, if any.
   *
   * Idempotent; safe to call unconditionally from every session-teardown
   * path (launch failure, /cancel, app exit, stream stop) regardless of
   * whether this session actually requested a virtual display.
   */
  void release_active_virtual_display_lease();

  /**
   * @brief True if a virtual-display lease is currently held for the active session.
   *
   * @return True if a virtual-display lease is currently held.
   */
  [[nodiscard]] bool virtual_display_lease_active();

  /**
   * @brief Client-requested shape for a standalone `POST /jochona/v1/probe`
   *        attempt -- everything except `backend`, which this Host resolves
   *        internally from whichever encoder probe_encoders() selects (the
   *        client has no way to know or choose it in advance).
   */
  struct exact_tuple_shape_t {
    std::string codec;  ///< "h264" | "hevc" | "av1".
    std::string profile;  ///< "main8" | "main10".
    std::string chroma;  ///< "420" | "444".
    std::uint32_t width = 0;  ///< Frame width in pixels.
    std::uint32_t height = 0;  ///< Frame height in pixels.
    std::uint32_t fps = 0;  ///< Frame rate in frames per second.
    bool hdr = false;  ///< True when the tuple uses HDR.
  };

  /**
   * @brief Probe one exact encoder-tuple combination outside of a live
   *        session, for the `/jochona/v1/probe` bootstrap-proof endpoint.
   *
   * A fresh Host/Jochona Client pairing has no proven tuples until a
   * session has actually launched once, but Jochona Client refuses to
   * launch without a matching advertised tuple -- this breaks that
   * deadlock by running the exact same proof sequence a real `/launch` or
   * `/resume` call would use for the same combination: reconfigure the
   * display for the requested mode, re-select the active encoder
   * (video::probe_encoders()), and encode probe frames for the exact
   * candidate (video::probe_encoder_config()). A successful probe is
   * recorded into encoder::store_t exactly like a successful launch would
   * be, so it is immediately visible in the next `/jochona/v1/capabilities`
   * fetch. Never weakens the "exact proof before advertise" invariant: a
   * failure here means the combination genuinely does not work, not a
   * best-effort guess.
   *
   * Rejects with `host_busy` when an app or session is already active
   * (probing reconfigures live display/capture state). Always reverts any
   * display or virtual-display-lease state it changed before returning,
   * since no real session follows a probe.
   *
   * @param shape Requested codec/profile/chroma/resolution/fps/HDR combination.
   * @param capture_virtual True to prove virtual-display capture; false for physical capture.
   * @return The resulting proven tuple's stable id (already recorded in
   *         encoder::store_t) on success; otherwise an
   *         `encoder_tuple_unavailable` or `host_busy` rejection.
   */
  [[nodiscard]] std::variant<std::string, tuple_rejection_t> probe_exact_tuple(const exact_tuple_shape_t &shape, bool capture_virtual);

}  // namespace jochona::launch
