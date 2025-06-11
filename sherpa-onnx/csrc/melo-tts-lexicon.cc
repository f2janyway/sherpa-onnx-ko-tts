// sherpa-onnx/csrc/melo-tts-lexicon.cc
//
// Copyright (c)  2022-2024  Xiaomi Corporation

#include "sherpa-onnx/csrc/melo-tts-lexicon.h"

#include <fstream>
#include <regex>  // NOLINT
#include <sstream>
#include <strstream>
#include <unordered_map>
#include <utility>
#if __ANDROID_API__ >= 9
#include "android/asset_manager.h"
#include "android/asset_manager_jni.h"
#endif

#if __OHOS__
#include "rawfile/raw_file_manager.h"
#endif

#include <iostream>
#include <stdexcept>  // std::out_of_range 사용을 위해 포함
#include <string>
#include <vector>

#include "sherpa-onnx/csrc/file-utils.h"
#include "sherpa-onnx/csrc/jieba.h"
#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/melo-tts-ko-tokenizer.h"
#include "sherpa-onnx/csrc/melo-tts-ko.h"
#include "sherpa-onnx/csrc/onnx-utils.h"
#include "sherpa-onnx/csrc/symbol-table.h"
#include "sherpa-onnx/csrc/text-utils.h"

const std::vector<std::string> CHOSUNG = {
    "ᄀ", "ᄁ", "ᄂ", "ᄃ", "ᄄ", "ᄅ", "ᄆ", "ᄇ", "ᄈ", "ᄉ",
    "ᄊ", "ᄋ", "ᄌ", "ᄍ", "ᄎ", "ᄏ", "ᄐ", "ᄑ", "ᄒ",
};

const std::vector<std::string> JUNGSUNG = {
    "ᅡ", "ᅢ", "ᅣ", "ᅤ", "ᅥ", "ᅦ", "ᅧ", "ᅨ", "ᅩ", "ᅪ", "ᅫ",
    "ᅬ", "ᅭ", "ᅮ", "ᅯ", "ᅰ", "ᅱ", "ᅲ", "ᅳ", "ᅴ", "ᅵ",
};

const std::vector<std::string> JONGSUNG = {
    "",  "ᆨ", "ᆩ", "ᆪ", "ᆫ", "ᆬ", "ᆭ", "ᆮ", "ᆯ", "ᆰ", "ᆱ", "ᆲ", "ᆳ", "ᆴ",
    "ᆵ", "ᆶ", "ᆷ", "ᆸ", "ᆹ", "ᆺ", "ᆻ", "ᆼ", "ᆽ", "ᆾ", "ᆿ", "ᇀ", "ᇁ", "ᇂ",
};
// --- 유니코드 코드 포인트 추출 헬퍼 함수 (이전 답변과 동일) ---
// This function remains the same as it correctly extracts a codepoint and
// advances the index.
long get_codepoint_at(const std::string &s, size_t &index) {
  if (index >= s.length()) {
    return -1;
  }

  unsigned char c = static_cast<unsigned char>(s[index]);
  SHERPA_ONNX_LOGE(">>> get_codepoint_at called with index: %zu, char: %c",
                   index, c);
  long codepoint;
  int len;

  if (c < 0x80) {  // 1-byte sequence (ASCII)
    codepoint = c;
    len = 1;
  } else if ((c & 0xE0) == 0xC0) {           // 2-byte sequence
    if (index + 1 >= s.length()) return -1;  // Check bounds before accessing
    codepoint = ((c & 0x1F) << 6) | (s[index + 1] & 0x3F);
    len = 2;
  } else if ((c & 0xF0) == 0xE0) {  // 3-byte sequence (most common for Korean)
    if (index + 2 >= s.length()) return -1;  // Check bounds before accessing
    codepoint = ((c & 0x0F) << 12) | ((s[index + 1] & 0x3F) << 6) |
                (s[index + 2] & 0x3F);
    len = 3;
  } else if ((c & 0xF8) == 0xF0) {           // 4-byte sequence
    if (index + 3 >= s.length()) return -1;  // Check bounds before accessing
    codepoint = ((c & 0x07) << 18) | ((s[index + 1] & 0x3F) << 12) |
                ((s[index + 2] & 0x3F) << 6) | (s[index + 3] & 0x3F);
    len = 4;
  } else {       // Invalid start byte, skip it
    index += 1;  // Advance by 1 to avoid infinite loop on invalid bytes
    return -1;
  }

  index += len;  // Update index to point to the next character
  SHERPA_ONNX_LOGE(
      ">>> get_codepoint_at called with index: %zu, char: %c return codepoint: "
      "%ld",
      index, c, codepoint);
  return codepoint;
}

namespace JamoUtils {

// --- MODIFIED splitOne function ---
// This function should take the already-parsed Unicode codepoint
// and the original string representation of that character for fallback.
std::vector<std::string> splitOne(long codePoint,
                                  const std::string &original_char_str) {
  // Check if the codepoint is within the Hangul Syllable block range
  if (codePoint >= 0xAC00 && codePoint <= 0xD7A3) {
    long startValue = codePoint - 0xAC00;
    long jong = startValue % 28;
    long jung = (startValue / 28) % 21;
    long cho = (startValue / 28) / 21;

    // It's crucial to have `CHOSUNG`, `JUNGSUNG`, and `JONGSUNG`
    // defined and accessible here, typically as global or static const
    // arrays/vectors. Example (you need to define these properly in your code):
    // extern const std::vector<std::string> CHOSUNG;
    // extern const std::vector<std::string> JUNGSUNG;
    // extern const std::vector<std::string> JONGSUNG;

    // Defensive check to ensure calculated indices are within bounds of Jamo
    // arrays
    if (static_cast<size_t>(cho) >= CHOSUNG.size() ||
        static_cast<size_t>(jung) >= JUNGSUNG.size() ||
        static_cast<size_t>(jong) >= JONGSUNG.size()) {
      // This case should ideally not be hit if the Unicode decomposition logic
      // is sound and your Jamo arrays are complete. But it's a good safeguard.
      SHERPA_ONNX_LOGE(
          "Jamo index out of bounds! codePoint: %ld, cho: %ld, jung: %ld, "
          "jong: %ld",
          codePoint, cho, jung, jong);
      return {original_char_str, "",
              ""};  // Fallback: return the original character
    }

    return {CHOSUNG[cho], JUNGSUNG[jung], JONGSUNG[jong]};
  } else {
    // If it's not a Hangul syllable, return the character itself and two empty
    // strings. This handles English letters, numbers, punctuation, or
    // standalone Jamo characters that aren't part of a syllable block.
    return {original_char_str, "", ""};
  }
}

// --- split function ---
std::vector<std::string> split(const std::string &target) {
  std::vector<std::string> result_jamos;
  size_t i = 0;

  SHERPA_ONNX_LOGE(">>> split called with target: %s", target.c_str());
  SHERPA_ONNX_LOGE(">>> initial i: %zu", i);

  while (i < target.length()) {
    SHERPA_ONNX_LOGE(">>> while loop i: %zu", i);
    size_t start_idx = i;

    // Call get_codepoint_at to parse ONE character and advance 'i'
    long codePoint = get_codepoint_at(target, i);

    if (codePoint == -1) {
      // If get_codepoint_at returns -1 (e.g., invalid UTF-8 sequence or
      // end of string during multi-byte read), it has already advanced
      // 'i' by 1 for invalid start bytes or left it at `s.length()`
      // for boundary checks. Just continue to the next iteration.
      continue;
    }

    // Extract the substring for the character that was just processed.
    // 'i' has been advanced by get_codepoint_at, so (i - start_idx) gives the
    // length.
    std::string current_char_str = target.substr(start_idx, i - start_idx);

    // --- CRUCIAL FIX HERE ---
    // Pass the extracted 'codePoint' directly to splitOne.
    // Also pass the 'current_char_str' for fallback if decomposition isn't
    // possible.
    std::vector<std::string> split_char_result =
        splitOne(codePoint, current_char_str);

    // Add the decomposed Jamo (or the original character if not decomposable)
    // to the result vector.
    int8_t cnt = 0;
    SHERPA_ONNX_LOGE(">>> splitOne returned %zu jamos",
                     split_char_result.size());
    for (const auto &jamo : split_char_result) {
      // We push back empty strings as well, consistent with the original Kotlin
      // logic.
      if (jamo == "") {
        SHERPA_ONNX_LOGE(
            ">>> splitOne returned an empty string, skipping. cnt: %d", cnt);
        continue;  // Skip empty strings
      }
      result_jamos.push_back(jamo);
      cnt++;
    }
  }
  return result_jamos;
}

}  // namespace JamoUtils

