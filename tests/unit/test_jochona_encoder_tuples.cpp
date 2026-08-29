/**
 * @file tests/unit/test_jochona_encoder_tuples.cpp
 * @brief Test src/jochona/encoder_tuples.*.
 */
// test imports
#include "../tests_common.h"

// standard imports
#include <algorithm>

// local imports
#include <src/jochona/encoder_tuples.h>

using namespace jochona::encoder;

namespace {

  /// Build a well-formed tuple_key_t for the test resolution/backend used throughout this file.
  tuple_key_t make_key(std::string backend = "nvenc", std::string codec = "av1", bool hdr = true) {
    tuple_key_t key;
    key.backend = std::move(backend);
    key.codec = std::move(codec);
    key.profile = hdr ? "main10" : "main8";
    key.chroma = "420";
    key.width = 3840;
    key.height = 2160;
    key.fps = 120;
    key.hdr = hdr;
    return key;
  }

  environment_fingerprint_t make_environment(std::string gpu = "Test GPU") {
    environment_fingerprint_t environment;
    environment.gpu = std::move(gpu);
    environment.driver = "unknown";
    environment.display_mode = "3840x2160@120-hdr";
    environment.virtual_display_adapter_version = "not-installed";
    environment.host_build = "0.0.0-test";
    return environment;
  }

}  // namespace

class JochonaEncoderTuplesTest: public ::testing::Test {
protected:
  void SetUp() override {
    store_t::instance().clear();
  }

  void TearDown() override {
    store_t::instance().clear();
  }
};

TEST_F(JochonaEncoderTuplesTest, MakeStableIdFormatsEveryField) {
  auto id = make_stable_id(make_key());
  ASSERT_EQ(id, "nvenc-av1-main10-420-3840x2160-120-hdr");
}

TEST_F(JochonaEncoderTuplesTest, MakeStableIdReportsSdrWhenHdrIsFalse) {
  auto id = make_stable_id(make_key("amdvce", "hevc", false));
  ASSERT_EQ(id, "amdvce-hevc-main8-420-3840x2160-120-sdr");
}

TEST_F(JochonaEncoderTuplesTest, RecordSuccessIsFindableAndAdvertised) {
  auto &store = store_t::instance();
  auto key = make_key();
  auto environment = make_environment();

  store.record_success(key, "physical", environment);

  auto found = store.find(make_stable_id(key));
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(found->key, key);
  ASSERT_EQ(found->captures, std::vector<std::string> {"physical"});
  ASSERT_EQ(found->environment, environment);

  auto advertised = store.advertised_tuples();
  ASSERT_EQ(advertised.size(), 1);
  ASSERT_EQ(advertised.front().id, make_stable_id(key));
}

TEST_F(JochonaEncoderTuplesTest, RecordingTheSameKeyTwiceUnionsCaptureTargets) {
  auto &store = store_t::instance();
  auto key = make_key();
  auto environment = make_environment();

  store.record_success(key, "physical", environment);
  store.record_success(key, "virtual", environment);
  store.record_success(key, "virtual", environment);  // idempotent re-add

  auto found = store.find(make_stable_id(key));
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(found->captures.size(), 2);
  ASSERT_NE(std::ranges::find(found->captures, "physical"), found->captures.end());
  ASSERT_NE(std::ranges::find(found->captures, "virtual"), found->captures.end());
}

TEST_F(JochonaEncoderTuplesTest, ChangedEnvironmentInvalidatesEveryPreviousTuple) {
  auto &store = store_t::instance();
  auto stale_key = make_key("nvenc", "av1", true);
  auto fresh_key = make_key("nvenc", "hevc", true);

  store.record_success(stale_key, "physical", make_environment("GPU A"));
  ASSERT_TRUE(store.find(make_stable_id(stale_key)).has_value());

  // A different environment fingerprint discards every previously proven
  // tuple, including ones the new record_success call doesn't itself touch.
  store.record_success(fresh_key, "physical", make_environment("GPU B"));

  auto advertised = store.advertised_tuples();
  ASSERT_EQ(advertised.size(), 1);
  ASSERT_FALSE(store.find(make_stable_id(stale_key)).has_value());
  ASSERT_TRUE(store.find(make_stable_id(fresh_key)).has_value());
}

TEST_F(JochonaEncoderTuplesTest, ChangedDisplayModeInvalidatesEveryPreviousTuple) {
  auto &store = store_t::instance();
  auto stale_key = make_key("nvenc", "av1", true);
  auto fresh_key = make_key("nvenc", "hevc", false);
  auto stale_environment = make_environment();
  auto fresh_environment = make_environment();
  fresh_environment.display_mode = "1920x1080@60-sdr";

  store.record_success(stale_key, "physical", stale_environment);
  store.record_success(fresh_key, "physical", fresh_environment);

  ASSERT_FALSE(store.find(make_stable_id(stale_key)).has_value());
  ASSERT_TRUE(store.find(make_stable_id(fresh_key)).has_value());
}

TEST_F(JochonaEncoderTuplesTest, NewEnvironmentInvalidatesEvenWithoutSuccessfulTuple) {
  auto &store = store_t::instance();
  auto stale_key = make_key();
  auto stale_environment = make_environment();
  auto fresh_environment = make_environment();
  fresh_environment.driver = "new-driver";

  store.record_success(stale_key, "physical", stale_environment);
  store.begin_environment(fresh_environment);

  ASSERT_TRUE(store.advertised_tuples().empty());
}

TEST_F(JochonaEncoderTuplesTest, UnknownIdIsNotFound) {
  ASSERT_FALSE(store_t::instance().find("nvenc-av1-main10-420-3840x2160-120-hdr").has_value());
}

TEST_F(JochonaEncoderTuplesTest, AlternativesForExcludesRequestedIdAndDiffersByShape) {
  auto &store = store_t::instance();
  auto environment = make_environment();

  auto requested = make_key("nvenc", "av1", true);
  auto same_shape_alt = make_key("nvenc", "hevc", true);
  auto different_shape = make_key("nvenc", "h264", false);
  different_shape.width = 1920;
  different_shape.height = 1080;
  different_shape.fps = 60;

  store.record_success(requested, "physical", environment);
  store.record_success(same_shape_alt, "physical", environment);
  store.record_success(different_shape, "physical", environment);

  auto alternatives = store.alternatives_for(make_stable_id(requested));
  ASSERT_EQ(alternatives.size(), 1);
  ASSERT_EQ(alternatives.front(), make_stable_id(same_shape_alt));
}

TEST_F(JochonaEncoderTuplesTest, AlternativesForMalformedIdIsEmpty) {
  ASSERT_TRUE(store_t::instance().alternatives_for("not-a-well-formed-id").empty());
}

TEST_F(JochonaEncoderTuplesTest, ClearRemovesEveryRecordedTuple) {
  auto &store = store_t::instance();
  store.record_success(make_key(), "physical", make_environment());
  ASSERT_FALSE(store.advertised_tuples().empty());

  store.clear();
  ASSERT_TRUE(store.advertised_tuples().empty());
}
