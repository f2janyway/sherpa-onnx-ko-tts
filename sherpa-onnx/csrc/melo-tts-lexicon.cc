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
      ja_bert_model_path_ = ja_bert_model_path;
      // vocab_path_ = vocab_path;
      SHERPA_ONNX_LOGE(
          ">>>>> MeloTtsLexicon::Impl::Impl ja_bert_model_path_ %s",
          ja_bert_model_path_.c_str());
      SHERPA_ONNX_LOGE(">>>>> MeloTtsLexicon::Impl::Impl vocab_path_ %s",
                       vocab_path.c_str());
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
  }

  // --- 자모 분리 로직 ---

  std::vector<TokenIDs> ConvertKoreanTextToTokenIds(
      const std::string &_text) const {
    SHERPA_ONNX_LOGE("ConvertKoreanTextToTokenIds called with text: %s",
                     _text.c_str());

    std::vector<std::int64_t> phoneIds = TextToPhoneId(_text);

    SHERPA_ONNX_LOGE("phoneIds size: %zu", phoneIds.size());
    // print phoneme_ids for debugging

    std::vector<TokenIDs> result_token_ids_vec;
    // std::vector<std::int64_t> toneIds = CreateToneKo(phoneIds.size());
    std::vector<std::int64_t> toneIds =
        std::vector<std::int64_t>(phoneIds.size(), 11);
    result_token_ids_vec.emplace_back(std::move(phoneIds), std::move(toneIds));
    if (debug_) {
      std::ostringstream oss;
      oss << "phoneme_ids: [";
      for (size_t i = 0; i < result_token_ids_vec[0].tokens.size(); ++i) {
        oss << result_token_ids_vec[0].tokens[i];
        if (i < result_token_ids_vec[0].tokens.size() - 1) {
          oss << ", ";
        }
      }
      oss << "]";
      SHERPA_ONNX_LOGE("%s", oss.str().c_str());
    }
    return result_token_ids_vec;

    // 입력 텍스트를 소문자로 변환 (영어 대응)
    // std::string text = ToLowerCase(_text);

    // 단일 문장을 처리할 경우 보통 하나의 TokenIDs 객체를 담은 벡터를
    // 반환합니다.
    // std::vector<TokenIDs>
    //     result_token_ids_vec;

    // 파이썬 로그에서 추출한 phone ID 시퀀스
    // ['_', 'ᄇ', 'ᅡ', 'ᆯ', 'ᄇ', 'ᅡ', 'ᄇ', 'ᅡ', 'ᆯ', 'ᄇ', 'ᅡ', '_']
    // -> [0, 169, 181, 205, 169, 181, 169, 181, 205, 169, 181, 0]
    // std::vector<int64_t> phoneme_ids;

    // std::string text = ToLowerCase(_text);

    // SHERPA_ONNX_LOGE(">>> ConvertKoreanTextToTokenIds called with text: %s",
    //                  text.c_str());
    // std::vector<std::string> splited_ko_text = JamoUtils::split(text);

    // SHERPA_ONNX_LOGE(">>> add phoneme_ids: start");
    // // show all tokens for debugging in token2id_
    // for (const auto &pair : token2id_) {
    //   SHERPA_ONNX_LOGE(">>> token2id_ key: %s, value: %d",
    //   pair.first.c_str(),
    //                    pair.second);
    // }

    // // show token2id_ for debugging
    // SHERPA_ONNX_LOGE(">>> token2id_ size: %zu", token2id_.size());
    // for (const auto &ko : splited_ko_text) {
    //   SHERPA_ONNX_LOGE(">>> processing Jamo: %s", ko.c_str());
    //   // if (ko == "") {
    //   //   phoneme_ids.push_back(0);
    //   //   continue;
    //   // }
    //   // Use find() instead of at() to avoid std::out_of_range exception
    //   auto it = token2id_.find(ko);
    //   if (it != token2id_.end()) {
    //     // Key found, add its associated ID
    //     phoneme_ids.push_back(it->second);
    //   } else {
    //     // Key not found. This is where your problem is.
    //     // You need to decide how to handle missing Jamo.
    //     // Option 1: Log an error and skip this Jamo.
    //     SHERPA_ONNX_LOGE(
    //         "Error: Jamo '%s' not found in token2id_ map. Skipping.",
    //         ko.c_str());
    //     // Option 2: Add a special "unknown" token ID if you have one.
    //     // phoneme_ids.push_back(UNKNOWN_TOKEN_ID);
    //     // Option 3: If this should never happen, you might want to throw a
    //     // controlled error or assertion. For now, logging and skipping is a
    //     // safe approach to prevent crash.
    //   }
    // }
    // SHERPA_ONNX_LOGE(">>> add phoneme_ids: end");

    // std::vector<int64_t> tone_ids(phoneme_ids.size(),
    //                               0);  // 모든 톤 ID를 기본값으로 설정

    // SHERPA_ONNX_LOGE("result_token_ids_vec size: %zu",
    //                  result_token_ids_vec.size());
    // result_token_ids_vec.emplace_back(std::move(phoneme_ids),
    //                                   std::move(tone_ids));

    // // print phoneme_ids for debugging
    // // if (debug_) {
    // //   std::ostringstream oss;
    // //   oss << "phoneme_ids: [";
    // //   for (size_t i = 0; i < result_token_ids_vec[0].tokens.size(); ++i) {
    // //     oss << result_token_ids_vec[0].tokens[i];
    // //     if (i < result_token_ids_vec[0].tokens.size() - 1) {
    // //       oss << ", ";
    // //     }
    // //   }
    // //   oss << "]";
    // //   SHERPA_ONNX_LOGE("%s", oss.str().c_str());
    // // }
    // SHERPA_ONNX_LOGE("ConvertKoreanTextToTokenIds completed");
    // std::cout << "phoneme_ids: [";
    // for (const auto &id : phoneme_ids) {
    //   std::cout << id << ", ";
    // }
    // std::cout << std::endl;

    // return result_token_ids_vec;
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
    return ConvertKoreanTextToTokenIds(_text);

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

 private:
  void InitTokenizer(std::istream &is) {
    // SHERPA_ONNX_LOGE(">>>> InitTokenizer vocab_path: %s", vocab_path.c_str());
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

 private:
  // lexicon.txt is saved in word2ids_
  std::unordered_map<std::string, TokenIDs> word2ids_;

  // tokens.txt is saved in token2id_
  std::unordered_map<std::string, int32_t> token2id_;

  OfflineTtsVitsModelMetaData meta_data_;

  std::unique_ptr<cppjieba::Jieba> jieba_;

  std::string ja_bert_model_path_;
  std::unique_ptr<WordPieceTokenizer> tokenizer_kor_;
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
  // return impl_->ConvertKoreanTextToTokenIds(text);
  return impl_->ConvertTextToTokenIds(text);
  // return impl_->ConvertTextToTokenIds(text);
}

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
