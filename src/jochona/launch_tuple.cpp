/**
 * @file src/jochona/launch_tuple.cpp
 * @brief Definitions for the launch-time proven-tuple resolution glue.
 */
#include "launch_tuple.h"

// standard includes
#include <algorithm>
#include <array>
#include <format>
#include <mutex>

// lib includes
#include <nlohmann/json.hpp>

#ifdef _WIN32
  #include <d3d11.h>
  #include <dxgi.h>
  #include <windows.h>
#endif

// local includes
#include "../config.h"
#include "../httpcommon.h"
#include "../logging.h"
#include "../rtsp.h"
#include "../video.h"
#include "display_adapter_client.h"

using namespace std::literals;

namespace jochona::launch {

  std::string to_json(const tuple_rejection_t &rejection) {
    nlohmann::json body;
    body["error"] = rejection.error;
    if (!rejection.requested.empty()) {
      body["requested"] = rejection.requested;
    }
    if (!rejection.stage.empty()) {
      body["stage"] = rejection.stage;
    }
    body["detail"] = rejection.detail;
    if (rejection.error == "encoder_tuple_unavailable") {
      body["alternatives"] = rejection.alternatives;
    }
    return body.dump();
  }

  tuple_rejection_t host_busy_rejection() {
    tuple_rejection_t rejection;
    rejection.error = "host_busy";
    rejection.detail = "An app is already running on this host";
    return rejection;
  }

  encoder::environment_fingerprint_t current_environment_fingerprint(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t fps,
    bool hdr
  ) {
    encoder::environment_fingerprint_t fingerprint;
    fingerprint.gpu = config::video.adapter_name.empty() ? "unspecified" : config::video.adapter_name;
    fingerprint.driver = "unknown";
    fingerprint.display_mode = std::format("{}x{}@{}-{}", width, height, fps, hdr ? "hdr" : "sdr");
    fingerprint.host_build = PROJECT_VERSION;

#ifdef _WIN32
    IDXGIFactory1 *factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))) && factory) {
      IDXGIAdapter1 *adapter = nullptr;
      for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc {};
        bool matches = config::video.adapter_name.empty();
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !matches) {
          const std::wstring configured(config::video.adapter_name.begin(), config::video.adapter_name.end());
          matches = std::wstring(desc.Description) == configured;
        }
        if (matches) {
          LARGE_INTEGER version {};
          if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(ID3D11Device), &version))) {
            fingerprint.driver = std::format("{:016x}", static_cast<std::uint64_t>(version.QuadPart));
          }
          adapter->Release();
          adapter = nullptr;
          break;
        }
        adapter->Release();
        adapter = nullptr;
      }
      factory->Release();
    }
