/**
 * @file src/jochona/encoder_tuples.cpp
 * @brief Definitions for the proven encoder-tuple store.
 */
#include "encoder_tuples.h"

// standard includes
#include <algorithm>
#include <charconv>
#include <format>

namespace jochona::encoder {

  namespace {

    /**
     * @brief Parse the "{width}x{height}" segment of a tuple id back into integers.
     */
    bool parse_resolution(std::string_view segment, std::uint32_t &width, std::uint32_t &height) {
      auto x = segment.find('x');
      if (x == std::string_view::npos) {
        return false;
      }
      auto width_sv = segment.substr(0, x);
      auto height_sv = segment.substr(x + 1);

      auto parse_uint = [](std::string_view sv, std::uint32_t &out) {
        auto result = std::from_chars(sv.data(), sv.data() + sv.size(), out);
        return result.ec == std::errc {} && result.ptr == sv.data() + sv.size();
      };

      return parse_uint(width_sv, width) && parse_uint(height_sv, height);
    }

  }  // namespace

  std::string make_stable_id(const tuple_key_t &key) {
    return std::format(
      "{}-{}-{}-{}-{}x{}-{}-{}",
      key.backend,
      key.codec,
      key.profile,
      key.chroma,
      key.width,
      key.height,
      key.fps,
      key.hdr ? "hdr" : "sdr"
    );
  }

  namespace {

    /**
     * @brief Best-effort inverse of make_stable_id(), extracting only the
     *        fields needed to find same-resolution alternatives: width,
     *        height, fps, and the hdr flag. Backend/codec/profile/chroma
     *        are intentionally not recovered since alternatives must
     *        differ in at least one of those.
     */
    struct parsed_shape_t {
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      std::uint32_t fps = 0;
      bool hdr = false;
    };

    std::optional<parsed_shape_t> parse_stable_id_shape(std::string_view id) {
      // Fields are hyphen-separated: backend-codec-profile-chroma-WxH-fps-hdr|sdr
      // backend/codec/profile/chroma may themselves be free of hyphens by
      // convention (encoder/codec/profile names never contain '-'), so we
      // can safely split on '-' and take the last three fields plus the
      // WxH field before them.
      std::vector<std::string_view> parts;
      std::size_t start = 0;
      while (start <= id.size()) {
        auto dash = id.find('-', start);
        if (dash == std::string_view::npos) {
          parts.push_back(id.substr(start));
          break;
        }
        parts.push_back(id.substr(start, dash - start));
        start = dash + 1;
      }

      if (parts.size() < 3) {
        return std::nullopt;
      }

      auto hdr_token = parts.back();
      auto fps_token = parts[parts.size() - 2];
      auto resolution_token = parts[parts.size() - 3];

      parsed_shape_t shape;
      if (hdr_token != "hdr" && hdr_token != "sdr") {
        return std::nullopt;
      }
      shape.hdr = hdr_token == "hdr";

      auto fps_result = std::from_chars(fps_token.data(), fps_token.data() + fps_token.size(), shape.fps);
      if (fps_result.ec != std::errc {} || fps_result.ptr != fps_token.data() + fps_token.size()) {
        return std::nullopt;
      }

      if (!parse_resolution(resolution_token, shape.width, shape.height)) {
        return std::nullopt;
      }

      return shape;
    }

  }  // namespace

  store_t &store_t::instance() {
    static store_t instance;
    return instance;
  }

  void store_t::begin_environment(const environment_fingerprint_t &environment) {
    std::lock_guard lock {mutex_};
    if (!environment_ || !(*environment_ == environment)) {
      tuples_.clear();
      environment_ = environment;
    }
  }

  void store_t::record_success(const tuple_key_t &key, std::string_view capture, const environment_fingerprint_t &environment, std::chrono::system_clock::time_point verified_at) {
    std::lock_guard lock {mutex_};

    if (!environment_ || !(*environment_ == environment)) {
      // Environment changed (or this is the first record): every previously
      // proven tuple's evidence is stale.
      tuples_.clear();
      environment_ = environment;
    }

    auto id = make_stable_id(key);
    auto existing = std::ranges::find(tuples_, id, &proven_tuple_t::id);
    if (existing == tuples_.end()) {
      proven_tuple_t entry;
      entry.id = std::move(id);
      entry.key = key;
      entry.environment = environment;
      entry.captures.emplace_back(capture);
      entry.verified_at = verified_at;
      tuples_.push_back(std::move(entry));
      return;
    }

    existing->environment = environment;
    if (std::ranges::find(existing->captures, capture) == existing->captures.end()) {
      existing->captures.emplace_back(capture);
    }
    existing->verified_at = verified_at;
  }

  std::vector<proven_tuple_t> store_t::advertised_tuples() const {
    std::lock_guard lock {mutex_};
    return tuples_;
  }

  std::optional<proven_tuple_t> store_t::find(std::string_view id) const {
    std::lock_guard lock {mutex_};
    auto it = std::ranges::find(tuples_, id, &proven_tuple_t::id);
    if (it == tuples_.end()) {
      return std::nullopt;
    }
    return *it;
  }

  std::vector<std::string> store_t::alternatives_for(std::string_view requested_id) const {
    auto shape = parse_stable_id_shape(requested_id);
    if (!shape) {
      return {};
    }

    std::lock_guard lock {mutex_};
    std::vector<std::string> alternatives;
    for (const auto &tuple : tuples_) {
      if (tuple.id == requested_id) {
        continue;
      }
      if (tuple.key.width == shape->width && tuple.key.height == shape->height && tuple.key.fps == shape->fps && tuple.key.hdr == shape->hdr) {
        alternatives.push_back(tuple.id);
      }
    }

    // Prefer non-HDR/simpler alternatives at the same resolution if the
    // exact HDR shape has no siblings; fall back to any proven tuple at
    // the same resolution/fps regardless of HDR so the client always has
    // something concrete to retry with when one exists.
    if (alternatives.empty()) {
      for (const auto &tuple : tuples_) {
        if (tuple.id == requested_id) {
          continue;
        }
        if (tuple.key.width == shape->width && tuple.key.height == shape->height && tuple.key.fps == shape->fps) {
          alternatives.push_back(tuple.id);
        }
      }
    }

    std::ranges::sort(alternatives);
    return alternatives;
  }

  void store_t::clear() {
    std::lock_guard lock {mutex_};
    tuples_.clear();
    environment_.reset();
  }

}  // namespace jochona::encoder