namespace sherpa_onnx {

class MeloTtsLexicon::Impl {
 public:
  Impl(const std::string &lexicon, const std::string &tokens,
       const std::string &dict_dir,
       const OfflineTtsVitsModelMetaData &meta_data, bool debug)
      : meta_data_(meta_data), debug_(debug) {
    jieba_ = InitJieba(dict_dir);

    {
      std::ifstream is(tokens);
      InitTokens(is);
    }

    {
      std::ifstream is(lexicon);
      InitLexicon(is);
    }
  }

  Impl(const std::string &lexicon, const std::string &tokens,
       const OfflineTtsVitsModelMetaData &meta_data, bool debug)
      : meta_data_(meta_data), debug_(debug) {
    {
      std::ifstream is(tokens);
      InitTokens(is);
    }

    {
      std::ifstream is(lexicon);
      InitLexicon(is);
    }
  }

  template <typename Manager>
  Impl(Manager *mgr, const std::string &lexicon, const std::string &tokens,
       const std::string &dict_dir,
       const OfflineTtsVitsModelMetaData &meta_data, bool debug)
      : meta_data_(meta_data), debug_(debug) {
    jieba_ = InitJieba(dict_dir);

    {
      auto buf = ReadFile(mgr, tokens);

      std::istrstream is(buf.data(), buf.size());
      InitTokens(is);
    }

    {
      auto buf = ReadFile(mgr, lexicon);

      std::istrstream is(buf.data(), buf.size());
      InitLexicon(is);
    }
  }

  template <typename Manager>
  Impl(Manager *mgr, const std::string &lexicon, const std::string &tokens,
       const OfflineTtsVitsModelMetaData &meta_data, bool debug)
      : meta_data_(meta_data), debug_(debug) {
    {
      auto buf = ReadFile(mgr, tokens);

      std::istrstream is(buf.data(), buf.size());
      InitTokens(is);
    }

    {
      auto buf = ReadFile(mgr, lexicon);

      std::istrstream is(buf.data(), buf.size());
      InitLexicon(is);
    }
  }
  template <typename Manager>
  Impl(Manager *mgr, const std::string &lexicon, const std::string &tokens,
       const std::string &ja_bert_model_path, const std::string &vocab_path,
       const OfflineTtsVitsModelMetaData &meta_data, bool debug) {
    {
      
      auto buf = ReadFile(mgr, tokens);
      // ja_bert_model_path_ = ja_bert_model_path;
      // vocab_path_ = vocab_path;
      SHERPA_ONNX_LOGE(
          ">>>>> MeloTtsLexicon::Impl::Impl vocab_path_ %s,ja_bert_model_path_ "
          "%s",
          vocab_path.c_str(), ja_bert_model_path.c_str());
      auto vocab_buf = ReadFile(mgr, vocab_path);
      std::istrstream is_v(vocab_buf.data(), vocab_buf.size());
      InitTokenizer(is_v);

      // currently dont used this init tokens but just hardcoded tokens
      std::istrstream is(buf.data(), buf.size());
      InitTokens(is);
    }

    {
      auto buf = ReadFile(mgr, lexicon);

      std::istrstream is(buf.data(), buf.size());
      InitLexicon(is);
    }

    {
      auto buf = ReadFile(mgr, ja_bert_model_path);
      // std::istrstream is();
      InitJaBert(buf.data(), buf.size());
    }
  }

  // --- 자모 분리 로직 ---

