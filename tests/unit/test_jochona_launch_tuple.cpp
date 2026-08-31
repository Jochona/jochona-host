/**
 * @file tests/unit/test_jochona_launch_tuple.cpp
 * @brief Test src/jochona/launch_tuple.*.
 */
// test imports
#include "../tests_common.h"

// lib imports
#include <nlohmann/json.hpp>

// local imports
#include <src/jochona/encoder_tuples.h>
#include <src/jochona/launch_tuple.h>
#include <src/rtsp.h>

using namespace jochona::launch;
using jochona::encoder::environment_fingerprint_t;
using jochona::encoder::store_t;
using jochona::encoder::tuple_key_t;

namespace {

  environment_fingerprint_t make_environment() {
    environment_fingerprint_t environment;
    environment.gpu = "10de:2684:8613:a1";
    environment.driver = "unknown";
    environment.display_mode = "3840x2160@120-hdr";
    environment.virtual_display_adapter_version = "not-installed";
    environment.host_build = "0.0.0-test";
    return environment;
  }

  rtsp_stream::launch_session_t make_session(int width, int height, int fps, bool hdr) {
    rtsp_stream::launch_session_t session {};
    session.width = width;
    session.height = height;
    session.fps = fps;
    session.enable_hdr = hdr;
    return session;
  }

}  // namespace

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

class JochonaResolveRequestedTupleTest: public ::testing::Test {
protected:
  void SetUp() override {
    store_t::instance().clear();
  }

  void TearDown() override {
    store_t::instance().clear();
  }
};

TEST_F(JochonaResolveRequestedTupleTest, AcceptedTuplePinsSessionForRtspAnnounceEnforcement) {
  tuple_key_t key;
  key.backend = "nvenc";
  key.codec = "hevc";
  key.profile = "main10";
  key.chroma = "444";
  key.width = 3840;
  key.height = 2160;
  key.fps = 120;
  key.hdr = true;

  auto environment = make_environment();
  store_t::instance().record_success(key, "physical", environment);

  auto session = make_session(3840, 2160, 120, true);
  auto rejection = resolve_requested_tuple("nvenc-hevc-main10-444-3840x2160-120-hdr", session, false);

  ASSERT_FALSE(rejection.has_value());
  ASSERT_TRUE(session.pinned_encoder_tuple.has_value());
  EXPECT_EQ(session.pinned_encoder_tuple->video_format, 1);  // HEVC
  EXPECT_EQ(session.pinned_encoder_tuple->dynamic_range, 1);  // HDR
  EXPECT_EQ(session.pinned_encoder_tuple->chroma_sampling_type, 1);  // 4:4:4
}

TEST_F(JochonaResolveRequestedTupleTest, RejectedTupleLeavesSessionUnpinned) {
  auto session = make_session(3840, 2160, 120, true);
  auto rejection = resolve_requested_tuple("nvenc-av1-main10-420-3840x2160-120-hdr", session, false);

  ASSERT_TRUE(rejection.has_value());
  EXPECT_EQ(rejection->error, "encoder_tuple_unavailable");
  EXPECT_FALSE(session.pinned_encoder_tuple.has_value());
}

TEST_F(JochonaResolveRequestedTupleTest, H264TuplePinsBaselineWireValues) {
  tuple_key_t key;
  key.backend = "nvenc";
  key.codec = "h264";
  key.profile = "main8";
  key.chroma = "420";
  key.width = 1920;
  key.height = 1080;
  key.fps = 60;
  key.hdr = false;

  auto environment = make_environment();
  store_t::instance().record_success(key, "physical", environment);

  auto session = make_session(1920, 1080, 60, false);
  auto rejection = resolve_requested_tuple("nvenc-h264-main8-420-1920x1080-60-sdr", session, false);

  ASSERT_FALSE(rejection.has_value());
  ASSERT_TRUE(session.pinned_encoder_tuple.has_value());
  EXPECT_EQ(session.pinned_encoder_tuple->video_format, 0);
  EXPECT_EQ(session.pinned_encoder_tuple->dynamic_range, 0);
  EXPECT_EQ(session.pinned_encoder_tuple->chroma_sampling_type, 0);
}
