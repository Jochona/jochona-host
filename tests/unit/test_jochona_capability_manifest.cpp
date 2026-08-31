/**
 * @file tests/unit/test_jochona_capability_manifest.cpp
 * @brief Test src/jochona/capability_manifest.*.
 */
// test imports
#include "../tests_common.h"

// standard imports
#include <string>
#include <vector>

// local imports
#include <src/jochona/capability_manifest.h>
#include <src/jochona/display_adapter_client.h>

using namespace jochona;

TEST(JochonaCapabilityManifestTest, VirtualDisplayPoolIsEmptyWithoutTheRealDriver) {
  // This test process has no Jochona Display Adapter device to talk to, so
  // enumerate_slots() must honestly report std::nullopt, and the manifest
  // must never publish a guessed width/height/fps/hdr pool entry for a
  // slot it could not actually query (see docs/protocols/
  // jochona-host-capabilities.md's virtualDisplay.pool schema).
  ASSERT_FALSE(display_adapter::client_t::instance().enumerate_slots().has_value());

  auto body = manifest::build(capability::permission_set_t::observer_grant());
  ASSERT_TRUE(body.contains("virtualDisplay"));
  ASSERT_TRUE(body["virtualDisplay"]["pool"].is_array());
  ASSERT_TRUE(body["virtualDisplay"]["pool"].empty());
  ASSERT_FALSE(body["virtualDisplay"]["installed"].get<bool>());
  ASSERT_FALSE(body["virtualDisplay"]["healthy"].get<bool>());
}

TEST(JochonaCapabilityManifestTest, ManifestReportsExactlyTheRequestedPermissionSet) {
  auto observer_body = manifest::build(capability::permission_set_t::observer_grant());
  auto observer_permissions = observer_body.at("permissions").get<std::vector<std::string>>();
  ASSERT_EQ(observer_permissions, std::vector<std::string> {"host.observe"});

  auto full_body = manifest::build(capability::permission_set_t::default_paired_client_grant());
  auto full_permissions = full_body.at("permissions").get<std::vector<std::string>>();
  ASSERT_EQ(
    full_permissions,
    (std::vector<std::string> {"session.launch", "session.stop", "host.volume.read", "host.volume.write"})
  );
}

TEST(JochonaCapabilityManifestTest, ManifestSchemaIsAlwaysMajorOneMinorZero) {
  auto body = manifest::build(capability::permission_set_t::default_paired_client_grant());
  ASSERT_EQ(body.at("schema").at("major").get<int>(), 1);
  ASSERT_EQ(body.at("schema").at("minor").get<int>(), 0);
}
