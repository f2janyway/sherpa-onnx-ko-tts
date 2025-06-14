// sherpa-onnx/csrc/offline-tts-impl.cc
//
// Copyright (c)  2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/offline-tts-impl.h"

#include <memory>
#include <vector>

#if __ANDROID_API__ >= 9
#include "android/asset_manager.h"
#include "android/asset_manager_jni.h"
#endif

#if __OHOS__
#include "rawfile/raw_file_manager.h"
#endif

#include "sherpa-onnx/csrc/offline-tts-kokoro-impl.h"
#include "sherpa-onnx/csrc/offline-tts-matcha-impl.h"
#include "sherpa-onnx/csrc/offline-tts-vits-impl.h"

namespace sherpa_onnx {

std::vector<int64_t> OfflineTtsImpl::AddBlank(const std::vector<int64_t> &x,
                                              int32_t blank_id /*= 0*/) const {
  SHERPA_ONNX_LOGE(
      ">>>> OfflineTtsImpl::AddBlank  csrc/offline-tts-impl.cc start");
  // we assume the blank ID is 0
  std::vector<int64_t> buffer(x.size() * 2 + 1, blank_id);
  int32_t i = 1;
  for (auto k : x) {
    buffer[i] = k;
    i += 2;
  }
  SHERPA_ONNX_LOGE(
      ">>>> OfflineTtsImpl::AddBlank  csrc/offline-tts-impl.cc end");
  return buffer;
}

std::unique_ptr<OfflineTtsImpl> OfflineTtsImpl::Create(
    const OfflineTtsConfig &config) {
  SHERPA_ONNX_LOGE(
      ">>>> OfflineTtsImpl::Create csrc/offline-tts-impl.cc start");
  if (!config.model.vits.model.empty()) {
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsImpl::Create vits  csrc/offline-tts-impl.cc end");
    return std::make_unique<OfflineTtsVitsImpl>(config);
  } else if (!config.model.matcha.acoustic_model.empty()) {
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsImpl::Create matcha  csrc/offline-tts-impl.cc end");
    return std::make_unique<OfflineTtsMatchaImpl>(config);
  }
  SHERPA_ONNX_LOGE(
      ">>>> OfflineTtsImpl::Create kokoro  csrc/offline-tts-impl.cc end");
  return std::make_unique<OfflineTtsKokoroImpl>(config);
}

template <typename Manager>
std::unique_ptr<OfflineTtsImpl> OfflineTtsImpl::Create(
    Manager *mgr, const OfflineTtsConfig &config) {
  SHERPA_ONNX_LOGE(
      ">>>> OfflineTtsImpl::Create csrc/offline-tts-impl.cc start");
  if (!config.model.vits.model.empty()) {
    SHERPA_ONNX_LOGE( ">>>> OfflineTtsImpl::Create vits  csrc/offline-tts-impl.cc end");
    return std::make_unique<OfflineTtsVitsImpl>(mgr, config);
  } else if (!config.model.matcha.acoustic_model.empty()) {
    SHERPA_ONNX_LOGE( ">>>> OfflineTtsImpl::Create matcha  csrc/offline-tts-impl.cc end");
    return std::make_unique<OfflineTtsMatchaImpl>(mgr, config);
  }

  SHERPA_ONNX_LOGE( ">>>> OfflineTtsImpl::Create kokoro  csrc/offline-tts-impl.cc end");
  return std::make_unique<OfflineTtsKokoroImpl>(mgr, config);
}

#if __ANDROID_API__ >= 9
template std::unique_ptr<OfflineTtsImpl> OfflineTtsImpl::Create(
    AAssetManager *mgr, const OfflineTtsConfig &config);
#endif

#if __OHOS__
template std::unique_ptr<OfflineTtsImpl> OfflineTtsImpl::Create(
    NativeResourceManager *mgr, const OfflineTtsConfig &config);
#endif

}  // namespace sherpa_onnx
