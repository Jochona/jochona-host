/**
 * @file tests/unit/test_jochona_launch_tuple.cpp
 * @brief Test src/jochona/launch_tuple.*.
 */
// test imports
#include "../tests_common.h"

// lib imports
#include <nlohmann/json.hpp>

// local imports
#include <src/jochona/launch_tuple.h>

using namespace jochona::launch;

TEST(JochonaLaunchTupleTest, HostBusyRejectionShapeOmitsRequestedStageAndAlternatives) {
  auto rejection = host_busy_rejection();
  ASSERT_EQ(rejection.error, "host_busy");
  ASSERT_TRUE(rejection.requested.empty());
  ASSERT_TRUE(rejection.stage.empty());
  ASSERT_FALSE(rejection.detail.empty());

  auto body = nlohmann::json::parse(to_json(rejection));
  ASSERT_EQ(body.at("error"), "host_busy");
  ASSERT_FALSE(body.contains("requested"));
  ASSERT_FALSE(body.contains("stage"));
  ASSERT_FALSE(body.contains("alternatives"));
  ASSERT_TRUE(body.contains("detail"));
}

TEST(JochonaLaunchTupleTest, EncoderTupleUnavailableRejectionIncludesRequestedStageAndAlternatives) {
  tuple_rejection_t rejection;
  rejection.error = "encoder_tuple_unavailable";
  rejection.requested = "nvenc-av1-main10-420-3840x2160-120-hdr";
  rejection.stage = "encoder_initialize";
  rejection.detail = "NVENC rejected the capture format for this display mode.";
  rejection.alternatives = {"nvenc-hevc-main10-420-3840x2160-120-hdr", "nvenc-av1-main8-420-3840x2160-120-sdr"};

  auto body = nlohmann::json::parse(to_json(rejection));
  ASSERT_EQ(body.at("error"), "encoder_tuple_unavailable");
  ASSERT_EQ(body.at("requested"), rejection.requested);
  ASSERT_EQ(body.at("stage"), rejection.stage);
  ASSERT_EQ(body.at("detail"), rejection.detail);

  auto alternatives = body.at("alternatives").get<std::vector<std::string>>();
  ASSERT_EQ(alternatives, rejection.alternatives);
}

TEST(JochonaLaunchTupleTest, VirtualDisplayLeaseIsInactiveWithoutAnAcquiredLease) {
  // No lease has been acquired in this process; the query must report false
  // rather than fabricating an active lease, and release must be a safe
  // idempotent no-op.
  ASSERT_FALSE(virtual_display_lease_active());
  release_active_virtual_display_lease();
  ASSERT_FALSE(virtual_display_lease_active());
}