  /// @brief vector means split sentence TokenIds contains token_ids, tone_ids
  /// and ja_bert_vec
  ///
  /// struct TokenIDs {
  ///
  /// tokens std::vector<int64_t> token_ids
  ///
  /// tones std::vector<int64_t> korean 11 or 0?
  ///
  /// ja_bert_vec std::vector<float>
  ///
  /// ...
  /// }
  /// @param _text
  /// @return  std::vector<TokenIDs>
  std::vector<TokenIDs> ConvertTextToTokenIdsKorean(
      const std::string &_text) const {
    SHERPA_ONNX_LOGE("ConvertTextToTokenIdsKorean called with text: %s",
                     _text.c_str());
    std::vector<std::string> words;
    // words = SplitUtf8(_text);
    words = split_sentences_ko(_text);
    for (const auto &word : words) {
      SHERPA_ONNX_LOGE(">>>> ConvertTextToTokenIdsKorean word: %s",
                       word.c_str());
    }

    std::vector<TokenIDs> ans;
    TokenIDs this_sentence;

    for (const auto &word : words) {
      SHERPA_ONNX_LOGE("ConvertTextToTokenIdsKorean word: %s", word.c_str());
      // split text if long text
      // std::vector<std::int64_t> phoneIds = TextToPhoneId(_text);

      // std::vector<float> ja_bert_vec_final;
      SHERPA_ONNX_LOGE(
          ">>>> ConvertTextToTokenIdsKorean melo-tts-lexicon.cc start g2pk");
      G2PResult g2p_result = g2pk(word, *tokenizer_kor_);
      SHERPA_ONNX_LOGE(
          ">>>> ConvertTextToTokenIdsKorean melo-tts-lexicon.cc end g2pk");

      std::vector<std::int64_t> phoneIds = g2p_result.phone_ids;
      std::vector<std::int64_t> word2ph = g2p_result.word2ph;
      std::vector<std::string> phones = g2p_result.phones;
      {
        SHERPA_ONNX_LOGE(">>>> ConvertTextToTokenIdsKorean G2pResult");
        // size
        SHERPA_ONNX_LOGE(
            ">>>> ConvertTextToTokenIdsKorean G2pResult phoneIds size: %zu, "
            "phones size: %zu",
            phoneIds.size(), phones.size());
        std::ostringstream phone_stream;
        int idx = 0;
        for (auto count : word2ph) {
          for (int i = 0; i < count; i++) {
            phone_stream << phoneIds[idx + i] << ",";
          }
          idx += count;
          phone_stream << "\n";
        }
        phone_stream << "\n";
        SHERPA_ONNX_LOGE("%s", phone_stream.str().c_str());
        std::ostringstream ph_stream;
        idx = 0;
        for (auto count : word2ph) {
          for (int i = 0; i < count; i++) {
            ph_stream << phones[idx + i] << ",";
          }
          idx += count;
          ph_stream << "\n";
        }
        ph_stream << "\n";

        SHERPA_ONNX_LOGE("%s", ph_stream.str().c_str());
        std::ostringstream word2ph_stream;
        for (auto e : word2ph) {
          word2ph_stream << e << ",";
        }
        SHERPA_ONNX_LOGE(
            ">>>> ConvertTextToTokenIdsKorean G2pResult word2ph 0 :%s",
            word2ph_stream.str().c_str());
        SHERPA_ONNX_LOGE(
            ">>>> ConvertTextToTokenIdsKorean G2pResult word2ph size: %zu",
            word2ph.size());
        // SHERPA_ONNX_LOGE(
        //     ">>>> ConvertTextToTokenIdsKorean G2pResult ja_bert_vec size:
        //     %zu", ja_bert_vec.size());
      }

      /// 여기선 미리 해준다!!!! ja_bert_vec
      // ADDBland 효과!!!!!!!
      for (size_t i = 0; i < word2ph.size(); ++i) {
        word2ph[i] = word2ph[i] * 2;
        // Python의 print(word2ph)와 동일하게 매 반복마다 출력
        std::cout << "word2ph (in loop): [";
        for (size_t j = 0; j < word2ph.size(); ++j) {
          std::cout << word2ph[j] << (j == word2ph.size() - 1 ? "" : ", ");
        }
        std::cout << "]" << std::endl;
      }

      // word2ph[0] += 1
      if (!word2ph.empty()) {  // 벡터가 비어있지 않은지 확인
        word2ph[0] += 1;
      }

      ///////////////////////////////////
      ///////////////////////////////////
      //
      /// @file offline-tts-vits-impl.h
      ///  Generate
      //  여기가 아닌 [addBlank하고 맞춰야함!!!!!]
      //
      // if hps.data.add_blank:
      //     phone = commons.intersperse(phone, 0) << 0을 시작서부터 사이 사이에
      //     추가 tone = commons.intersperse(tone, 0) language =
      //     commons.intersperse(language, 0) for i in range(len(word2ph)):
      //         word2ph[i] = word2ph[i] * 2 << 각 항목 * 2
      //     word2ph[0] += 1 <<  맨 첫 번째 항목에 1 추가

      // 파이선에서는 이 코드를 지나고 ja_bert를 구해서
      // bert_model
      //     res = model(**inputs, output_hidden_states=True)
      //     res = torch.cat(res["hidden_states"][-3:-2], -1)[0].cpu()
      //     print("japanese_bert.py get_bert_feature res1", res.shape)
      //     <<hidden_state[0].shape torch.Size([1, 9, 768]) #
      //     print("japanese_bert.py get_bert_feature res1", res)
      //     print("japanese_bert.py get_bert_feature word2ph", word2ph)
      //     print("japanese_bert.py get_bert_feature inputs[input_ids].shape",
      //     inputs["input_ids"].shape) print("japanese_bert.py get_bert_feature
      //     inputs[input_ids].shape[-1]", inputs["input_ids"].shape[-1])

      /////////////////////////////////////
      // logs
      // hidden_state.size 13
      // japanese_bert.py get_bert_feature res1 torch.Size([9, 768])
      // japanese_bert.py get_bert_feature word2ph [3, 14, 16, 8, 6, 12, 6, 6,
      // 2]
      // << 각 항목 * 2, 맨 첫 번째 항목에 1 추가 japanese_bert.py
      // get_bert_feature inputs[input_ids].shape torch.Size([1, 9])
      // japanese_bert.py get_bert_feature inputs[input_ids].shape[-1] 9
      // japanese_bert.py get_bert_feature phone_level_feature  cnt: 0
      // element.shape: torch.Size([3, 768]) japanese_bert.py get_bert_feature
      // phone_level_feature  cnt: 0 element.shape: torch.Size([14, 768])
      // japanese_bert.py get_bert_feature phone_level_feature  cnt: 0
      // element.shape: torch.Size([16, 768]) japanese_bert.py get_bert_feature
      // phone_level_feature  cnt: 0 element.shape: torch.Size([8, 768])
      // japanese_bert.py get_bert_feature phone_level_feature  cnt: 0
      // element.shape: torch.Size([6, 768]) japanese_bert.py get_bert_feature
      // phone_level_feature  cnt: 0 element.shape: torch.Size([12, 768])
      // japanese_bert.py get_bert_feature phone_level_feature  cnt: 0
      // element.shape: torch.Size([6, 768]) japanese_bert.py get_bert_feature
      // phone_level_feature  cnt: 0 element.shape: torch.Size([6, 768])
      // japanese_bert.py get_bert_feature phone_level_feature  cnt: 0
      // element.shape: torch.Size([2, 768])
      ////////////////////////////////

      // assert inputs["input_ids"].shape[-1] == len(word2ph),
      // f"{inputs['input_ids'].shape[-1]}/{len(word2ph)}" word2phone = word2ph
      // phone_level_feature = []
      // for i in range(len(word2phone)):
      //     repeat_feature = res[i].repeat(word2phone[i], 1)
      //     # print("japanese_bert.py get_bert_feature repeat_feature",
      //     repeat_feature) phone_level_feature.append(repeat_feature) #
      //     print("japanese_bert.py get_bert_feature phone_level_feature",
      //     phone_level_feature)

      // cnt = 0
      // for i in phone_level_feature:
      //     print("japanese_bert.py get_bert_feature phone_level_feature","
      //     cnt:",cnt,"element.shape:", i.shape)
      // phone_level_feature = torch.cat(phone_level_feature, dim=0)
      // print("japanese_bert.py get_bert_feature phone_level_feature 1",
      // phone_level_feature, phone_level_feature.shape)
      ////////////////////////////////////// torch.Size([73, 768])

      // return phone_level_feature.T
      //
      /////////////////////////////////
      /////////////////////////////////
      std::vector<float> ja_bert_vec = GetJaBert(word, word2ph);
      // ja_bert_vec에서 완전히 처리되서 나옴
      // std::vector<float> ja_bert_vec_final;

      // GetJJaBert(_text, word2ph);에서 완전히 처리
      //  int token_idx = 0;
      //  const int ja_bert_vec_size = 768;
      //  std::ostringstream word2ph_stream;
      //  for (auto e : word2ph) {
      //    word2ph_stream << e << ",";
      //    // word2ph 값이 1인 경우 해당 토큰은 건너뜁니다.
      //    // 하지만 ja_bert_vec_input에서 다음 토큰 위치로 이동하기 위해
      //    token_idx는
      //    // 증가해야 합니다.
      //    // if (e == 1) {
      //    //   token_idx++;
      //    //   continue;
      //    // }

      //   // ja_bert_vec_input에서 현재 토큰 벡터의 시작 위치를 계산합니다.
      //   int start_idx_in_input = token_idx * ja_bert_vec_size;

      //   // ja_bert_vec_input의 끝을 벗어나지 않도록 범위 체크
      //   if (start_idx_in_input + ja_bert_vec_size > ja_bert_vec.size()) {
      //     // std::cerr << "Error: Out of bounds access for ja_bert_vec_input
      //     at
      //     // token_idx " << token_idx << std::endl; SHERPA_ONNX_LOGE("Error:
      //     Out
      //     // of bounds access for ja_bert_vec_input at token_idx " <<
      //     token_idx); SHERPA_ONNX_LOGE(
      //         ">>>> ConvertTextToTokenIdsKorean ja_bert_vec_size: %zu",
      //         ja_bert_vec_size);
      //     SHERPA_ONNX_LOGE(
      //         ">>>> ConvertTextToTokenIdsKorean ja_bert_vec.size(): %zu",
      //         ja_bert_vec.size());
      //     SHERPA_ONNX_LOGE(
      //         ">>>> ConvertTextToTokenIdsKorean Error: Out of bounds access
      //         for " "ja_bert_vec_input at token_idx %zu", token_idx);
      //     break;  // 또는 적절한 오류 처리
      //   }

      //   // 현재 토큰에 해당하는 벡터의 시작과 끝 이터레이터를 가져옵니다.
      //   auto it_begin = ja_bert_vec.begin() + start_idx_in_input;
      //   auto it_end = ja_bert_vec.begin() + start_idx_in_input +
      //   ja_bert_vec_size;

      //   // 'e'번 반복하여 ja_bert_vec_final에 벡터를 통째로 추가합니다.
      //   for (int i = 0; i < e; i++) {
      //     ja_bert_vec_final.insert(ja_bert_vec_final.end(), it_begin,
      //     it_end);
      //     // SHERPA_ONNX_LOGE(
      //     //     ">>>> ConvertTextToTokenIdsKorean ja_bert_vec_final size:
      //     //     %zu,tokenIdx: %d", ja_bert_vec_final.size(),token_idx);
      //   }
      //   token_idx++;
      // }
      // SHERPA_ONNX_LOGE(">>>> word2ph addblanks sequence: %s",
      //                  word2ph_stream.str().c_str());
      SHERPA_ONNX_LOGE(
          ">>>> ConvertTextToTokenIdsKorean ja_bert_vec_final size: %zu",
          ja_bert_vec.size());

      SHERPA_ONNX_LOGE(
          ">>>> ConvertTextToTokenIdsKorean melo-tts-lexicon.cc phoneIds size: "
          "%zu",
          phoneIds.size());
      SHERPA_ONNX_LOGE(
          ">>>> ConvertTextToTokenIdsKorean melo-tts-lexicon.cc "
          "ja_bert_vec_final size: %zu",
          ja_bert_vec.size());
      // print phoneme_ids for debugging

      std::vector<TokenIDs> result_token_ids_vec;
      // std::vector<std::int64_t> toneIds = CreateToneKo(phoneIds.size());
      std::vector<std::int64_t> toneIds =
          std::vector<std::int64_t>(phoneIds.size(), 11);

      std::ostringstream oss;
      oss << "real_ja_bert10: [";
      for (size_t i = 0; i < 10; ++i) {
        oss << ja_bert_vec[i] << ",";
      }
      oss << "]";
      SHERPA_ONNX_LOGE("%s", oss.str().c_str());
      // SHERPA_ONNX_LOGE("=======THIS TEMP JARBERT as 0 =======");

      // std::vector<float> temp_ja_bert_vec_final =
      //     std::vector<float>(ja_bert_vec_final.size(), 0.0);
      SHERPA_ONNX_LOGE(
          ">>>> ConvertTextToTokenIdsKorean 768 * phoneIds size: %zu = %zu "
          "should  same ja_bert_vec_final.size :%zu",
          phoneIds.size(), 768 * phoneIds.size(), ja_bert_vec.size());
      this_sentence.tokens.insert(this_sentence.tokens.end(), phoneIds.begin(),
                                  phoneIds.end());
      this_sentence.tones.insert(this_sentence.tones.end(), toneIds.begin(),
                                 toneIds.end());
      this_sentence.ja_bert_vec.insert(this_sentence.ja_bert_vec.end(),
                                       ja_bert_vec.begin(), ja_bert_vec.end());

      this_sentence.sentences.push_back(word);

      
      // if (word == "." || word == "!" || word == "?" || word == "," ||
      //     word == "。" || word == "！" || word == "？" || word == "，") {
        ans.push_back(std::move(this_sentence));
        this_sentence = {};
      // }
      // result_token_ids_vec.emplace_back(std::move(phoneIds),
      // std::move(toneIds),
      //                                   std::move(ja_bert_vec));
      // if (debug_) {
      //   std::ostringstream oss;
      //   oss << "phoneme_ids: [";
      //   for (size_t i = 0; i < result_token_ids_vec[0].tokens.size(); ++i) {
      //     oss << result_token_ids_vec[0].tokens[i];
      //     if (i < result_token_ids_vec[0].tokens.size() - 1) {
      //       oss << ", ";
      //     }
      //   }
      //   oss << "]";
      //   SHERPA_ONNX_LOGE("%s", oss.str().c_str());
      // }
    }
    // if (!this_sentence.tokens.empty()) {
    //   ans.push_back(std::move(this_sentence));
    // }
    // print this_sentence all
    for (int i = 0; i < ans.size(); i++) {
      std::ostringstream oss;
      oss << "phoneme_ids: [";
      for (size_t j = 0; j < ans[i].tokens.size(); ++j) {
        oss << ans[i].tokens[j];
        if (j < ans[i].tokens.size() - 1) {
          oss << ", ";
        }
      }
      oss << "]\n";
      SHERPA_ONNX_LOGE("%s", oss.str().c_str());
      oss.clear();
      oss << "tones: [";
      for (size_t j = 0; j < ans[i].tones.size(); ++j) {
        oss << ans[i].tones[j];
        if (j < ans[i].tones.size() - 1) {
          oss << ", ";
        }
      }
      oss << "]\n";
      SHERPA_ONNX_LOGE("%s", oss.str().c_str());
      oss.clear();
      oss << "ja_bert_vec.size: [";
      // for (size_t j = 0; j < ans[i].ja_bert_vec.size(); ++j) {
      oss << ans[i].ja_bert_vec.size();
      // if (j < ans[i].ja_bert_vec.size() - 1) {
      //   oss << ", ";
      // }
      oss << "]\n";
      // }
      SHERPA_ONNX_LOGE("%s", oss.str().c_str());
      oss.clear();
      oss << "sentences: [";
      for (size_t j = 0; j < ans[i].sentences.size(); ++j) {
        oss << ans[i].sentences[j];
        if (j < ans[i].sentences.size() - 1) {
          oss << ", ";
        }
      }
      oss << "]\n";
      SHERPA_ONNX_LOGE("%s", oss.str().c_str());
    }

    return ans;
  }

