/**
 * @file src/jochona/host_volume.cpp
 * @brief Definitions for the Host output-volume read/write control.
 */
#include "host_volume.h"

// standard includes
#include <algorithm>

#ifdef _WIN32
  // platform includes
  #include <endpointvolume.h>
  #include <mmdeviceapi.h>
  #include <windows.h>

  // local includes
  #include "utility.h"
#endif

namespace jochona::host_volume {

#ifdef _WIN32

  namespace {

    /// COM Release() adapter matching util::safe_ptr's deleter-function signature.
    template<class T>
    void release(T *p) {
      p->Release();
    }

    using device_enum_t = util::safe_ptr<IMMDeviceEnumerator, release<IMMDeviceEnumerator>>;
    using device_t = util::safe_ptr<IMMDevice, release<IMMDevice>>;
    using endpoint_volume_t = util::safe_ptr<IAudioEndpointVolume, release<IAudioEndpointVolume>>;

    /**
     * @brief RAII COM apartment guard scoped to one get()/set() call.
     *
     * SimpleWeb's HTTP handler threads are pooled and not guaranteed to
     * have an existing COM apartment, unlike Sunshine's dedicated audio
     * capture thread; each call initializes and tears down its own.
     */
    struct com_scope_t {
      HRESULT hr;

      com_scope_t():
          hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
      }

      ~com_scope_t() {
        if (SUCCEEDED(hr)) {
          CoUninitialize();
        }
      }

      /// True if this thread has (or already had) a usable COM apartment.
      [[nodiscard]] bool ok() const {
        // RPC_E_CHANGED_MODE means the thread already has an apartment of a
        // different concurrency model, which is still usable for our
        // purposes; only a hard failure should stop us.
        return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
      }
    };

    /**
     * @brief Get the IAudioEndpointVolume interface for the default render endpoint.
     */
    endpoint_volume_t default_endpoint_volume() {
      device_enum_t device_enum;
      if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void **) &device_enum))) {
        return {};
      }

      device_t device;
      if (FAILED(device_enum->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
        return {};
      }

      endpoint_volume_t endpoint_volume;
      if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **) &endpoint_volume))) {
        return {};
      }

      return endpoint_volume;
    }

  }  // namespace

  status_t get() {
    status_t status;

    com_scope_t com;
    if (!com.ok()) {
      return status;
    }

    auto endpoint_volume = default_endpoint_volume();
    if (!endpoint_volume) {
      return status;
    }

    float scalar = 0.0f;
    if (FAILED(endpoint_volume->GetMasterVolumeLevelScalar(&scalar))) {
      return status;
    }

    status.available = true;
    status.min = 0;
    status.max = 100;
    status.current = std::clamp(static_cast<int>(scalar * 100.0f + 0.5f), 0, 100);
    return status;
  }

  bool set(int level_0_100) {
    auto clamped = std::clamp(level_0_100, 0, 100);

    com_scope_t com;
    if (!com.ok()) {
      return false;
    }

    auto endpoint_volume = default_endpoint_volume();
    if (!endpoint_volume) {
      return false;
    }

    return SUCCEEDED(endpoint_volume->SetMasterVolumeLevelScalar(static_cast<float>(clamped) / 100.0f, nullptr));
  }

#else  // !_WIN32

  status_t get() {
    // No cross-platform host-volume backend exists yet; Linux/macOS support
    // follows the same seam once a PulseAudio/CoreAudio backend is added.
    return {};
  }

  bool set(int) {
    return false;
  }

#endif  // _WIN32

  nlohmann::json to_json(const status_t &status) {
    nlohmann::json body;
    body["available"] = status.available;
    body["min"] = status.available ? nlohmann::json(status.min) : nlohmann::json(nullptr);
    body["max"] = status.available ? nlohmann::json(status.max) : nlohmann::json(nullptr);
    body["current"] = status.available ? nlohmann::json(status.current) : nlohmann::json(nullptr);
    return body;
  }

  std::string to_json(const rejection_t &rejection) {
    nlohmann::json body;
    body["error"] = rejection.error;
    body["detail"] = rejection.detail;
    return body.dump();
  }

}  // namespace jochona::host_volume