#endif

    auto adapter_status = display_adapter::client_t::instance().probe();
    if (adapter_status.installed && adapter_status.healthy) {
      fingerprint.virtual_display_adapter_version = std::format("{}.{}", adapter_status.protocol_major, adapter_status.protocol_minor);
    } else {
      fingerprint.virtual_display_adapter_version = "not-installed";
    }

    return fingerprint;
  }

  namespace {

    void record_one(
      const std::string &backend,
      const std::string &codec,
      const std::string &profile,
      const std::string &chroma,
      std::uint32_t width,
      std::uint32_t height,
      std::uint32_t fps,
      bool hdr,
      std::string_view capture,
      const encoder::environment_fingerprint_t &environment
    ) {
      video::config_t candidate {};
      candidate.width = static_cast<int>(width);
      candidate.height = static_cast<int>(height);
      candidate.framerate = static_cast<int>(fps);
      candidate.bitrate = static_cast<int>(std::clamp<std::uint64_t>(
        static_cast<std::uint64_t>(width) * height * fps / 50000,
        6000,
        150000
      ));
      candidate.slicesPerFrame = 1;
      candidate.encoderCscMode = hdr ? 3 : 1;
      candidate.videoFormat = codec == "h264" ? 0 : codec == "hevc" ? 1 :
                                                                      2;
      candidate.dynamicRange = profile == "main10" ? 1 : 0;
      candidate.chromaSamplingType = chroma == "444" ? 1 : 0;

      if (!video::probe_encoder_config(candidate)) {
        return;
      }

      encoder::tuple_key_t key;
      key.backend = backend;
      key.codec = codec;
      key.profile = profile;
      key.chroma = chroma;
      key.width = width;
      key.height = height;
      key.fps = fps;
      key.hdr = hdr;
      encoder::store_t::instance().record_success(key, capture, environment);
    }

  }  // namespace

  void record_probe_success(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t fps,
    bool stream_hdr,
    bool capture_virtual
  ) {
    auto backend_view = video::current_encoder_name();
    if (backend_view.empty()) {
      return;
    }

    std::string backend {backend_view};
    std::string_view capture = capture_virtual ? "virtual" : "physical";
    auto environment = current_environment_fingerprint(width, height, fps, stream_hdr);
    encoder::store_t::instance().begin_environment(environment);

    if (!stream_hdr) {
      record_one(backend, "h264", "main8", "420", width, height, fps, false, capture, environment);
      if (video::last_encoder_probe_supported_yuv444_for_codec[0]) {
        record_one(backend, "h264", "main8", "444", width, height, fps, false, capture, environment);
      }

      if (video::active_hevc_mode >= 2) {
        record_one(backend, "hevc", "main8", "420", width, height, fps, false, capture, environment);
        if (video::last_encoder_probe_supported_yuv444_for_codec[1]) {
          record_one(backend, "hevc", "main8", "444", width, height, fps, false, capture, environment);
        }
      }

      if (video::active_av1_mode >= 2) {
        record_one(backend, "av1", "main8", "420", width, height, fps, false, capture, environment);
        if (video::last_encoder_probe_supported_yuv444_for_codec[2]) {
          record_one(backend, "av1", "main8", "444", width, height, fps, false, capture, environment);
        }
      }
      return;
    }

    if (video::active_hevc_mode == 3 || video::active_hevc_mode == 5) {
      record_one(backend, "hevc", "main10", "420", width, height, fps, true, capture, environment);
    }
    if ((video::active_hevc_mode == 4 || video::active_hevc_mode == 5) && video::last_encoder_probe_supported_yuv444_for_codec[1]) {
      record_one(backend, "hevc", "main10", "444", width, height, fps, true, capture, environment);
    }

    if (video::active_av1_mode == 3 || video::active_av1_mode == 5) {
      record_one(backend, "av1", "main10", "420", width, height, fps, true, capture, environment);
    }
    if ((video::active_av1_mode == 4 || video::active_av1_mode == 5) && video::last_encoder_probe_supported_yuv444_for_codec[2]) {
      record_one(backend, "av1", "main10", "444", width, height, fps, true, capture, environment);
    }
  }

  std::optional<tuple_rejection_t> resolve_requested_tuple(std::string_view requested_id, const rtsp_stream::launch_session_t &session, bool capture_virtual) {
    auto &store = encoder::store_t::instance();
    auto proven = store.find(requested_id);

    auto reject = [&](std::string detail) {
      tuple_rejection_t rejection;
      rejection.error = "encoder_tuple_unavailable";
      rejection.requested = std::string {requested_id};
      rejection.stage = "encoder_initialize";
      rejection.detail = std::move(detail);
      rejection.alternatives = store.alternatives_for(requested_id);
      return rejection;
    };

    if (!proven) {
      return reject("The requested encoder tuple has not been proven on this host.");
    }

    const auto &key = proven->key;
    if (key.width != static_cast<std::uint32_t>(session.width) || key.height != static_cast<std::uint32_t>(session.height) || key.fps != static_cast<std::uint32_t>(session.fps)) {
      return reject(std::format("The requested tuple was proven at {}x{}@{}, which does not match the requested stream mode {}x{}@{}.", key.width, key.height, key.fps, session.width, session.height, session.fps));
    }

    if (key.hdr != session.enable_hdr) {
      return reject("The requested tuple's HDR state does not match the requested stream's HDR state.");
    }

    std::string_view requested_capture = capture_virtual ? "virtual" : "physical";
    if (std::ranges::find(proven->captures, requested_capture) == proven->captures.end()) {
      return reject(std::format("The requested tuple has not been proven for {} capture.", requested_capture));
    }

    // Pin session codec selection: narrow the active codec modes so RTSP's
    // codec negotiation with the client cannot silently pick a different
    // codec/HDR level than the one the client pinned via jochonaTuple.
    if (key.codec == "h264") {
      video::active_hevc_mode = 1;
      video::active_av1_mode = 1;
    } else if (key.codec == "hevc") {
      video::active_av1_mode = 1;
      if (key.chroma == "444" && key.hdr) {
        video::active_hevc_mode = 4;
      } else if (key.hdr) {
        video::active_hevc_mode = 3;
      } else {
        video::active_hevc_mode = 2;
      }
    } else if (key.codec == "av1") {
      video::active_hevc_mode = 1;
      if (key.chroma == "444" && key.hdr) {
        video::active_av1_mode = 4;
      } else if (key.hdr) {
        video::active_av1_mode = 3;
      } else {
        video::active_av1_mode = 2;
      }
    }

    return std::nullopt;
  }

  namespace {

    std::mutex virtual_lease_mutex;
    std::optional<display_adapter::lease_handle_t> virtual_lease;  ///< Held for the lifetime of the one active session, if it requested a virtual display.

    /**
     * @brief Decode this Host's stable http::unique_id (canonical UUID text)
     *        back into 16 raw bytes to use as the adapter lease owner id.
     */
    std::array<std::uint8_t, 16> host_owner_id() {
      std::array<std::uint8_t, 16> bytes {};
      std::size_t byte_index = 0;
      int nibble = -1;
      for (char c : http::unique_id) {
        int value;
        if (c >= '0' && c <= '9') {
          value = c - '0';
        } else if (c >= 'a' && c <= 'f') {
          value = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
          value = c - 'A' + 10;
        } else {
          continue;  // skip hyphens and any other separators
        }
        if (nibble < 0) {
          nibble = value;
        } else {
          if (byte_index < bytes.size()) {
            bytes[byte_index++] = static_cast<std::uint8_t>((nibble << 4) | value);
          }
          nibble = -1;
        }
        if (byte_index >= bytes.size()) {
          break;
        }
      }
      return bytes;
    }

#ifdef _WIN32
    /**
     * @brief Best-effort DXGI LUID lookup for config::video.adapter_name.
     *
     * Returns std::nullopt when DXGI enumeration fails or no adapter
     * matches; pinning the render adapter LUID is a best-effort placement
     * optimization, not a precondition for leasing a virtual display slot.
     */
    std::optional<LUID> resolve_configured_adapter_luid() {
      IDXGIFactory1 *factory = nullptr;
      if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))) || !factory) {
        return std::nullopt;
      }

      std::optional<LUID> result;
      IDXGIAdapter1 *adapter = nullptr;
      for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc {};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
          auto description = std::wstring(desc.Description);
          bool matches = config::video.adapter_name.empty();
          if (!matches) {
            std::wstring configured(config::video.adapter_name.begin(), config::video.adapter_name.end());
            matches = description == configured;
          }
          if (matches && !result) {
            result = desc.AdapterLuid;
          }
        }
        adapter->Release();
        adapter = nullptr;
      }
      factory->Release();
      return result;
    }