  // ** 기존 ConvertTextToTokenIds 함수를 수정 **
  // 이 함수는 language_code 인자를 받지 않으므로,
  // 외부에 있는 MeloTtsLexicon::ConvertTextToTokenIds에서 언어 코드를 넘겨줘야
  // 합니다. 현재 코드를 보면 `std::string /*unused_voice = ""*/` 형태로 voice
  // 인자가 있습니다. 이 voice 인자를 language_code로 활용할 수 있습니다.
  std::vector<TokenIDs> ConvertTextToTokenIds(const std::string &_text) const {
    // SHERPA_ONNX_LOGE는 배포 시 로깅 수준을 낮추는 것이 좋습니다.
    // if text start with english check
    char16_t firstChar = _text.at(0);
    SHERPA_ONNX_LOGE(">>> ConvertTextToTokenIds firstChar: %c %s", firstChar,
                     _text.c_str());

    if ((firstChar >= char16_t('a') && firstChar <= char16_t('z')) ||
        (firstChar >= char16_t('A') && firstChar <= char16_t('Z'))) {
      SHERPA_ONNX_LOGE("ConvertTextToTokenIds called with do origin text: %s",
                       _text.c_str());
      return ConvertTextToTokenIdsOrigin(_text);
    }

    SHERPA_ONNX_LOGE("ConvertTextToTokenIds called do kroean with text: %s",
                     _text.c_str());
    return ConvertTextToTokenIdsKorean(_text);

    std::string text =
        ToLowerCase(_text);  // 입력 텍스트를 소문자로 변환 (영어 대응)

    // ** 하드코딩 시작 **
    // 현재 코드의 ConvertTextToTokenIds는 std::vector<TokenIDs>를 반환합니다.
    // 단일 문장을 처리할 경우 보통 하나의 TokenIDs 객체를 담은 벡터를
    // 반환합니다.
    std::vector<TokenIDs> result_token_ids_vec;

    // 언어 코드를 판단하는 로직이 필요합니다.
    // 이 예시에서는 언어 코드를 직접 받는 것이 아니라, 텍스트 내용을 통해
    // 대략적으로 판단하거나, 외부 호출 함수에서 language_code를 인자로 넘겨주는
    // 것이 더 적합합니다. 임시로, "밟아 밟아"가 들어오면 한국어라고 가정합니다.
    // 실제 사용 시에는 `MeloTtsLexicon::ConvertTextToTokenIds(const std::string
    // &text, const std::string &language_code)` 와 같이 language_code를
    // 명시적으로 받는 오버로드를 구현하는 것이 좋습니다. bool is_korean_text =
    // (_text == "밟아 밟아"); // 단순 하드코딩 예시
    bool is_korean_text = (_text ==
                           "하늘에 계신 우리 아버지여 이름이 거룩히 여김을 "
                           "받으시오며");  // 단순
                                           // 하드코딩
                                           // 예시

    if (is_korean_text) {
      std::vector<int64_t> phoneme_ids;
      std::vector<int64_t> tone_ids;

      // 파이썬 로그에서 추출한 phone ID 시퀀스
      // ['_', 'ᄇ', 'ᅡ', 'ᆯ', 'ᄇ', 'ᅡ', 'ᄇ', 'ᅡ', 'ᆯ', 'ᄇ', 'ᅡ', '_']
      // -> [0, 169, 181, 205, 169, 181, 169, 181, 205, 169, 181, 0]
      // phoneme_ids = {0, 169, 181, 205, 169, 181, 169, 181, 205, 169, 181, 0};
      // clean_text: 하늘에 계신 우리 아버지여 이름이 거룩히 여김을 받으시오며
      // -> phones:['_', 'ᄒ', 'ᅡ', 'ᄂ', 'ᅳ', 'ᄅ', 'ᅦ', 'ᄀ', 'ᅨ', 'ᄉ', 'ᅵ',
      // 'ᆫ', 'ᄋ', 'ᅮ', 'ᄅ', 'ᅵ', 'ᄋ', 'ᅡ', 'ᄇ', 'ᅥ', 'ᄌ', 'ᅵ', 'ᄋ', 'ᅧ',
      // 'ᄋ', 'ᅵ', 'ᄅ', 'ᅳ', 'ᄆ', 'ᅵ', 'ᄀ', 'ᅥ', 'ᄅ', 'ᅮ', 'ᄏ', 'ᅵ', 'ᄋ',
      // 'ᅧ', 'ᄀ', 'ᅵ', 'ᄆ', 'ᅳ', 'ᆯ', 'ᄇ', 'ᅡ', 'ᄃ', 'ᅳ', 'ᄉ', 'ᅵ', 'ᄋ',
      // 'ᅩ', 'ᄆ', 'ᅧ', '_'], tones:[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      // 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      // 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], word2ph:[1, 6, 5, 4,
      // 4, 4, 6, 2, 2, 2, 3, 2, 2, 4, 3, 3, 1]
      // phoneme_ids = {0, 180, 181, 164, 199, 167, 186, 162, 188, 171, 201,
      // 203, 173, 194, 167, 201, 173, 181, 169, 185, 174, 201, 173, 187, 173,
      // 201, 167, 199, 168, 201, 162, 185, 167, 194, 177, 201, 173, 187, 162,
      // 201, 168, 199, 205, 169, 181, 165, 199, 171, 201, 173, 189, 168, 187,
      // 0};
      // 나라이 임하옵시며 추가
      phoneme_ids = {0,   180, 181, 164, 199, 167, 186, 162, 188, 171, 201,
                     203, 173, 194, 167, 201, 173, 181, 169, 185, 174, 201,
                     173, 187, 173, 201, 167, 199, 168, 201, 162, 185, 167,
                     194, 177, 201, 173, 187, 162, 201, 168, 199, 205, 169,
                     181, 165, 199, 171, 201, 173, 189, 168, 187, 0,   164,
                     181, 167, 181, 173, 201, 173, 201, 206, 180, 181, 173,
                     189, 207, 172, 201, 168, 187, 0};

      // 파이썬 로그에서 추출한 tone ID 시퀀스
      // [0, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 0]
      for (int64_t phone_id : phoneme_ids) {
        if (phone_id == 0) {  // 패딩 토큰 '_'
          tone_ids.push_back(0);
        } else {  // 실제 음소
          tone_ids.push_back(0);
          // if (phone_id >= 169 && phone_id <= 181) {
          //     tone_ids.push_back(11);
          // } else if (phone_id >= 182 && phone_id <= 185) {
          //     tone_ids.push_back(10);
          // } else if (phone_id >= 186 && phone_id <= 188) {
          //     tone_ids.push_back(9);
          // } else if (phone_id >= 189 && phone_id <= 191) {
          //     tone_ids.push_back(8);
          // } else if (phone_id >= 192 && phone_id <= 194) {
          //     tone_ids.push_back(7);
          // } else if (phone_id >= 195 && phone_id <= 197) {
          //     tone_ids.push_back(6);
          // } else if (phone_id >= 198 && phone_id <= 200) {
          //     tone_ids.push_back(5);
          // } else {
          //     tone_ids.push_back(4); // 나머지 음소는 4로 처리
          // }
          // tone_ids.push_back(11);
        }
      }

      SHERPA_ONNX_LOGE(
          "Hardcoded Korean text '밟아 밟아' detected. Using pre-defined "
          "phonemes and tones.");
      // 단일 문장이므로, TokenIDs 객체를 만들어서 벡터에 추가
      result_token_ids_vec.push_back(TokenIDs(phoneme_ids, tone_ids));

      return result_token_ids_vec;  // 하드코딩된 결과 반환
    }
    // ** 하드코딩 끝 **

    // ** 기존 MeloTTS(중국어/영어) 처리 로직 시작 **
    // 밟아 밟아 가 아닌 다른 텍스트일 경우 기존 로직을 따릅니다.
    std::regex punct_re{"：|、|；"};
    std::string s = std::regex_replace(text, punct_re, ",");

    std::regex punct_re2("。");
    s = std::regex_replace(s, punct_re2, ".");

    std::regex punct_re3("？");
    s = std::regex_replace(s, punct_re3, "?");

    std::regex punct_re4("！");
    s = std::regex_replace(s, punct_re4, "!");

    std::vector<std::string> words;
    if (jieba_) {  // jieba_가 초기화되어 있다면 (주로 중국어)
      bool is_hmm = true;
      jieba_->Cut(text, words, is_hmm);

      if (debug_) {
        std::ostringstream os;
        std::string sep = "";
        for (const auto &w : words) {
          os << sep << w;
          sep = "_";
        }
#if __OHOS__
        SHERPA_ONNX_LOGE("input text: %{public}s", text.c_str());
        SHERPA_ONNX_LOGE("after replacing punctuations: %{public}s", s.c_str());

        SHERPA_ONNX_LOGE("after jieba processing: %{public}s",
                         os.str().c_str());
#else
        SHERPA_ONNX_LOGE("input text: %s", text.c_str());
        SHERPA_ONNX_LOGE("after replacing punctuations: %s", s.c_str());

        SHERPA_ONNX_LOGE("after jieba processing: %s", os.str().c_str());
#endif
      }
    } else {  // jieba_가 없다면 (주로 영어 등)
      words = SplitUtf8(text);

      if (debug_) {
        fprintf(stderr, "Input text in string (lowercase): %s\n", text.c_str());
        fprintf(stderr, "Input text in bytes (lowercase):");
        for (int8_t c : text) {
          fprintf(stderr, " %02x", c);
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "After splitting to words:");
        for (const auto &w : words) {
          fprintf(stderr, " %s", w.c_str());
        }
        fprintf(stderr, "\n");
      }
    }

    std::vector<TokenIDs> ans;  // 최종 반환될 TokenIDs 벡터
    TokenIDs this_sentence;     // 현재 처리 중인 문장의 TokenIDs

    for (const auto &w : words) {
      auto ids = ConvertWordToIds(w);  // lexicon에서 단어에 해당하는 ID를 찾음
      if (ids.tokens.empty()) {
        SHERPA_ONNX_LOGE("Ignore OOV '%s'", w.c_str());
        continue;
      }

      this_sentence.tokens.insert(this_sentence.tokens.end(),
                                  ids.tokens.begin(), ids.tokens.end());
      this_sentence.tones.insert(this_sentence.tones.end(), ids.tones.begin(),
                                 ids.tones.end());

      // 문장 분리 기준 (마침표, 쉼표 등)
      if (w == "." || w == "!" || w == "?" || w == "," || w == "。" ||
          w == "！" || w == "？" || w == "，") {
        ans.push_back(std::move(this_sentence));
        this_sentence = {};  // 다음 문장을 위해 초기화
      }
    }  // for (const auto &w : words)

    if (!this_sentence.tokens.empty()) {
      ans.push_back(std::move(this_sentence));  // 마지막 문장 추가
    }

    return ans;  // 기존 로직 결과 반환
  }

