// sherpa-onnx/csrc/offline-tts-vits-model-config.h
//
// Copyright (c)  2023  Xiaomi Corporation

#ifndef SHERPA_ONNX_CSRC_OFFLINE_TTS_VITS_MODEL_CONFIG_H_
#define SHERPA_ONNX_CSRC_OFFLINE_TTS_VITS_MODEL_CONFIG_H_

#include <string>

#include "sherpa-onnx/csrc/parse-options.h"

namespace sherpa_onnx {

struct OfflineTtsVitsModelConfig {
  std::string model;
  std::string lexicon;
  std::string tokens;

  // If data_dir is given, lexicon is ignored
  // data_dir is for piper-phonemize, which uses espeak-ng
  std::string data_dir;

  // Used for Chinese TTS models using jieba
  std::string dict_dir;

  float noise_scale = 0.667;
  float noise_scale_w = 0.8;
  float length_scale = 1;

  // korean for tokenizer & ja_bert
  std::string ja_bert_model;
  std::string vocab;

  // used only for multi-speaker models, e.g, vctk speech dataset.
  // Not applicable for single-speaker models, e.g., ljspeech dataset

  OfflineTtsVitsModelConfig() = default;

  OfflineTtsVitsModelConfig(const std::string &model,
                            const std::string &lexicon,
                            const std::string &tokens,
                            const std::string &data_dir,
                            const std::string &dict_dir,
                            const std::string &ja_bert_model,
                            const std::string &vocab,
                             float noise_scale = 0.667,
                            float noise_scale_w = 0.8, float length_scale = 1

                            )
      : model(model),
        lexicon(lexicon),
        tokens(tokens),
        data_dir(data_dir),
        dict_dir(dict_dir),
        noise_scale(noise_scale),
        noise_scale_w(noise_scale_w),
        length_scale(length_scale),
        ja_bert_model(ja_bert_model),
        vocab(vocab) {}

  void Register(ParseOptions *po);
  bool Validate() const;

  std::string ToString() const;
};

}  // namespace sherpa_onnx

#endif  // SHERPA_ONNX_CSRC_OFFLINE_TTS_VITS_MODEL_CONFIG_H_
