/**
 * @file src/jochona/display_adapter_client.h
 * @brief Host-side client for the versioned Jochona Display Adapter IOCTL ABI.
 *
 * Wraps the wire protocol defined in display_adapter_abi.h: protocol-version
 * detection, one-slot lease/configure/release, render-adapter LUID pinning,
 * and a background watchdog-ping thread that keeps a lease alive for as
 * long as the returned lease handle is held.
 *
 * This header is intentionally platform-neutral (no Windows types) so it
 * can be included and unit tested on every Sunshine build platform. Only
 * the .cpp translation unit branches on `_WIN32`; on every other platform
 * the client honestly reports the adapter as not installed, since the
 * Jochona Display Adapter driver is Windows-only (protocol contract
 * targets Windows 11 IddCx).
 */
#pragma once

// standard includes
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace jochona::display_adapter {

  /**
   * @brief Requested or reported display mode for a leased slot.
   */
  struct slot_mode_t {
    std::uint32_t width = 0;  ///< Frame width in pixels.
    std::uint32_t height = 0;  ///< Frame height in pixels.
    std::uint32_t refresh_numerator = 0;  ///< Refresh-rate numerator.
    std::uint32_t refresh_denominator = 1;  ///< Refresh-rate denominator.
    std::uint32_t bits_per_channel = 8;  ///< Bits per color channel.
    bool hdr_enabled = false;  ///< Whether HDR metadata is active for this mode.
  };

  /**
   * @brief Result of probing the driver for protocol compatibility and health.
   *
   * Every field reflects an actual attempt to reach the driver; nothing here
   * is inferred from static configuration. `installed` is true only when
   * the device interface GUID resolves to a real device path, and `healthy`
   * is true only when that device additionally answered GET_PROTOCOL_VERSION
   * with protocol major version 1.
   */
  struct manifest_status_t {
    bool installed = false;  ///< True if the device interface GUID resolved to a device path.
    bool healthy = false;  ///< True if the driver answered GET_PROTOCOL_VERSION with a compatible version.
    std::uint16_t protocol_major = 0;  ///< Negotiated protocol major version, valid when `healthy`.
    std::uint16_t protocol_minor = 0;  ///< Negotiated protocol minor version, valid when `healthy`.
    std::string detail;  ///< Human-readable detail, always populated (success or failure reason).
  };

  /**
   * @brief Snapshot of one slot in the driver's pool, as reported by
   *        ENUMERATE_SLOTS -- the real per-slot data backing the
   *        manifest's `virtualDisplay.pool[]` entries.
   */
  struct slot_status_t {
    std::uint32_t id = 0;  ///< Slot id (0 for the sole protocol v1.0 default slot).
    bool leased = false;  ///< True when the slot is Leased or Configured (in use by some owner).
    slot_mode_t mode;  ///< Baseline mode when free, current configured mode otherwise (per the ABI's JochonaSlotInfo.Mode doc).
  };

  /**
   * @brief Failure reasons a lease/configure/release call can return.
   */
  enum class error_e {
    driver_not_installed,  ///< No device interface for the adapter GUID was found.
    protocol_version_mismatch,  ///< The driver's protocol major version is incompatible.
    slot_not_found,  ///< The requested slot id does not exist.
    slot_busy,  ///< The slot is already leased by another owner.
    slot_not_leased,  ///< Configure/release was attempted without a valid lease.
    lease_token_mismatch,  ///< The supplied lease token does not match the driver's record.
    invalid_parameter,  ///< The driver rejected the request shape.
    io_failure,  ///< The IOCTL call itself failed (device removed, access denied, etc.).
  };

  /**
   * @brief Convert an error_e to a stable lowercase snake_case token for logs/JSON.
   */
  std::string_view to_string(error_e error);

  class lease_handle_t;

  /**
   * @brief Singleton client for the default (slot 0) Jochona Display Adapter pool member.
   *
   * Protocol v1.0 defines exactly one stable slot; this client only ever
   * addresses slot 0, matching JOCHONA_PROTOCOL_V1_MAX_SLOTS.
   */
  class client_t {
  public:
    /**
     * @brief Access the process-wide client instance.
     */
    static client_t &instance();

    client_t(const client_t &) = delete;
    client_t &operator=(const client_t &) = delete;

    /**
     * @brief Probe driver installation and protocol health without acquiring a lease.
     *
     * Safe to call at any time, including while a lease is held elsewhere
     * in the process; used to populate the capabilities manifest's
     * `virtualDisplay.installed`/`healthy` fields honestly.
     */
    [[nodiscard]] manifest_status_t probe();

    /**
     * @brief Enumerate the driver's slot pool (id, lease state, mode) for
     *        the capabilities manifest's `virtualDisplay.pool[]`.
     *
     * Safe to call at any time, including while a lease is held elsewhere
     * in the process. Returns `std::nullopt` when the driver is not
     * installed or unreachable; never fabricates slot data.
     */
    [[nodiscard]] std::optional<std::vector<slot_status_t>> enumerate_slots();

    /**
     * @brief Lease the default slot (id 0) for the given opaque owner id.
     *
     * @param owner_id 16-byte opaque owner identity (e.g. this Host process's session identity).
     * @return An owning lease handle on success, or an error_e on failure.
     */
    [[nodiscard]] std::variant<lease_handle_t, error_e> lease(const std::array<std::uint8_t, 16> &owner_id);

    /**
     * @brief Pin the render adapter used by the driver's swapchain to the given LUID.
     *
     * @param luid_low_part Low 32 bits of the DXGI adapter LUID.
     * @param luid_high_part High 32 bits (signed) of the DXGI adapter LUID.
     * @return True on success.
     */
    bool set_render_adapter_luid(std::uint32_t luid_low_part, std::int32_t luid_high_part);

  private:
    client_t();
    friend class lease_handle_t;

    /// Opens (or reuses) the device handle; returns false if the driver is unreachable.
    bool ensure_open();

    struct impl_t;
    std::shared_ptr<impl_t> impl_;
  };

  /**
   * @brief RAII lease over the default display-adapter slot.
   *
   * While alive, a background thread pings the driver's watchdog at half
   * the driver-reported timeout interval. Destruction (or an explicit
   * `release()`) sends RELEASE_SLOT and stops the watchdog thread. Moving
   * a lease_handle_t transfers ownership; a moved-from handle releases
   * nothing on destruction.
   */
  class lease_handle_t {
  public:
    lease_handle_t(lease_handle_t &&other) noexcept;
    lease_handle_t &operator=(lease_handle_t &&other) noexcept;
    lease_handle_t(const lease_handle_t &) = delete;
    lease_handle_t &operator=(const lease_handle_t &) = delete;
    ~lease_handle_t();

    /**
     * @brief Configure the leased slot with the given mode.
     *
     * @return std::nullopt on success, or the error_e on failure. On error the lease
     *         itself remains held; the caller may retry with a different mode.
     */
    [[nodiscard]] std::optional<error_e> configure(const slot_mode_t &mode);

    /**
     * @brief Release the slot and stop the watchdog thread early.
     *
     * Idempotent; safe to call before destruction. After this call the
     * handle no longer owns a lease.
     */
    void release();

    /**
     * @brief True if this handle currently owns a live lease.
     */
    [[nodiscard]] bool valid() const;

  private:
    friend class client_t;
    struct impl_t;
    explicit lease_handle_t(std::unique_ptr<impl_t> impl);
    std::unique_ptr<impl_t> impl_;
  };

}  // namespace jochona::display_adapter