  std::vector<TokenIDs> ConvertTextToTokenIdsOrigin(
      const std::string &_text) const {
    std::string text = ToLowerCase(_text);
  // see
  //
  https:  // github.com/Plachtaa/VITS-fast-fine-tuning/blob/main/text/mandarin.py#L244
    std::regex punct_re{"：|、|；"};
    std::string s = std::regex_replace(text, punct_re, ",");

    std::regex punct_re2("。");
    s = std::regex_replace(s, punct_re2, ".");

    std::regex punct_re3("？");
    s = std::regex_replace(s, punct_re3, "?");

    std::regex punct_re4("！");
    s = std::regex_replace(s, punct_re4, "!");

    std::vector<std::string> words;
    if (jieba_) {
      bool is_hmm = true;
      jieba_->Cut(text, words, is_hmm);

      if (debug_) {
        std::ostringstream os;
        std::string sep = "";
        for (const auto &w : words) {
          os << sep << w;
          sep = "_";
        }
#if __OHOS__
        SHERPA_ONNX_LOGE("input text: %{public}s", text.c_str());
        SHERPA_ONNX_LOGE("after replacing punctuations: %{public}s", s.c_str());

        SHERPA_ONNX_LOGE("after jieba processing: %{public}s",
                         os.str().c_str());
#else
        SHERPA_ONNX_LOGE("input text: %s", text.c_str());
        SHERPA_ONNX_LOGE("after replacing punctuations: %s", s.c_str());

        SHERPA_ONNX_LOGE("after jieba processing: %s", os.str().c_str());
#endif
      }
    } else {
      words = SplitUtf8(text);

      if (debug_) {
        fprintf(stderr, "Input text in string (lowercase): %s\n", text.c_str());
        fprintf(stderr, "Input text in bytes (lowercase):");
        for (int8_t c : text) {
          fprintf(stderr, " %02x", c);
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "After splitting to words:");
        for (const auto &w : words) {
          fprintf(stderr, " %s", w.c_str());
        }
        fprintf(stderr, "\n");
      }
    }

    std::vector<TokenIDs> ans;
    TokenIDs this_sentence;

    for (const auto &w : words) {
      auto ids = ConvertWordToIds(w);
      if (ids.tokens.empty()) {
        SHERPA_ONNX_LOGE("Ignore OOV '%s'", w.c_str());
        continue;
      }

      this_sentence.tokens.insert(this_sentence.tokens.end(),
                                  ids.tokens.begin(), ids.tokens.end());
      this_sentence.tones.insert(this_sentence.tones.end(), ids.tones.begin(),
                                 ids.tones.end());

      if (w == "." || w == "!" || w == "?" || w == "," || w == "。" ||
          w == "！" || w == "？" || w == "，") {
        ans.push_back(std::move(this_sentence));
        this_sentence = {};
      }
    }  // for (const auto &w : words)

    if (!this_sentence.tokens.empty()) {
      ans.push_back(std::move(this_sentence));
    }

    return ans;
  }

