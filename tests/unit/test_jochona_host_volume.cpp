/**
 * @file tests/unit/test_jochona_host_volume.cpp
 * @brief Test src/jochona/host_volume.*.
 */
// test imports
#include "../tests_common.h"

// lib imports
#include <nlohmann/json.hpp>

// local imports
#include <src/jochona/host_volume.h>

using namespace jochona::host_volume;

TEST(JochonaHostVolumeTest, AvailableStatusReportsRealFields) {
  status_t status;
  status.available = true;
  status.min = 0;
  status.max = 100;
  status.current = 42;

  auto body = to_json(status);
  ASSERT_EQ(body.at("available"), true);
  ASSERT_EQ(body.at("min"), 0);
  ASSERT_EQ(body.at("max"), 100);
  ASSERT_EQ(body.at("current"), 42);
}

TEST(JochonaHostVolumeTest, UnavailableStatusReportsNullRangeInsteadOfZeros) {
  status_t status;
  status.available = false;

  auto body = to_json(status);
  ASSERT_EQ(body.at("available"), false);
  ASSERT_TRUE(body.at("min").is_null());
  ASSERT_TRUE(body.at("max").is_null());
  ASSERT_TRUE(body.at("current").is_null());
}

TEST(JochonaHostVolumeTest, RejectionSerializesErrorAndDetail) {
  rejection_t rejection;
  rejection.error = "invalid_parameter";
  rejection.detail = "The 'level' query parameter must be an integer in [0, 100].";

  auto body = nlohmann::json::parse(to_json(rejection));
  ASSERT_EQ(body.at("error"), rejection.error);
  ASSERT_EQ(body.at("detail"), rejection.detail);
}

TEST(JochonaHostVolumeTest, GetHonestlyReportsPlatformAvailability) {
  // No assumptions about the concrete platform: available implies a
  // coherent [0, 100] range and a current value inside it; unavailable
  // implies the status_t defaults (min/max/current all 0), never a
  // fabricated range.
  auto status = get();
  if (status.available) {
    EXPECT_EQ(status.min, 0);
    EXPECT_EQ(status.max, 100);
    EXPECT_GE(status.current, status.min);
    EXPECT_LE(status.current, status.max);
  } else {
    EXPECT_EQ(status.min, 0);
    EXPECT_EQ(status.max, 0);
  }
}

TEST(JochonaHostVolumeTest, SetIsFalseWhenUnavailable) {
  // On any platform where the endpoint is genuinely unavailable, set() must
  // honestly fail rather than silently succeeding.
  if (!get().available) {
    EXPECT_FALSE(set(50));
  }
}
