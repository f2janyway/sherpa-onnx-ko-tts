// sherpa-onnx/csrc/offline-tts-frontend.h
//
// Copyright (c)  2023  Xiaomi Corporation

#ifndef SHERPA_ONNX_CSRC_OFFLINE_TTS_FRONTEND_H_
#define SHERPA_ONNX_CSRC_OFFLINE_TTS_FRONTEND_H_
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sherpa-onnx/csrc/macros.h"

namespace sherpa_onnx {

/// @brief
/// @param tokens std::vector<int64_t> token_ids
/// @param tones std::vector<int64_t> korean 11 or 0?
/// @param ja_bert_vec std::vector<float>
/// @param sentences std::vector<std::string>
struct TokenIDs {
  TokenIDs() = default;

  /*implicit*/ TokenIDs(std::vector<int64_t> tokens)  // NOLINT
      : tokens{std::move(tokens)} {}

  /*implicit*/ TokenIDs(const std::vector<int32_t> &tokens)  // NOLINT
      : tokens{tokens.begin(), tokens.end()} {}
  TokenIDs(std::vector<int64_t> tokens,  // NOLINT
           std::vector<int64_t> tones
        )   // NOLINT
      : tokens{std::move(tokens)}, tones{std::move(tones)}
       {}
  TokenIDs(std::vector<int64_t> tokens,  // NOLINT
           std::vector<int64_t> tones,
           std::vector<float> ja_bert_vec,
           std::vector<std::string> sentences
          )  // NOLINT
      : tokens{std::move(tokens)},
        tones{std::move(tones)},
        ja_bert_vec{std::move(ja_bert_vec)},
        sentences{std::move(sentences)} {}

  std::string ToString() const;

  std::vector<int64_t> tokens;

  // Used only in MeloTTS
  std::vector<int64_t> tones;

  std::vector<float> ja_bert_vec;
  std::vector<std::string> sentences;
};

class OfflineTtsFrontend {
 public:
  virtual ~OfflineTtsFrontend() = default;

  /** Convert a string to token IDs.
   *
   * @param text The input text.
   *             Example 1: "This is the first sample sentence; this is the
   *             second one." Example 2: "这是第一句。这是第二句。"
   * @param voice Optional. It is for espeak-ng.
   *
   * @return Return a vector-of-vector of token IDs. Each subvector contains
   *         a sentence that can be processed independently.
   *         If a frontend does not support splitting the text into sentences,
   *         the resulting vector contains only one subvector.
   */
  virtual std::vector<TokenIDs> ConvertTextToTokenIds(
      const std::string &text, const std::string &voice = "") const = 0;

  // virtual std::vector<float> GetJaBert(const std::string &text) const {
  //   // 기본 구현: 지원하지 않음을 나타내기 위해 비어있는 벡터를 반환하거나,
  //   // 오류 로그를 남기고 빈 벡터를 반환합니다.
  //   SHERPA_ONNX_LOGE("GetJaBert is not supported by this frontend type.");
  //   // std::cerr << "Warning: GetJaBert is not supported by this frontend type.
  //   // Returning empty vector." << std::endl;
  //   return {};  // 비어있는 벡터 반환
  // }
  // --- 변경된 부분 끝 ---
};

// implementation is in ./piper-phonemize-lexicon.cc
void InitEspeak(const std::string &data_dir);

}  // namespace sherpa_onnx

#endif  // SHERPA_ONNX_CSRC_OFFLINE_TTS_FRONTEND_H_