  std::vector<float> GetJaBert(const std::string &text,
                               std::vector<int64_t> &word2ph_final) const {
    Ort::MemoryInfo memory_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    SHERPA_ONNX_LOGE("--- Tokenization & BERT Input Preparation ---");

    auto tokens_with_ids = tokenizer_kor_->tokenize_with_ids(text);
    auto ids = tokens_with_ids.second;    // input_ids에 사용될 ID 값
    auto tokens = tokens_with_ids.first;  // input_ids에 사용될 토큰 값
    // TextToPhoneId()
    {
      std::ostringstream os;
      for (const auto &id : ids) {
        os << id << " ";
      }
      SHERPA_ONNX_LOGE("GetJaBert input_ids: %s", os.str().c_str());
    }
    {
      std::ostringstream os;
      for (const auto &token : tokens) {
        os << token << " ";
      }
      SHERPA_ONNX_LOGE("GetJaBert tokens: %s", os.str().c_str());
    }

    // tokenizer_kor_->tokenize_with_ids(text);

    const int64_t seq_len = ids.size();
    const int64_t batch_size = 1;

    std::vector<int64_t> input_ids_data(ids.begin(), ids.end());
    std::vector<int64_t> token_type_ids_data(seq_len, 0);
    std::vector<int64_t> attention_mask_data(seq_len, 1);

    std::vector<int64_t> bert_input_shape = {batch_size, seq_len};

    std::vector<Ort::Value> bert_input_tensors;

    bert_input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
        memory_info, input_ids_data.data(), input_ids_data.size(),
        bert_input_shape.data(), bert_input_shape.size()));
    bert_input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
        memory_info, attention_mask_data.data(), attention_mask_data.size(),
        bert_input_shape.data(), bert_input_shape.size()));
    bert_input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
        memory_info, token_type_ids_data.data(), token_type_ids_data.size(),
        bert_input_shape.data(), bert_input_shape.size()));

    // 2. BERT 모델 추론 실행
    SHERPA_ONNX_LOGE("--- Running BERT Inference ---");
    std::vector<Ort::Value> bert_output_tensors;
    const char *my_input_names[] = {"input_ids", "attention_mask",
                                    "token_type_ids"};
    try {
      // BERT 모델의 입력 이름 순서가 중요합니다. "input_ids", "attention_mask",
      // "token_type_ids" ja_bert_input_names_ptr_는 InitJaBert에서 이미 모델의
      // 실제 입력 이름을 얻어와 채워져 있어야 합니다.
      bert_output_tensors = ja_bert_sess_->Run(
          Ort::RunOptions{nullptr}, my_input_names, bert_input_tensors.data(),
          bert_input_tensors.size(), ja_bert_output_names_ptr_.data(),
          ja_bert_output_names_ptr_.size());
      SHERPA_ONNX_LOGE("BERT Inference completed successfully.");
    } catch (const Ort::Exception &e) {
      SHERPA_ONNX_LOGE("Error running BERT inference: %s", e.what());
      return {};  // 에러 발생 시 빈 벡터 반환
    }

    // 3. BERT 출력 결과 처리하여 ja_bert_vec 얻기
    // BERT 모델의 출력은 보통 [batch_size, sequence_length, hidden_size] 형태의
    // last_hidden_state 입니다. 혹은 [batch_size, hidden_size] 형태의
    // pooled_output 일 수 있습니다. VITS 모델이 기대하는 형식에 따라 적절히
    // 처리해야 합니다. 여기서는 last_hidden_state를 가정하고 flatten하여 1D
    // float 벡터로 만듭니다.
    std::vector<float> ja_bert_vec;
    if (bert_output_tensors.empty()) {
      SHERPA_ONNX_LOGE("BERT returned no output tensors.");
      return {};
    }

    SHERPA_ONNX_LOGE("GetJatBert bert_output_tensors.size(): %d",
                     bert_output_tensors.size());
    // BERT 모델의 첫 번째 출력을 가져옵니다.
    Ort::Value &bert_output_tensor = bert_output_tensors[0];
    auto bert_output_info = bert_output_tensor.GetTensorTypeAndShapeInfo();
    std::vector<int64_t> bert_output_shape = bert_output_info.GetShape();
    size_t bert_output_size = bert_output_info.GetElementCount();
    // bert_output_tensors의 모든 텐서들을 순회하며 출력
    for (size_t i = 0; i < bert_output_tensors.size(); ++i) {
      Ort::Value &current_tensor = bert_output_tensors[i];
      auto current_info = current_tensor.GetTensorTypeAndShapeInfo();
      std::vector<int64_t> current_shape = current_info.GetShape();
      size_t current_element_count = current_info.GetElementCount();

      SHERPA_ONNX_LOGE("--- Tensor %zu of bert_output_tensors ---", i);

      // 텐서의 형태(shape) 출력
      std::string shape_str = "[";
      for (size_t dim_idx = 0; dim_idx < current_shape.size(); ++dim_idx) {
        shape_str += std::to_string(current_shape[dim_idx]);
        if (dim_idx < current_shape.size() - 1) {
          shape_str += ", ";
        }
      }
      shape_str += "]";
      SHERPA_ONNX_LOGE("Shape: %s", shape_str.c_str());
      SHERPA_ONNX_LOGE("Total elements: %zu", current_element_count);

      // 텐서 데이터에 접근하여 모든 벡터의 앞 3개 원소 출력
      if (current_element_count > 0 &&
          current_shape.size() >=
              3) {  // 최소한 [batch, seq_len, hidden_size] 형태여야 함
        float *current_data = current_tensor.GetTensorMutableData<float>();

        // current_shape: [batch_size, sequence_length, hidden_size]
        // 여기서 batch_size는 1이라고 가정합니다.
        // int64_t sequence_length = current_shape[1];
        int64_t sequence_length = 5;
        int64_t hidden_size = current_shape[2];

        const size_t num_elements_to_print_per_vector =
            std::min((size_t)3, (size_t)hidden_size);

        SHERPA_ONNX_LOGE("5개만 보이게==================");
        SHERPA_ONNX_LOGE(
            "Printing first %zu elements for each of %lld vectors (tokens):",
            num_elements_to_print_per_vector, sequence_length);

        for (int64_t seq_idx = 0; seq_idx < sequence_length; ++seq_idx) {
          std::string vector_elements_str = "Vector (token) ";
          vector_elements_str += std::to_string(seq_idx);
          vector_elements_str += ": [";

          // 각 벡터의 시작 오프셋 계산: seq_idx * hidden_size
          float *vector_start_ptr = current_data + (seq_idx * hidden_size);

          for (size_t elem_idx = 0; elem_idx < num_elements_to_print_per_vector;
               ++elem_idx) {
            vector_elements_str += std::to_string(vector_start_ptr[elem_idx]);
            if (elem_idx < num_elements_to_print_per_vector - 1) {
              vector_elements_str += ", ";
            }
          }
          vector_elements_str += "]";
          SHERPA_ONNX_LOGE("%s", vector_elements_str.c_str());
        }
      } else if (current_element_count > 0 && current_shape.size() < 3) {
        SHERPA_ONNX_LOGE(
            "Warning: Tensor has fewer than 3 dimensions. Cannot process as "
            "[batch, seq_len, hidden_size].");
        // 이 경우에는 기존처럼 전체 텐서의 처음 10개 원소를 출력할 수 있습니다.
        // 예를 들어:
        // size_t num_elements_to_print = std::min((size_t)10,
        // current_element_count);
        // ... (기존 처음 10개 출력 로직) ...
      } else {
        SHERPA_ONNX_LOGE("Tensor is empty (0 elements).");
      }
    }
    // 추출된 BERT 출력의 핵심 차원
    // bert_output_shape: [batch_size, sequence_length, hidden_size]
    int64_t original_sequence_length = bert_output_shape[1];  // 예: 9
    int64_t hidden_size = bert_output_shape[2];               // 예: 768

    // word2ph_final의 길이와 original_sequence_length가 일치하는지 확인 (필수)
    if (word2ph_final.size() != original_sequence_length) {
      SHERPA_ONNX_LOGE(
          "Mismatch between word2ph_final size (%zu) and BERT original "
          "sequence length (%lld).",
          word2ph_final.size(), original_sequence_length);
      return {};  // 에러 처리: 빈 벡터 반환
    }

    // 1. 확장된 시퀀스 길이 계산
    long long expanded_sequence_length = 0;
    for (int64_t count : word2ph_final) {
      expanded_sequence_length += count;
    }

    SHERPA_ONNX_LOGE(
        "Original BERT sequence length: %lld, Expanded sequence length: %lld",
        original_sequence_length, expanded_sequence_length);
    float *bert_output_data = bert_output_tensor.GetTensorMutableData<float>();
    // 2. 확장된 데이터를 저장할 임시 벡터 (전치 전)
    // 형태: [expanded_sequence_length, hidden_size]
    std::vector<float> expanded_data_flat(expanded_sequence_length *
                                          hidden_size);
    size_t current_expanded_idx =
        0;  // expanded_data_flat에 데이터를 채울 현재 위치

    for (int64_t original_seq_idx = 0;
         original_seq_idx < original_sequence_length; ++original_seq_idx) {
      // 현재 원본 BERT 벡터의 시작 포인터
      float *original_vector_start_ptr =
          bert_output_data + (original_seq_idx * hidden_size);

      // word2ph_final에 해당하는 반복 횟수
      int64_t repeat_count = word2ph_final[original_seq_idx];

      for (int64_t r = 0; r < repeat_count; ++r) {
        // 현재 벡터를 expanded_data_flat에 복사
        for (int64_t h_idx = 0; h_idx < hidden_size; ++h_idx) {
          expanded_data_flat[current_expanded_idx * hidden_size + h_idx] =
              original_vector_start_ptr[h_idx];
        }
        current_expanded_idx++;
      }
    }

    SHERPA_ONNX_LOGE("Expanded data generated. Flat size: %zu",
                     expanded_data_flat.size());

    // (디버깅용) 확장된 데이터의 앞 10개 원소 출력
    // SHERPA_ONNX_LOGE("Expanded data (first 10 elements):");
    // std::string debug_expanded_str = "[";
    // for (size_t k = 0; k < std::min((size_t)10, expanded_data_flat.size());
    // ++k) {
    //     debug_expanded_str += std::to_string(expanded_data_flat[k]);
    //     if (k < std::min((size_t)10, expanded_data_flat.size()) - 1)
    //     debug_expanded_str += ", ";
    // }
    // debug_expanded_str += "]";
    // SHERPA_ONNX_LOGE("%s", debug_expanded_str.c_str());

    // 3. 확장된 데이터를 전치 (Transpose)
    // 목표: [expanded_sequence_length, hidden_size] -> [hidden_size,
    // expanded_sequence_length] 전치된 데이터를 ja_bert_vec에 직접 채웁니다.
    ja_bert_vec.assign(expanded_sequence_length * hidden_size,
                       0.0f);  // 크기 할당 및 0으로 초기화 (선택 사항)

    for (int64_t h_idx = 0; h_idx < hidden_size; ++h_idx) {
      for (int64_t exp_seq_idx = 0; exp_seq_idx < expanded_sequence_length;
           ++exp_seq_idx) {
        // 전치 공식:
        // Transposed[row][col] = Original[col][row]
        // 즉, ja_bert_vec[h_idx][exp_seq_idx] =
        // expanded_data_flat[exp_seq_idx][h_idx]
        ja_bert_vec[h_idx * expanded_sequence_length + exp_seq_idx] =
            expanded_data_flat[exp_seq_idx * hidden_size + h_idx];
      }
    }

    SHERPA_ONNX_LOGE(
        "Data transposed and copied to ja_bert_vec. Final ja_bert_vec size: "
        "%zu",
        ja_bert_vec.size());

    // (디버깅용) 최종 ja_bert_vec의 앞 10개 원소 출력 (전치된 형태)
    SHERPA_ONNX_LOGE("Final ja_bert_vec (first 10 elements):");
    std::string final_elements_str = "[";
    for (size_t k = 0; k < std::min((size_t)10, ja_bert_vec.size()); ++k) {
      final_elements_str += std::to_string(ja_bert_vec[k]);
      if (k < std::min((size_t)10, ja_bert_vec.size()) - 1) {
        final_elements_str += ", ";
      }
    }
    final_elements_str += "]";
    SHERPA_ONNX_LOGE("%s", final_elements_str.c_str());

    return ja_bert_vec;
    // auto bert_output_info = bert_output_tensor.GetTensorTypeAndShapeInfo();
    // std::vector<int64_t> bert_output_shape = bert_output_info.GetShape();
    // size_t bert_output_size = bert_output_info.GetElementCount();

    // // if (bert_output_data_type !=
    // // Ort::TypeAndShapeInfo::TensorElemDataType::ORT_FLOAT) {
    // //     SHERPA_ONNX_LOGE("BERT output type is not float. Expected
    // float.");
    // //     return {};
    // // }

    // float *bert_output_data =
    // bert_output_tensor.GetTensorMutableData<float>();

    // // VITS 모델이 기대하는 BERT 임베딩의 차원을 확인해야 합니다.
    // // 보통 [1, seq_len, 768] 형태이고, 이것을 [768 * seq_len]으로
    // flatten하여
    // // 사용합니다. 또는 [1, 768] (Pooled Output)일 수도 있습니다. 예시에서는
    // // last_hidden_state (3차원)를 flatten하는 것으로 가정합니다.
    // size_t expected_dim_count = 3;  // [batch, seq_len, hidden_size]
    // if (bert_output_shape.size() < expected_dim_count) {
    //   SHERPA_ONNX_LOGE(
    //       "Unexpected BERT output shape dimension. Expected at least 3 dims "
    //       "(batch, seq_len, hidden_size). Got %zu.",
    //       bert_output_shape.size());
    //   // Pooled output ([batch, hidden_size])일 경우 여기를 수정해야 합니다.
    //   // 예를 들어: expected_dim_count = 2; // [batch, hidden_size]
    // }

    // // 이 예시에서는 모든 BERT 출력을 그대로 ja_bert_vec에 복사합니다.
    // // 실제 BERT 출력의 hidden_size가 768이라고 가정합니다.
    // ja_bert_vec.assign(bert_output_data, bert_output_data +
    // bert_output_size);

    // SHERPA_ONNX_LOGE("GetJaBert BERT output processed. ja_bert_vec size:
    // %zu",
    //                  ja_bert_vec.size());
    // return ja_bert_vec;
  }

 private:
  void InitTokenizer(std::istream &is) {
    // SHERPA_ONNX_LOGE(">>>> InitTokenizer vocab_path: %s",
    // vocab_path.c_str());
    tokenizer_kor_ = std::make_unique<WordPieceTokenizer>(is, true);
    // if (debug_) {
    auto tokens = tokenizer_kor_->tokenize("안녕하세요 좋은 아침입니다.");
    for (const auto &t : tokens) {
      SHERPA_ONNX_LOGE(">>>> InitTokenizer tokenizer test %s", t.c_str());
    }
    // SHERPA_ONNX_LOGE(">>>> InitTokenizer tokenizer test %s",
    // vocab_path.c_str());
    // }
  }
  TokenIDs ConvertWordToIds(const std::string &w) const {
    if (word2ids_.count(w)) {
      return word2ids_.at(w);
    }

    if (token2id_.count(w)) {
      return {{token2id_.at(w)}, {0}};
    }

    TokenIDs ans;

    std::vector<std::string> words = SplitUtf8(w);
    for (const auto &word : words) {
      if (word2ids_.count(word)) {
        auto ids = ConvertWordToIds(word);
        ans.tokens.insert(ans.tokens.end(), ids.tokens.begin(),
                          ids.tokens.end());
        ans.tones.insert(ans.tones.end(), ids.tones.begin(), ids.tones.end());
      } else {
        // If the lexicon does not contain the word, we split the word into
        // characters.
        //
        // For instance, if the word is TTS and it is does not exist
        // in the lexicon, we split it into 3 characters: T T S
        std::string s;
        for (char c : word) {
          s = c;
          if (word2ids_.count(s)) {
            const auto &t = word2ids_.at(s);
            ans.tokens.insert(ans.tokens.end(), t.tokens.begin(),
                              t.tokens.end());
            ans.tones.insert(ans.tones.end(), t.tones.begin(), t.tones.end());
          }
        }
      }
    }

    return ans;
  }

  void InitTokens(std::istream &is) {
    SHERPA_ONNX_LOGE("InitTokens called");
    token2id_ = ReadTokens(is);
    token2id_[" "] = token2id_["_"];

    std::vector<std::pair<std::string, std::string>> puncts = {
        {",", "，"}, {".", "。"}, {"!", "！"}, {"?", "？"}};

    for (const auto &p : puncts) {
      if (token2id_.count(p.first) && !token2id_.count(p.second)) {
        token2id_[p.second] = token2id_[p.first];
      }

      if (!token2id_.count(p.first) && token2id_.count(p.second)) {
        token2id_[p.first] = token2id_[p.second];
      }
    }

    if (!token2id_.count("、") && token2id_.count("，")) {
      token2id_["、"] = token2id_["，"];
    }
    SHERPA_ONNX_LOGE("InitTokens completed, token2id_ size: %zu",
                     token2id_.size());
    std::ostringstream oss;
    oss << "token2id_: {";
    for (const auto &pair : token2id_) {
      oss << "\"" << pair.first << "\": " << pair.second << ", ";
    }
    oss << "}";
    SHERPA_ONNX_LOGE("%s", oss.str().c_str());
    SHERPA_ONNX_LOGE("InitTokens completed");
  }

  void InitLexicon(std::istream &is) {
    std::string word;
    std::vector<std::string> token_list;

    std::vector<std::string> phone_list;
    std::vector<int64_t> tone_list;

    std::string line;
    std::string phone;
    int32_t line_num = 0;

    while (std::getline(is, line)) {
      ++line_num;

      std::istringstream iss(line);

      token_list.clear();
      phone_list.clear();
      tone_list.clear();

      iss >> word;
      ToLowerCase(&word);

      if (word2ids_.count(word)) {
        SHERPA_ONNX_LOGE("Duplicated word: %s at line %d:%s. Ignore it.",
                         word.c_str(), line_num, line.c_str());
        continue;
      }

      while (iss >> phone) {
        token_list.push_back(std::move(phone));
      }

      if ((token_list.size() & 1) != 0) {
        SHERPA_ONNX_LOGE("Invalid line %d: '%s'", line_num, line.c_str());
        exit(-1);
      }

      int32_t num_phones = token_list.size() / 2;
      phone_list.reserve(num_phones);
      tone_list.reserve(num_phones);

      for (int32_t i = 0; i != num_phones; ++i) {
        phone_list.push_back(std::move(token_list[i]));
        tone_list.push_back(std::stoi(token_list[i + num_phones], nullptr));
        if (tone_list.back() < 0 || tone_list.back() > 50) {
          SHERPA_ONNX_LOGE("Invalid line %d: '%s'", line_num, line.c_str());
          exit(-1);
        }
      }

      std::vector<int32_t> ids = ConvertTokensToIds(token2id_, phone_list);
      if (ids.empty()) {
        continue;
      }

      if (ids.size() != num_phones) {
        SHERPA_ONNX_LOGE("Invalid line %d: '%s'", line_num, line.c_str());
        exit(-1);
      }

      std::vector<int64_t> ids64{ids.begin(), ids.end()};

      word2ids_.insert(
          {std::move(word), TokenIDs{std::move(ids64), std::move(tone_list)}});
    }

    // For Chinese+English MeloTTS
    word2ids_["呣"] = word2ids_["母"];
    word2ids_["嗯"] = word2ids_["恩"];
  }
  void InitJaBert(void *model_data, size_t model_data_length) {
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::InitJaBert() "
        "csrc/offline-tts-vits-model.cc start");
    ja_bert_env_ = Ort::Env(ORT_LOGGING_LEVEL_WARNING, "ja_bert");
    ja_bert_sess_opts_ = Ort::SessionOptions();
    ja_bert_sess_opts_.SetIntraOpNumThreads(1);

    ja_bert_sess_ = std::make_unique<Ort::Session>(
        ja_bert_env_, model_data, model_data_length, ja_bert_sess_opts_);
    GetInputNames(ja_bert_sess_.get(), &ja_bert_input_names_,
                  &ja_bert_input_names_ptr_);
    GetOutputNames(ja_bert_sess_.get(), &ja_bert_output_names_,
                   &ja_bert_output_names_ptr_);

    // // get meta data
    Ort::ModelMetadata meta_data = ja_bert_sess_->GetModelMetadata();
    // // if (config_.debug) {
    std::ostringstream os;
    os << "---ja_bert model---\n";
    PrintModelMetadata(os, meta_data);
    os << "----------input names----------\n";
    for (auto &name : ja_bert_input_names_) {
      os << name << "\n";
    }
    os << "----------output names----------\n";
    for (auto &name : ja_bert_output_names_) {
      os << name << "\n";
    }
    SHERPA_ONNX_LOGE("%s", os.str().c_str());
    // }
  }

 private:
  // lexicon.txt is saved in word2ids_
  std::unordered_map<std::string, TokenIDs> word2ids_;

  // tokens.txt is saved in token2id_
  std::unordered_map<std::string, int32_t> token2id_;

  OfflineTtsVitsModelMetaData meta_data_;

  std::unique_ptr<cppjieba::Jieba> jieba_;

  std::unique_ptr<WordPieceTokenizer> tokenizer_kor_;

  // ja_bert
  Ort::Env ja_bert_env_;
  Ort::SessionOptions ja_bert_sess_opts_;
  Ort::AllocatorWithDefaultOptions ja_bert_allocator_;

  std::unique_ptr<Ort::Session> ja_bert_sess_;

  std::vector<std::string> ja_bert_input_names_;
  std::vector<const char *> ja_bert_input_names_ptr_;

  std::vector<std::string> ja_bert_output_names_;
  std::vector<const char *> ja_bert_output_names_ptr_;

  // std::string vocab_path_;
  bool debug_ = false;
};