#endif

  }  // namespace

  std::optional<tuple_rejection_t> acquire_virtual_display_for_session(const rtsp_stream::launch_session_t &session) {
    auto &client = display_adapter::client_t::instance();

    auto reject = [&](std::string detail) {
      tuple_rejection_t rejection;
      rejection.error = "encoder_tuple_unavailable";
      rejection.stage = "virtual_display_lease";
      rejection.detail = std::move(detail);
      return rejection;
    };

    auto lease_result = client.lease(host_owner_id());
    if (auto *error = std::get_if<display_adapter::error_e>(&lease_result)) {
      return reject(std::format("Could not lease the Jochona Display Adapter's virtual display slot: {}.", display_adapter::to_string(*error)));
    }
    auto lease = std::move(std::get<display_adapter::lease_handle_t>(lease_result));

#ifdef _WIN32
    if (auto luid = resolve_configured_adapter_luid()) {
      client.set_render_adapter_luid(luid->LowPart, luid->HighPart);
    } else {
      BOOST_LOG(warning) << "Jochona: could not resolve a DXGI LUID for the configured render adapter; the display adapter will use its own default."sv;
    }
#endif

    display_adapter::slot_mode_t mode;
    mode.width = static_cast<std::uint32_t>(session.width);
    mode.height = static_cast<std::uint32_t>(session.height);
    mode.refresh_numerator = static_cast<std::uint32_t>(session.fps);
    mode.refresh_denominator = 1;
    mode.bits_per_channel = session.enable_hdr ? 10 : 8;
    mode.hdr_enabled = session.enable_hdr;

    if (auto error = lease.configure(mode)) {
      return reject(std::format("Could not configure the Jochona Display Adapter's virtual display slot at {}x{}@{}: {}.", session.width, session.height, session.fps, display_adapter::to_string(*error)));
    }

    std::lock_guard lock {virtual_lease_mutex};
    virtual_lease = std::move(lease);
    return std::nullopt;
  }

  void release_active_virtual_display_lease() {
    std::lock_guard lock {virtual_lease_mutex};
    if (virtual_lease) {
      virtual_lease->release();
      virtual_lease.reset();
    }
  }

  bool virtual_display_lease_active() {
    std::lock_guard lock {virtual_lease_mutex};
    return virtual_lease.has_value() && virtual_lease->valid();
  }

}  // namespace jochona::launch
