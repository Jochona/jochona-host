/**
 * @file tests/unit/test_jochona_capabilities.cpp
 * @brief Test src/jochona/capabilities.*.
 */
// test imports
#include "../tests_common.h"

// local imports
#include <src/jochona/capabilities.h>

using namespace jochona::capability;

TEST(JochonaCapabilitiesTest, ManifestStringRoundTripsForEveryPermission) {
  for (auto permission : all_permissions()) {
    auto manifest_string_value = manifest_string(permission);
    ASSERT_FALSE(manifest_string_value.empty());

    auto parsed = permission_from_manifest_string(manifest_string_value);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(*parsed, permission);
  }
}

TEST(JochonaCapabilitiesTest, UnknownManifestStringParsesToNullopt) {
  ASSERT_FALSE(permission_from_manifest_string("not.a.real.permission").has_value());
}

TEST(JochonaCapabilitiesTest, HostObserveDerivesToNoApolloBits) {
  ASSERT_EQ(apollo_bits(permission_e::host_observe), static_cast<std::uint32_t>(apollo_perm_none));
}

TEST(JochonaCapabilitiesTest, SessionLaunchAndSessionStopShareTheApolloLaunchBit) {
  ASSERT_EQ(apollo_bits(permission_e::session_launch), static_cast<std::uint32_t>(apollo_perm_launch_apps));
  ASSERT_EQ(apollo_bits(permission_e::session_stop), static_cast<std::uint32_t>(apollo_perm_launch_apps));
}

TEST(JochonaCapabilitiesTest, PermissionSetGrantIsIdempotentAndQueryable) {
  permission_set_t permissions;
  ASSERT_FALSE(permissions.has(permission_e::session_launch));

  permissions.grant(permission_e::session_launch);
  permissions.grant(permission_e::session_launch);  // idempotent
  ASSERT_TRUE(permissions.has(permission_e::session_launch));
  ASSERT_FALSE(permissions.has(permission_e::host_observe));

  auto strings = permissions.manifest_strings();
  ASSERT_EQ(strings.size(), 1);
  ASSERT_EQ(strings.front(), "session.launch");
}

TEST(JochonaCapabilitiesTest, DefaultPairedClientGrantExcludesHostObserve) {
  auto permissions = permission_set_t::default_paired_client_grant();
  ASSERT_TRUE(permissions.has(permission_e::session_launch));
  ASSERT_TRUE(permissions.has(permission_e::session_stop));
  ASSERT_TRUE(permissions.has(permission_e::host_volume_read));
  ASSERT_TRUE(permissions.has(permission_e::host_volume_write));
  ASSERT_FALSE(permissions.has(permission_e::host_observe));

  ASSERT_EQ(
    permissions.apollo_permission_bits(),
    static_cast<std::uint32_t>(apollo_perm_launch_apps | apollo_perm_server_command)
  );
}

TEST(JochonaCapabilitiesTest, ObserverGrantHoldsOnlyHostObserveAndNoApolloBits) {
  auto permissions = permission_set_t::observer_grant();
  ASSERT_TRUE(permissions.has(permission_e::host_observe));
  ASSERT_FALSE(permissions.has(permission_e::session_launch));
  ASSERT_FALSE(permissions.has(permission_e::session_stop));
  ASSERT_FALSE(permissions.has(permission_e::host_volume_read));
  ASSERT_FALSE(permissions.has(permission_e::host_volume_write));

  ASSERT_EQ(permissions.apollo_permission_bits(), static_cast<std::uint32_t>(apollo_perm_none));

  auto strings = permissions.manifest_strings();
  ASSERT_EQ(strings.size(), 1);
  ASSERT_EQ(strings.front(), "host.observe");
}