MeloTtsLexicon::~MeloTtsLexicon() = default;

MeloTtsLexicon::MeloTtsLexicon(const std::string &lexicon,
                               const std::string &tokens,
                               const std::string &dict_dir,
                               const OfflineTtsVitsModelMetaData &meta_data,
                               bool debug)
    : impl_(std::make_unique<Impl>(lexicon, tokens, dict_dir, meta_data,
                                   debug)) {}

MeloTtsLexicon::MeloTtsLexicon(const std::string &lexicon,
                               const std::string &tokens,
                               const OfflineTtsVitsModelMetaData &meta_data,
                               bool debug)
    : impl_(std::make_unique<Impl>(lexicon, tokens, meta_data, debug)) {}

template <typename Manager>
MeloTtsLexicon::MeloTtsLexicon(Manager *mgr, const std::string &lexicon,
                               const std::string &tokens,
                               const std::string &dict_dir,
                               const OfflineTtsVitsModelMetaData &meta_data,
                               bool debug)
    : impl_(std::make_unique<Impl>(mgr, lexicon, tokens, dict_dir, meta_data,
                                   debug)) {}

template <typename Manager>
MeloTtsLexicon::MeloTtsLexicon(Manager *mgr, const std::string &lexicon,
                               const std::string &tokens,
                               const OfflineTtsVitsModelMetaData &meta_data,
                               bool debug)
    : impl_(std::make_unique<Impl>(mgr, lexicon, tokens, meta_data, debug)) {}

