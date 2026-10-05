/**
 * @file src/jochona/capabilities.h
 * @brief Typed Jochona capability/permission model backing GET /jochona/v1/capabilities
 *        and the Apollo-compatible `<Permission>`/`<VirtualDisplayCapable>`/
 *        `<VirtualDisplayDriverReady>` tags on /serverinfo.
 *
 * Jochona Host is family "Jochona" but stays wire-compatible with Apollo's
 * /serverinfo permission model so an Apollo-aware client (or Jochona Client
 * itself, which mirrors this bit layout in
 * app/backend/adapters/hostcapabilities.h) can read a coherent permission
 * bitmask without special-casing the Jochona family. There is exactly one
 * canonical policy: a fixed set of named Jochona permissions, each of which
 * knows (a) the manifest string it is advertised as in the JSON
 * `permissions` array, and (b) the Apollo PERM bits it derives to, if any.
 *
 * `host.observe` is Jochona-only: it grants read access to authenticated
 * /serverinfo and the capacity/capability fields of the manifest (used by
 * Beacon's observer-only Host pairing) and deliberately derives to zero
 * Apollo bits, since Apollo has no observer-only concept and granting any
 * of ListApps/ViewStream/LaunchApps/Input would exceed the intended scope.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jochona::capability {

  /**
   * @brief Canonical Jochona permissions. Manifest strings and Apollo-bit
   *        derivation live in manifest_string()/apollo_bits() below so the
   *        mapping stays in exactly one place.
   */
  enum class permission_e {
    session_launch,  ///< "session.launch" -- start an application/session.
    session_stop,  ///< "session.stop" -- terminate the active session.
    host_volume_read,  ///< "host.volume.read" -- read current host output volume.
    host_volume_write,  ///< "host.volume.write" -- change host output volume.
    host_observe,  ///< "host.observe" -- read-only /serverinfo + capabilities/capacity, no control.
  };

  /**
   * @brief Bit-for-bit mirror of Apollo's crypto::PERM leaf bits (upstream
   *        src/crypto.h), matching Jochona Client's
   *        HostCapabilities::Permission exactly. Group prefixes
   *        (_input=1<<8, _operation=1<<16, _action=1<<24) are not
   *        reproduced; only the leaf bits a policy can derive to.
   */
  enum apollo_perm_e : std::uint32_t {
    apollo_perm_none = 0,  ///< No Apollo permission bits.

    apollo_perm_input_controller = 1u << 8,  ///< Controller input.
    apollo_perm_input_touch = 1u << 9,  ///< Touch input.
    apollo_perm_input_pen = 1u << 10,  ///< Pen input.
    apollo_perm_input_mouse = 1u << 11,  ///< Mouse input.
    apollo_perm_input_keyboard = 1u << 12,  ///< Keyboard input.

    apollo_perm_clipboard_set = 1u << 16,  ///< Set clipboard contents.
    apollo_perm_clipboard_read = 1u << 17,  ///< Read clipboard contents.
    apollo_perm_file_upload = 1u << 18,  ///< Upload files to the host.
    apollo_perm_file_download = 1u << 19,  ///< Download files from the host.
    apollo_perm_server_command = 1u << 20,  ///< General administrative/server-command operations.

    apollo_perm_list_apps = 1u << 24,  ///< List available applications.
    apollo_perm_view_stream = 1u << 25,  ///< View the active stream.
    apollo_perm_launch_apps = 1u << 26,  ///< Launch applications/sessions.
  };

  /**
   * @brief All canonical permissions, in the stable order they are
   *        advertised in the manifest.
   *
   * @return The canonical permission list.
   */
  const std::vector<permission_e> &all_permissions();

  /**
   * @brief The wire manifest string for a canonical permission, e.g. "session.launch".
   *
   * @param permission Canonical permission to convert.
   * @return The permission's wire manifest string.
   */
  std::string_view manifest_string(permission_e permission);

  /**
   * @brief Parse a manifest string back to its canonical permission.
   * @param value Wire manifest string, e.g. "session.launch".
   * @return std::nullopt when the string does not name a known permission.
   */
  std::optional<permission_e> permission_from_manifest_string(std::string_view value);

  /**
   * @brief The Apollo PERM bits a single canonical permission derives to.
   *
   * `session.stop` derives to the same LaunchApps bit as `session.launch`:
   * Apollo itself gates CancelApp on PERM::launch, with no separate "exit"
   * bit (see Jochona Client's HostCapabilities::allowExit()). `host.volume.*`
   * derive to ServerCommand, Apollo's general administrative-operation bit,
   * since Apollo has no dedicated volume-control leaf bit. `host.observe`
   * derives to apollo_perm_none by design.
   *
   * @param permission Canonical permission to derive from.
   * @return Bitwise OR of the Apollo PERM bits this permission grants.
   */
  std::uint32_t apollo_bits(permission_e permission);

  /**
   * @brief A concrete grant: which canonical permissions a caller holds.
   */
  class permission_set_t {
  public:
    permission_set_t() = default;

    /// Grant a canonical permission. Idempotent.
    /// @param permission Permission to grant.
    void grant(permission_e permission);

    /// True if `permission` has been granted.
    /// @param permission Permission to check.
    /// @return True if `permission` has been granted.
    [[nodiscard]] bool has(permission_e permission) const;

    /// Manifest strings for every granted permission, in canonical order.
    /// @return The manifest strings for every granted permission.
    [[nodiscard]] std::vector<std::string> manifest_strings() const;

    /// Bitwise OR of apollo_bits() across every granted permission.
    /// @return The combined Apollo PERM bitmask for this grant.
    [[nodiscard]] std::uint32_t apollo_permission_bits() const;

    /**
     * @brief The default grant for a fully paired GameStream client.
     *
     * Jochona Host's baseline pairing flow (inherited from Sunshine) has no
     * granular per-client consent UI, so every client that completes
     * GameStream pairing receives the full first-party control grant:
     * launch/stop a session and read/write host volume. This does not
     * include `host.observe`, which is reserved for Beacon's
     * observer-only enrollment path.
     *
     * @return The default paired-client permission grant.
     */
    static permission_set_t default_paired_client_grant();

    /**
     * @brief The grant used for Beacon-style observer-only enrollment:
     *        `host.observe` alone, deriving to zero Apollo bits.
     *
     * @return The observer-only permission grant.
     */
    static permission_set_t observer_grant();

  private:
    std::uint32_t granted_mask_ = 0;  ///< Bitmask over permission_e ordinal values.
  };

}  // namespace jochona::capability
