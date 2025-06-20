// sherpa-onnx/csrc/offline-tts-model-config.cc
//
// Copyright (c)  2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/offline-tts-model-config.h"

#include "sherpa-onnx/csrc/macros.h"

namespace sherpa_onnx {

void OfflineTtsModelConfig::Register(ParseOptions *po) {
  SHERPA_ONNX_LOGE(">>>> OfflineTtsModelConfig::Register csrc/offline-tts-model-config.cc start");
  vits.Register(po);
  matcha.Register(po);
  kokoro.Register(po);

  po->Register("num-threads", &num_threads,
               "Number of threads to run the neural network");

  po->Register("debug", &debug,
               "true to print model information while loading it.");

  po->Register("provider", &provider,
               "Specify a provider to use: cpu, cuda, coreml");
  SHERPA_ONNX_LOGE(">>>> OfflineTtsModelConfig::Register csrc/offline-tts-model-config.cc end");
}

bool OfflineTtsModelConfig::Validate() const {
  SHERPA_ONNX_LOGE(">>>> OfflineTtsModelConfig::Validate csrc/offline-tts-model-config.cc start");
  if (num_threads < 1) {
    SHERPA_ONNX_LOGE("num_threads should be > 0. Given %d", num_threads);
    return false;
  }

  if (!vits.model.empty()) {
    return vits.Validate();
  }

  if (!matcha.acoustic_model.empty()) {
    return matcha.Validate();
  }

  SHERPA_ONNX_LOGE(">>>> OfflineTtsModelConfig::Validate csrc/offline-tts-model-config.cc end");
  return kokoro.Validate();
}

std::string OfflineTtsModelConfig::ToString() const {
  std::ostringstream os;

  os << "OfflineTtsModelConfig(";
  os << "vits=" << vits.ToString() << ", ";
  os << "matcha=" << matcha.ToString() << ", ";
  os << "kokoro=" << kokoro.ToString() << ", ";
  os << "num_threads=" << num_threads << ", ";
  os << "debug=" << (debug ? "True" : "False") << ", ";
  os << "provider=\"" << provider << "\")";

  return os.str();
}

}  // namespace sherpa_onnx