template <typename Manager>
MeloTtsLexicon::MeloTtsLexicon(Manager *mgr, const std::string &lexicon,
                               const std::string &tokens,
                               const std::string &ja_bert_model_path,
                               const std::string &vocab_path,
                               const OfflineTtsVitsModelMetaData &meta_data,
                               bool debug)
    : impl_(std::make_unique<Impl>(mgr, lexicon, tokens, ja_bert_model_path,
                                   vocab_path, meta_data, debug)) {}

std::vector<TokenIDs> MeloTtsLexicon::ConvertTextToTokenIds(
    const std::string &text, const std::string & /*unused_voice = ""*/) const {
  // return impl_->ConvertTextToTokenIdsKorean(text);
  return impl_->ConvertTextToTokenIds(text);
  // return impl_->ConvertTextToTokenIds(text);
}

// std::vector<float> MeloTtsLexicon::GetJaBert(const std::string &text) const {
//   SHERPA_ONNX_LOGE(">>>>> GetJaBert MeloTtsLexicon::GetJaBert impl");
//   return impl_->GetJaBert(text);
//   // return impl_->GetJaBert(text);
// }
#if __ANDROID_API__ >= 9
template MeloTtsLexicon::MeloTtsLexicon(
    AAssetManager *mgr, const std::string &lexicon, const std::string &tokens,
    const std::string &dict_dir, const OfflineTtsVitsModelMetaData &meta_data,
    bool debug);

template MeloTtsLexicon::MeloTtsLexicon(
    AAssetManager *mgr, const std::string &lexicon, const std::string &tokens,
    const OfflineTtsVitsModelMetaData &meta_data, bool debug);

template MeloTtsLexicon::MeloTtsLexicon(
    AAssetManager *mgr, const std::string &lexicon, const std::string &tokens,
    const std::string &ja_bert_model_path, const std::string &vocab_path,
    const OfflineTtsVitsModelMetaData &meta_data, bool debug);
#endif

#if __OHOS__
template MeloTtsLexicon::MeloTtsLexicon(
    NativeResourceManager *mgr, const std::string &lexicon,
    const std::string &tokens, const std::string &dict_dir,
    const OfflineTtsVitsModelMetaData &meta_data, bool debug);

template MeloTtsLexicon::MeloTtsLexicon(
    NativeResourceManager *mgr, const std::string &lexicon,
    const std::string &tokens, const OfflineTtsVitsModelMetaData &meta_data,
    bool debug);
#endif

}  // namespace sherpa_onnx
