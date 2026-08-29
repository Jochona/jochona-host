/**
 * @file src/jochona/capabilities.cpp
 * @brief Definitions for the typed Jochona capability/permission model.
 */
#include "capabilities.h"

// standard includes
#include <algorithm>

namespace jochona::capability {

  const std::vector<permission_e> &all_permissions() {
    static const std::vector<permission_e> permissions {
      permission_e::session_launch,
      permission_e::session_stop,
      permission_e::host_volume_read,
      permission_e::host_volume_write,
      permission_e::host_observe,
    };
    return permissions;
  }

  std::string_view manifest_string(permission_e permission) {
    switch (permission) {
      case permission_e::session_launch:
        return "session.launch";
      case permission_e::session_stop:
        return "session.stop";
      case permission_e::host_volume_read:
        return "host.volume.read";
      case permission_e::host_volume_write:
        return "host.volume.write";
      case permission_e::host_observe:
        return "host.observe";
    }
    return {};
  }

  std::optional<permission_e> permission_from_manifest_string(std::string_view value) {
    for (auto permission : all_permissions()) {
      if (manifest_string(permission) == value) {
        return permission;
      }
    }
    return std::nullopt;
  }

  std::uint32_t apollo_bits(permission_e permission) {
    switch (permission) {
      case permission_e::session_launch:
        return apollo_perm_launch_apps;
      case permission_e::session_stop:
        // Apollo gates CancelApp on PERM::launch as well as LaunchApp; there
        // is no separate "exit"/"stop" leaf bit to derive to.
        return apollo_perm_launch_apps;
      case permission_e::host_volume_read:
      case permission_e::host_volume_write:
        // Apollo has no dedicated volume-control leaf bit; volume actions
        // are gated behind the general administrative ServerCommand bit.
        return apollo_perm_server_command;
      case permission_e::host_observe:
        // Deliberately zero: observer access is a Jochona-only concept with
        // no Apollo-compatible equivalent, and must not imply any Apollo
        // control bit (list/view/launch/input all remain unset).
        return apollo_perm_none;
    }
    return apollo_perm_none;
  }

  void permission_set_t::grant(permission_e permission) {
    granted_mask_ |= (1u << static_cast<std::uint32_t>(permission));
  }

  bool permission_set_t::has(permission_e permission) const {
    return (granted_mask_ & (1u << static_cast<std::uint32_t>(permission))) != 0;
  }

  std::vector<std::string> permission_set_t::manifest_strings() const {
    std::vector<std::string> result;
    for (auto permission : all_permissions()) {
      if (has(permission)) {
        result.emplace_back(manifest_string(permission));
      }
    }
    return result;
  }

  std::uint32_t permission_set_t::apollo_permission_bits() const {
    std::uint32_t bits = apollo_perm_none;
    for (auto permission : all_permissions()) {
      if (has(permission)) {
        bits |= apollo_bits(permission);
      }
    }
    return bits;
  }

  permission_set_t permission_set_t::default_paired_client_grant() {
    permission_set_t set;
    set.grant(permission_e::session_launch);
    set.grant(permission_e::session_stop);
    set.grant(permission_e::host_volume_read);
    set.grant(permission_e::host_volume_write);
    return set;
  }

  permission_set_t permission_set_t::observer_grant() {
    permission_set_t set;
    set.grant(permission_e::host_observe);
    return set;
  }

}  // namespace jochona::capability
