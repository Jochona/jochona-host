/**
 * @file src/jochona/encoder_tuples.h
 * @brief Proven encoder-tuple store keyed by backend/GPU/driver/display/mode/build facts.
 *
 * An Encoder Tuple is only advertised in the capabilities manifest, and only
 * selectable via `/launch?jochonaTuple=<id>`, after the exact
 * (backend, codec, profile, chroma, resolution, fps, HDR) combination has
 * actually encoded probe frames successfully via video::probe_encoders().
 * Static GPU-model assumptions and vendor capability bits alone are never
 * sufficient -- see docs/protocols/jochona-host-capabilities.md.
 *
 * A tuple's identity (tuple_key_t) does not include the capture target:
 * per the manifest schema, `capture` is an array of capture types
 * ("physical", "virtual") that a single tuple has been independently
 * proven against, not a discriminant that splits one codec/resolution
 * combination into separate tuples.
 *
 * The store is invalidated wholesale whenever the recorded environment
 * fingerprint changes. That fingerprint contains GPU, driver, display mode,
 * virtual-display-adapter version, and Host build facts. A successful probe
 * in a different display mode therefore replaces every older tuple instead
 * of advertising stale proof from a mode that is no longer active.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace jochona::encoder {

  /**
   * @brief Host-wide facts that must all match for two probe results to
   *        belong to the same proof environment. Any difference invalidates
   *        the entire store (see the file-level doc comment).
   */
  struct environment_fingerprint_t {
    std::string gpu;  ///< Best-effort GPU identity (configured adapter name, or "unspecified").
    std::string driver;  ///< Best-effort driver identity, or "unknown" when undiscoverable on this platform.
    std::string display_mode;  ///< Active "{width}x{height}@{fps}-{hdr|sdr}" display mode.
    std::string virtual_display_adapter_version;  ///< Installed Jochona Display Adapter version, or "not-installed".
    std::string host_build;  ///< Jochona Host build/version string (PROJECT_VERSION).

    bool operator==(const environment_fingerprint_t &) const = default;
  };

  /**
   * @brief Identity of one proven encoder tuple, independent of capture target.
   */
  struct tuple_key_t {
    std::string backend;  ///< Encoder backend name, e.g. "nvenc", "amdvce", "quicksync", "vaapi", "videotoolbox", "software".
    std::string codec;  ///< "h264" | "hevc" | "av1".
    std::string profile;  ///< "main8" | "main10".
    std::string chroma;  ///< "420" | "444".
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 0;
    bool hdr = false;

    bool operator==(const tuple_key_t &) const = default;
  };

  /**
   * @brief One entry in the proven-tuple store: identity, the capture
   *        targets it has been proven against, and proof metadata.
   */
  struct proven_tuple_t {
    std::string id;  ///< Stable id, e.g. "nvenc-av1-main10-420-3840x2160-120-hdr".
    tuple_key_t key;
    environment_fingerprint_t environment;  ///< Proof environment captured with this tuple.
    std::vector<std::string> captures;  ///< Subset of {"physical", "virtual"} independently proven for this key.
    std::string method = "vendor-query+probe-frames";  ///< Proof method, per the capabilities schema.
    std::chrono::system_clock::time_point verified_at;  ///< Most recent time probe frames succeeded for this key.
  };

  /**
   * @brief Build the stable, human-legible tuple id for a key.
   *
   * Format: "{backend}-{codec}-{profile}-{chroma}-{width}x{height}-{fps}-{hdr|sdr}".
   */
  std::string make_stable_id(const tuple_key_t &key);

  /**
   * @brief Process-wide store of proven encoder tuples.
   *
   * Thread-safe: probe_encoders() runs on the nvhttp server thread and the
   * capabilities route may run concurrently on the same thread pool for a
   * different connection, so all access is guarded by an internal mutex.
   */
  class store_t {
  public:
    static store_t &instance();

    /**
     * @brief Set the current proof environment before exact tuple probes.
     *
     * A changed environment clears all older tuples even when every new
     * exact probe fails and therefore records no replacement tuple.
     */
    void begin_environment(const environment_fingerprint_t &environment);

    /**
     * @brief Record that `key` proved successfully for `capture` under `environment`.
     *
     * If `environment` differs from the fingerprint of the store's existing
     * entries, every previously recorded tuple is discarded first (see the
     * file-level doc comment on invalidation). Re-recording an existing key
     * adds `capture` to its proven-captures set (if not already present)
     * and refreshes `verified_at`.
     *
     * @param capture Either "physical" or "virtual".
     */
    void record_success(const tuple_key_t &key, std::string_view capture, const environment_fingerprint_t &environment, std::chrono::system_clock::time_point verified_at = std::chrono::system_clock::now());

    /**
     * @brief Every currently advertised (i.e. still-valid) proven tuple.
     */
    [[nodiscard]] std::vector<proven_tuple_t> advertised_tuples() const;

    /**
     * @brief Look up a proven tuple by its stable id.
     */
    [[nodiscard]] std::optional<proven_tuple_t> find(std::string_view id) const;

    /**
     * @brief Proven tuple ids at the same resolution/fps as `requested_id`
     *        (parsed from the id's own shape, even if `requested_id` is not
     *        itself currently proven) that differ in codec/profile/chroma/
     *        HDR -- used to populate the `alternatives` array of an
     *        `encoder_tuple_unavailable` response. Excludes `requested_id`
     *        itself. Empty when `requested_id` does not parse as a
     *        well-formed tuple id.
     */
    [[nodiscard]] std::vector<std::string> alternatives_for(std::string_view requested_id) const;

    /// Test/diagnostic helper: drop every recorded tuple.
    void clear();

  private:
    store_t() = default;

    mutable std::mutex mutex_;
    std::optional<environment_fingerprint_t> environment_;
    std::vector<proven_tuple_t> tuples_;
  };

}  // namespace jochona::encoder
