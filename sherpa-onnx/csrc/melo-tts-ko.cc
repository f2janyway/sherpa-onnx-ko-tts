#include <algorithm>
#include <codecvt>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>
#include "melo-tts-ko-tokenizer.h"
#include "melo-tts-ko.h"
#include "sherpa-onnx/csrc/macros.h"
using namespace std;

// 한글 유니코드 오프셋 값
const int JAMO_BASE = 0xAC00;
const int LEAD_BASE = 0x1100;   // 초성
const int VOWEL_BASE = 0x1161;  // 중성
const int TAIL_BASE = 0x11A7;   // 종성 (주의: 종성은 1부터 시작)

const int NUM_LEADS = 19;

const int NUM_VOWELS = 21;
const int NUM_TAILS = 28;

// 한글 여부 확인
bool is_hangul_syllable(wchar_t ch) { return (ch >= 0xAC00 && ch <= 0xD7A3); }
// Function to trim leading/trailing whitespace from a string
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (std::string::npos == first) {
        return str; // No non-whitespace characters
    }
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
}

bool containsDigit(const std::wstring& text) {
    // 문자열의 각 문자를 순회합니다.
    for (char ch : text) {
        // 현재 문자가 숫자인지 확인합니다.
        // isdigit 함수는 해당 문자가 0부터 9까지의 숫자를 나타내면 true를 반환합니다.
        if (std::isdigit(ch)) {
            return true; // 숫자를 찾으면 즉시 true를 반환하고 함수를 종료합니다.
        }
    }
    return false; // 문자열 전체를 순회했지만 숫자를 찾지 못했습니다.
}
// wstring 버전 process_num 선언
// std::wstring process_num_w(std::wstring num, bool sino = true);
std::wstring process_num_w(std::wstring num, bool sino = true) {
    // 콤마 제거: wstring과 wregex 사용
    num = std::regex_replace(num, std::wregex(L","), L"");

    if (num == L"0") {
        return L"영";
    }
    if (!sino && num == L"20") {
        return L"스무";
    }

    // 초기화 맵: wchar_t와 wstring 사용
    std::map<wchar_t, std::wstring> digit2name;
    std::map<wchar_t, std::wstring> digit2mod;
    std::map<wchar_t, std::wstring> digit2dec;

    // 리터럴에 L 접두사 사용하여 wstring으로 변경
    std::wstring digits_w = L"123456789";
    std::wstring names[] = {L"일", L"이", L"삼", L"사", L"오", L"육", L"칠", L"팔", L"구"};
    std::wstring modifiers[] = {L"한", L"두", L"세", L"네", L"다섯", L"여섯", L"일곱", L"여덟", L"아홉"};
    std::wstring decimals[] = {L"", L"열", L"스물", L"서른", L"마흔", L"쉰", L"예순", L"일흔", L"여든", L"아흔"};

    for (size_t i = 0; i < digits_w.length(); ++i) {
        digit2name[digits_w[i]] = names[i];
        digit2mod[digits_w[i]] = modifiers[i];
    }
    for (size_t i = 0; i <= 9; ++i) {
        // char 대신 wchar_t를 map 키로 사용하려면,
        // std::to_wstring으로 변환 후 첫 번째 wchar_t를 사용합니다.
        digit2dec[std::to_wstring(i)[0]] = decimals[i];
    }

    std::vector<std::wstring> spelledout_reversed; // wstring 벡터로 변경

    for (int i = num.length() - 1; i >= 0; --i) {
        wchar_t digit = num[i]; // wchar_t로 변경
        int position = num.length() - 1 - i;
        std::wstring name = L""; // wstring으로 변경

        if (digit == L'0') { // L'0'으로 변경
            if (position % 4 == 0) {
                bool last_three_empty = true;
                if (!spelledout_reversed.empty()) {
                    int count_empty = 0;
                    for (int k = 0; k < std::min((int)spelledout_reversed.size(), 3); ++k) {
                        if (spelledout_reversed[k].empty()) {
                            count_empty++;
                        }
                    }
                    if (count_empty == std::min((int)spelledout_reversed.size(), 3)) {
                         last_three_empty = true;
                    } else {
                        last_three_empty = false;
                    }
                }
                if (last_three_empty) {
                    spelledout_reversed.push_back(L""); // L""으로 변경
                    continue;
                }
            } else {
                spelledout_reversed.push_back(L""); // L""으로 변경
                continue;
            }
        }

        if (sino) {
            if (position == 0) {
                name = digit2name.count(digit) ? digit2name[digit] : L"";
            } else if (position == 1) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"십";
                if (name == L"일십") { // L""으로 변경
                    name = L"십"; // L""으로 변경
                }
            } else if (position == 2) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"백";
                if (name == L"일백") { // L""으로 변경
                    name = L"백"; // L""으로 변경
                }
            } else if (position == 3) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"천";
                if (name == L"일천") { // L""으로 변경
                    name = L"천"; // L""으로 변경
                }
            } else if (position == 4) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"만";
                if (name == L"일만") { // L""으로 변경
                    name = L"만"; // L""으로 변경
                }
            } else if (position == 5) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"십";
                if (name == L"일십") { // L""으로 변경
                    name = L"십"; // L""으로 변경
                }
            } else if (position == 6) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"백";
                if (name == L"일백") { // L""으로 변경
                    name = L"백"; // L""으로 변경
                }
            } else if (position == 7) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"천";
                if (name == L"일천") { // L""으로 변경
                    name = L"천"; // L""으로 변경
                }
            } else if (position == 8) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"억";
            } else if (position == 9) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"십";
            } else if (position == 10) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"백";
            } else if (position == 11) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"천";
            } else if (position == 12) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"조";
            } else if (position == 13) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"십";
            } else if (position == 14) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"백";
            } else if (position == 15) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"천";
            }
        } else { // Pure Korean numerals
            if (position == 0) {
                name = digit2mod.count(digit) ? digit2mod[digit] : L"";
            } else if (position == 1) {
                name = digit2dec.count(digit) ? digit2dec[digit] : L"";
            }
            // Pure Korean (sino=False) for higher magnitudes
            else if (position == 2) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"백";
                if (name == L"일백") {
                    name = L"백";
                }
            } else if (position == 3) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"천";
                if (name == L"일천") {
                    name = L"천";
                }
            } else if (position == 4) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"만";
                if (name == L"일만") {
                    name = L"만";
                }
            } else if (position == 5) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"십";
                if (name == L"일십") {
                    name = L"십";
                }
            } else if (position == 6) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"백";
                if (name == L"일백") {
                    name = L"백";
                }
            } else if (position == 7) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"천";
                if (name == L"일천") {
                    name = L"천";
                }
            } else if (position == 8) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"억";
            } else if (position == 9) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"십";
            } else if (position == 10) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"백";
            } else if (position == 11) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"천";
            } else if (position == 12) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"조";
            } else if (position == 13) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"십";
            } else if (position == 14) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"백";
            } else if (position == 15) {
                name = (digit2name.count(digit) ? digit2name[digit] : L"") + L"천";
            }
        }
        spelledout_reversed.push_back(name);
    }

    std::reverse(spelledout_reversed.begin(), spelledout_reversed.end());

    std::wstring result = L""; // wstring으로 변경
    for (const std::wstring& s : spelledout_reversed) { // wstring 참조로 변경
        result += s;
    }
    return result;
}
std::wstring convertNumbersInText_w(const std::wstring& text, bool sino = true) {
    if(!containsDigit(text)){
        return text;
    }
    // wstring용 정규 표현식 (L"" 접두사)
    std::wregex num_regex(L"\\d[\\d,]*");

    std::wstring result = text;

    auto words_begin = std::wsregex_iterator(text.begin(), text.end(), num_regex);
    auto words_end = std::wsregex_iterator();

    std::vector<std::pair<std::wstring, std::wsmatch>> matches;
    for (std::wsregex_iterator i = words_begin; i != words_end; ++i) {
        matches.push_back({i->str(), *i});
    }
    std::reverse(matches.begin(), matches.end());

    for (const auto& match_pair : matches) {
        std::wstring num_str = match_pair.first;
        std::wsmatch match_info = match_pair.second;

        std::wstring converted_num = process_num_w(num_str, sino);

        result.replace(match_info.position(), match_info.length(), converted_num);
    }
    return result;
}
// 분리 함수
vector<wchar_t> decompose_hangul(wstring input) {
  vector<wchar_t> result;
  for (wchar_t ch : input) {
    if (is_hangul_syllable(ch)) {
      int offset = ch - JAMO_BASE;
      int lead = offset / (NUM_VOWELS * NUM_TAILS);
      int vowel = (offset % (NUM_VOWELS * NUM_TAILS)) / NUM_TAILS;
      int tail = offset % NUM_TAILS;

      result.push_back(LEAD_BASE + lead);
      result.push_back(VOWEL_BASE + vowel);
      if (tail != 0) result.push_back(TAIL_BASE + tail);
    } else {
      result.push_back(ch);
    }
  }
  return result;
}
// 자모 -> 완성형 음절
wstring compose_hangul(const wstring &jamo_str) {
  wstring result;
  size_t i = 0;

  while (i < jamo_str.size()) {
    wchar_t lead = jamo_str[i];

    // 초성인지 확인
    if (lead < LEAD_BASE || lead >= LEAD_BASE + NUM_LEADS) {
      result += lead;
      ++i;
      continue;
    }

    if (i + 1 >= jamo_str.size()) break;
    wchar_t vowel = jamo_str[i + 1];

    if (vowel < VOWEL_BASE || vowel >= VOWEL_BASE + NUM_VOWELS) {
      result += lead;
      ++i;
      continue;
    }

    int lead_index = lead - LEAD_BASE;
    int vowel_index = vowel - VOWEL_BASE;
    int tail_index = 0;

    if (i + 2 < jamo_str.size()) {
      wchar_t tail = jamo_str[i + 2];
      if (tail >= TAIL_BASE + 1 && tail < TAIL_BASE + NUM_TAILS) {
        tail_index = tail - TAIL_BASE;
        i += 3;
      } else {
        i += 2;
      }
    } else {
      i += 2;
    }

    wchar_t composed = JAMO_BASE + (lead_index * NUM_VOWELS * NUM_TAILS) +
                       (vowel_index * NUM_TAILS) + tail_index;
    result += composed;
  }

  return result;
}
std::wstring utf8_to_wstring(const std::string& str) {
    // Using codecvt_utf8_utf16 for wchar_t (typically 2 or 4 bytes)
    // The locale on some systems might need to be set for this to work robustly.
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    return converter.from_bytes(str);
}
std::string wstring_to_utf8(const std::wstring& wstr) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    return converter.to_bytes(wstr);
}

std::wstring decomposed_and_compose(std::wstring origin) {
    auto decompose_out = decompose_hangul(origin);
    wstring result;
    result.insert(result.end(), decompose_out.begin(), decompose_out.end());
    return result;
}
wstring replace_pairs_w(const wstring& inp, const vector<pair<wstring, wstring>>& pairs) {
    wstring out = decomposed_and_compose(inp);
    for (const auto& [from, to] : pairs) {
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != wstring::npos) {
            out.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
    return out;
}

// std::wstring utf8_to_wstring(const std::string &str) {
//   std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
//   return conv.from_bytes(str);
// }
// std::string wstring_to_utf8(const std::wstring &wstr) {
//   std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
//   return conv.to_bytes(wstr);
// }
std::map<std::string, std::string> rule_id2text_map;

// --- gloss 함수 (로깅/디버깅용) ---
void gloss(bool verbose, const std::wstring &out, const std::wstring &inp,
           const std::string &rule) {
  if (verbose) {
    if (out != inp) {
      SHERPA_ONNX_LOGE("Rule: %s", rule.c_str());
      SHERPA_ONNX_LOGE("\tChanged: '%s -> %s'", wstring_to_utf8(inp).c_str(), wstring_to_utf8(out).c_str());
    }
  }
}



// link1: 홑받침 또는 쌍받침 + ᄋ → 초성으로 변경 (wstring 버전)
std::wstring link1_w(const std::wstring &inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["13"];
    std::vector<std::pair<std::wstring, std::wstring>> pairs = {
        {L"ᆨᄋ", L"ᄀ"}, {L"ᆩᄋ", L"ᄁ"}, {L"ᆫᄋ", L"ᄂ"}, {L"ᆮᄋ", L"ᄃ"}, {L"ᆯᄋ", L"ᄅ"},
        {L"ᆷᄋ", L"ᄆ"}, {L"ᆸᄋ", L"ᄇ"}, {L"ᆺᄋ", L"ᄉ"}, {L"ᆻᄋ", L"ᄊ"}, {L"ᆽᄋ", L"ᄌ"},
        {L"ᆾᄋ", L"ᄎ"}, {L"ᆿᄋ", L"ᄏ"}, {L"ᇀᄋ", L"ᄐ"}, {L"ᇁᄋ", L"ᄑ"},
    };
    std::wstring out = replace_pairs_w(inp, pairs);
    gloss(verbose, (out), (inp), rule);
    return out;
}

// link2: 겹받침 + ᄋ → 초성 2개로 분해 (wstring 버전)
std::wstring link2_w(const std::wstring &inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["14"];
    std::vector<std::pair<std::wstring, std::wstring>> pairs = {
        {L"ᆪᄋ", L"ᆨᄊ"}, {L"ᆬᄋ", L"ᆫᄌ"}, {L"ᆰᄋ", L"ᆯᄀ"},
        {L"ᆱᄋ", L"ᆯᄆ"}, {L"ᆲᄋ", L"ᆯᄇ"}, {L"ᆳᄋ", L"ᆯᄊ"},
        {L"ᆴᄋ", L"ᆯᄐ"}, {L"ᆵᄋ", L"ᆯᄑ"}, {L"ᆹᄋ", L"ᆸᄊ"},

        // test
        {L"ᆲ", L"ᆯ"},
        {L"ᆪ", L"ㄱ"},
        {L"ᆹ", L"ㅂ"},
        // {L"ᆰ", L"ᆯ"},
    };
    std::wstring out = replace_pairs_w(inp, pairs);
    gloss(verbose, (out), (inp), rule);
    return out;
}

// link3: 받침 + 공백 + ᄋ 처리 (wstring 버전)
std::wstring link3_w(const std::wstring &inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["15"];
    std::vector<std::pair<std::wstring, std::wstring>> pairs = {
        {L"ᆨ ᄋ", L" ᄀ"},  {L"ᆩ ᄋ", L" ᄁ"},  {L"ᆫ ᄋ", L" ᄂ"},  {L"ᆮ ᄋ", L" ᄃ"},
        {L"ᆯ ᄋ", L" ᄅ"},  {L"ᆷ ᄋ", L" ᄆ"},  {L"ᆸ ᄋ", L" ᄇ"},  {L"ᆺ ᄋ", L" ᄉ"},
        {L"ᆻ ᄋ", L" ᄊ"},  {L"ᆽ ᄋ", L" ᄌ"},  {L"ᆾ ᄋ", L" ᄎ"},  {L"ᆿ ᄋ", L" ᄏ"},
        {L"ᇀ ᄋ", L" ᄐ"},  {L"ᇁ ᄋ", L" ᄑ"},  {L"ᆪ ᄋ", L"ᆨ ᄊ"}, {L"ᆬ ᄋ", L"ᆫ ᄌ"},
        {L"ᆰ ᄋ", L"ᆯ ᄀ"}, {L"ᆱ ᄋ", L"ᆯ ᄆ"}, {L"ᆲ ᄋ", L"ᆯ ᄇ"}, {L"ᆳ ᄋ", L"ᆯ ᄊ"},
        {L"ᆴ ᄋ", L"ᆯ ᄐ"}, {L"ᆵ ᄋ", L"ᆯ ᄑ"}, {L"ᆹ ᄋ", L"ᆸ ᄊ"},
    };
    std::wstring out = replace_pairs_w(inp, pairs);
    gloss(verbose, (out), (inp), rule);
    return out;
}

// link4: ㅎ+ᄋ → 생략 규칙 (wstring 버전)
std::wstring link4_w(const std::wstring &inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["12.4"];
    std::vector<std::pair<std::wstring, std::wstring>> pairs = {
        {L"ᇂᄋ", L"ᄋ"}, {L"ᆭᄋ", L"ᄂ"}, {L"ᆶᄋ", L"ᄅ"}
    };
    std::wstring out = replace_pairs_w(inp, pairs);
    gloss(verbose, (out), (inp), rule);
    return out;
}
/// 파이썬의 jyeo 함수 대응 (wstring 버전)
std::wstring jyeo_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["5.1"];

    std::wstring _inp = decomposed_and_compose(inp);
    std::wstring out = std::regex_replace(_inp, std::wregex(L"([ᄌᄍᄎ])ᅧ"), L"$1ᅥ");
    gloss(verbose, (out), (inp), rule);
    return out;
}

// 파이썬의 ye 함수 대응 (wstring 버전)
std::wstring ye_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["5.2"];
    std::wstring out = inp;
    if (descriptive) {
        std::wstring _inp = decomposed_and_compose(inp);
        out = std::regex_replace(_inp, std::wregex(L"([ᄀᄁᄃᄄᄅᄆᄇᄈᄌᄍᄎᄏᄐᄑᄒ])ᅨ"), L"$1ᅦ");
    }
    gloss(verbose, (out), (inp), rule);
    return out;
}

// 파이썬의 consonant_ui 함수 대응 (wstring 버전)
std::wstring consonant_ui_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["5.3"];
    std::wstring _inp = decomposed_and_compose(inp);
    std::wstring out = std::regex_replace(_inp, std::wregex(L"([ᄀᄁᄂᄃᄄᄅᄆᄇᄈᄉᄊᄌᄍᄎᄏᄐᄑᄒ])ᅴ"), L"$1ᅵ");
    gloss(verbose, (out), (inp), rule);
    return out;
}

// 파이썬의 josa_ui 함수 대응 (wstring 버전)
std::wstring josa_ui_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["5.4.2"];
    std::wstring out = decomposed_and_compose(inp);
    if (descriptive) {
        out = std::regex_replace(inp, std::wregex(L"의/J"), L"에");
    }
    else {
        size_t pos = out.find(L"/J");
        if (pos != std::wstring::npos) {
            out.replace(pos, 2, L"");
        }
    }
    gloss(verbose, (out), (inp), rule);
    return out;
}

// 파이썬의 vowel_ui 함수 대응 (wstring 버전)
std::wstring vowel_ui_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["5.4.1"];
    std::wstring out = inp;
    if (descriptive) {
        std::wstring _inp = decomposed_and_compose(inp);
        out = std::regex_replace(_inp, std::wregex(L"(.ᄋ)ᅴ"), L"$1ᅵ");
    }
    gloss(verbose, (out), (inp), rule);
    return out;
}

// 파이썬의 jamo 함수 대응 (wstring 버전)
std::wstring jamo_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["16"];
    std::wstring out = decomposed_and_compose(inp);

    out = std::regex_replace(out, std::wregex(L"([그])ᆮᄋ"), L"$1ᄉ");
    out = std::regex_replace(out, std::wregex(L"([으])[ᆽᆾᇀᇂ]ᄋ"), L"$1ᄉ");
    out = std::regex_replace(out, std::wregex(L"([으])[ᆿ]ᄋ"), L"$1ᄀ");
    out = std::regex_replace(out, std::wregex(L"([으])[ᇁ]ᄋ"), L"$1ᄇ");

    gloss(verbose, (out), (inp), rule);
    return out;
}

// 파이썬의 rieulgiyeok 함수 대응 (wstring 버전)
std::wstring rieulgiyeok_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["11.1"];
    std::wstring _inp = decomposed_and_compose(inp);
    std::wstring out = std::regex_replace(_inp, std::wregex(L"ᆰ/P([ᄀᄁ])"), L"ᆯᄁ");
    gloss(verbose, (out), (inp), rule);
    return out;
}

// 파이썬의 rieulbieub 함수 대응 (wstring 버전)
std::wstring rieulbieub_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["25"];
    std::wstring out = decomposed_and_compose(inp);

    out = std::regex_replace(out, std::wregex(L"([ᆲᆴ])/Pᄀ"), L"$1ᄁ");
    out = std::regex_replace(out, std::wregex(L"([ᆲᆴ])/Pᄃ"), L"$1ᄄ");
    out = std::regex_replace(out, std::wregex(L"([ᆲᆴ])/Pᄉ"), L"$1ᄊ");
    out = std::regex_replace(out, std::wregex(L"([ᆲᆴ])/Pᄌ"), L"$1ᄍ");

    gloss(verbose, (out), (inp), rule);
    return out;
}

// 파이썬의 verb_nieun 함수 대응 (wstring 버전)
std::wstring verb_nieun_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["24"];
    std::wstring out = decomposed_and_compose(inp);

    out = std::regex_replace(out, std::wregex(L"([ᆫᆷ])/Pᄀ"), L"$1ᄁ");
    out = std::regex_replace(out, std::wregex(L"([ᆫᆷ])/Pᄃ"), L"$1ᄄ");
    out = std::regex_replace(out, std::wregex(L"([ᆫᆷ])/Pᄉ"), L"$1ᄊ");
    out = std::regex_replace(out, std::wregex(L"([ᆫᆷ])/Pᄌ"), L"$1ᄍ");

    out = std::regex_replace(out, std::wregex(L"ᆬ/Pᄀ"), L"ᆫᄁ");
    out = std::regex_replace(out, std::wregex(L"ᆬ/Pᄃ"), L"ᆫᄄ");
    out = std::regex_replace(out, std::wregex(L"ᆬ/Pᄉ"), L"ᆫᄊ");
    out = std::regex_replace(out, std::wregex(L"ᆬ/Pᄌ"), L"ᆫᄍ");

    out = std::regex_replace(out, std::wregex(L"ᆱ/Pᄀ"), L"ᆷᄁ");
    out = std::regex_replace(out, std::wregex(L"ᆱ/Pᄃ"), L"ᆷᄄ");
    out = std::regex_replace(out, std::wregex(L"ᆱ/Pᄉ"), L"ᆷᄊ");
    out = std::regex_replace(out, std::wregex(L"ᆱ/Pᄌ"), L"ᆷᄍ");

    gloss(verbose, (out), (inp), rule);
    return out;
}

// balb 함수 대응 (wstring 버전)
std::wstring balb_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["10.1"];
    std::wstring out = decomposed_and_compose(inp);

    out = std::regex_replace(out, std::wregex(L"(바)ᆲ($|[^ᄋᄒ])"), L"$1ᆸ$2");
    out = std::regex_replace(out, std::wregex(L"(너)ᆲ([ᄌᄍ]ᅮ|[ᄃᄄ]ᅮ)"), L"$1ᆸ$2");

    gloss(verbose, (out), (inp), rule);
    return out;
}

// palatalize 함수 대응 (wstring 버전)
std::wstring palatalize_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::wstring out = decomposed_and_compose(inp);

    std::wregex r1(L"ᆮᄋ([ᅵᅧ])");
    std::wregex r2(L"ᇀᄋ([ᅵᅧ])");
    std::wregex r3(L"ᆴᄋ([ᅵᅧ])");
    std::wregex r4(L"ᆮᄒ([ᅵ])");

    out = std::regex_replace(out, r1, L"ᄌ$1");
    out = std::regex_replace(out, r2, L"ᄎ$1");
    out = std::regex_replace(out, r3, L"ᆯᄎ$1");
    out = std::regex_replace(out, r4, L"ᄎ$1");

    gloss(verbose, (out), (inp), rule_id2text_map["17"]);
    return out;
}

// 파이썬의 modifying_rieul 함수 대응 (wstring 버전)
std::wstring modifying_rieul_w(std::wstring inp, bool descriptive = false, bool verbose = false) {
    std::string rule = rule_id2text_map["27"];
    std::wstring out = decomposed_and_compose(inp);

    out = std::regex_replace(out, std::wregex(L"ᆯ/E ᄀ"), L"ᆯ ᄁ");
    out = std::regex_replace(out, std::wregex(L"ᆯ/E ᄃ"), L"ᆯ ᄄ");
    out = std::regex_replace(out, std::wregex(L"ᆯ/E ᄇ"), L"ᆯ ᄈ");
    out = std::regex_replace(out, std::wregex(L"ᆯ/E ᄉ"), L"ᆯ ᄊ");
    out = std::regex_replace(out, std::wregex(L"ᆯ/E ᄌ"), L"ᆯ ᄍ");

    out = std::regex_replace(out, std::wregex(L"ᆯ걸"), L"ᆯ껄");
    out = std::regex_replace(out, std::wregex(L"ᆯ밖에"), L"ᆯ빠께");
    out = std::regex_replace(out, std::wregex(L"ᆯ세라"), L"ᆯ쎄라");
    out = std::regex_replace(out, std::wregex(L"ᆯ수록"), L"ᆯ쑤록");
    out = std::regex_replace(out, std::wregex(L"ᆯ지라도"), L"ᆯ찌라도");
    out = std::regex_replace(out, std::wregex(L"ᆯ지언정"), L"ᆯ찌언정");
    out = std::regex_replace(out, std::wregex(L"ᆯ진대"), L"ᆯ찐대");

    gloss(verbose, (out), (inp), rule);
    return out;
}
// ############################ vowels ###########################

using RuleEntry = tuple<wstring, wstring, vector<wstring>>;
vector<RuleEntry> table;
// tuple: (str1, str2, rule_ids)
std::wstring fix_replacement_backrefs_w(const std::wstring& replacement) {
    std::wstring fixed;
    fixed.reserve(replacement.size());

    for (size_t i = 0; i < replacement.size(); ++i) {
        if (replacement[i] == L'\\' && i + 1 < replacement.size()) {
            wchar_t next = replacement[i + 1];
            if (next >= L'0' && next <= L'9') {
                fixed += L'$';
                fixed += next;
                ++i; // Skip next character
                continue;
            }
        }
        fixed += replacement[i];
    }
    return fixed;
}
// std::string fix_replacement_backrefs(const std::string &replacement) {
//   std::string fixed;
//   fixed.reserve(replacement.size());

//   for (size_t i = 0; i < replacement.size(); ++i) {
//     if (replacement[i] == '\\' && i + 1 < replacement.size()) {
//       char next = replacement[i + 1];
//       if (next >= '0' && next <= '9') {
//         // \숫자 -> $숫자 로 변경
//         fixed += '$';
//         fixed += next;
//         ++i;  // 다음 문자 건너뜀
//         continue;
//       }
//     }
//     fixed += replacement[i];
//   }
//   return fixed;
// }
std::vector<RuleEntry> parse_table_csv_hardcoded() {
  
    std::vector<RuleEntry> table;

    // CSV 데이터를 하드코딩된 문자열로 정의
    // (\W|$) 맨 마지막 항목 지움(테스트)
    // locale utf korean으로 하면
//    std::locale::global(std::locale("ko_KR.UTF-8")); // 한글 출력 가능하도록 설정 이것과 연관
    const std::string hardcoded_csv_data = R"(,( ?)ᄒ,( ?)ᄀ,( ?)ᄁ,( ?)ᄂ,( ?)ᄃ,( ?)ᄄ,( ?)ᄅ,( ?)ᄆ,( ?)ᄇ,( ?)ᄈ,( ?)ᄉ,( ?)ᄊ,( ?)ᄌ,( ?)ᄍ,( ?)ᄎ,( ?)ᄏ,( ?)ᄐ,( ?)ᄑ,
ᇂ,\1ᄒ,\1ᄏ(12),\1ᄁ,ᆫ\1ᄂ(12),\1ᄐ(12),\1ᄄ,\1ᄅ,\1ᄆ,\1ᄇ,\1ᄈ,\1ᄊ(12),\1ᄊ,\1ᄎ(12),\1ᄍ,\1ᄎ,\1ᄏ,\1ᄐ,\1ᄑ,ᆮ\1
ᆨ,\1ᄏ(12),ᆨ\1ᄁ(23),,ᆼ\1ᄂ(18),ᆨ\1ᄄ(23),,ᆼ\1ᄂ(19/18),ᆼ\1ᄆ(18),ᆨ\1ᄈ(23),,ᆨ\1ᄊ(23),,ᆨ\1ᄍ(23),,,,,,
ᆩ,\1ᄏ,ᆨ\1ᄁ(9/23),ᆨ\1ᄁ(9),ᆼ\1ᄂ(18),ᆨ\1ᄄ(9/23),ᆨ\1ᄄ(9),ᆼ\1ᄂ,ᆼ\1ᄆ(18),ᆨ\1ᄈ(9/23),ᆨ\1ᄈ(9),ᆨ\1ᄊ(9/23),ᆨ\1ᄊ(9),ᆨ\1ᄍ(9/23),ᆨ\1ᄍ(9),ᆨ\1ᄎ(9),ᆨ\1ᄏ(9),ᆨ\1ᄐ(9),ᆨ\1ᄑ(9),ᆨ\1(9)
ᆪ,\1ᄏ,ᆨ\1ᄁ(9/23),ᆨ\1ᄁ(10),ᆼ\1ᄂ(18),ᆨ\1ᄄ(9/23),ᆨ\1ᄄ(10),ᆼ\1ᄂ,ᆼ\1ᄆ(18),ᆨ\1ᄈ(9/23),ᆨ\1ᄈ(10),ᆨ\1ᄊ(9/23),ᆨ\1ᄊ(10),ᆨ\1ᄍ(9/23),ᆨ\1ᄍ(10),ᆨ\1ᄎ(10),ᆨ\1ᄏ(10),ᆨ\1ᄐ(10),ᆨ\1ᄑ(10),ᆨ\1(10)
ᆫ,,,,,,,ᆯ\1ᄅ(20),,,,,,,,,,,,
ᆬ,ᆫ\1ᄎ(12),ᆫ\1ᄀ(10),ᆫ\1ᄁ(10),ᆫ\1ᄂ(10),ᆫ\1ᄃ(10),ᆫ\1ᄄ(10),ᆯ\1ᄅ(10/20),ᆫ\1ᄆ(10),ᆫ\1ᄇ(10),ᆫ\1ᄈ(10),ᆫ\1ᄉ(10),ᆫ\1ᄊ(10),ᆫ\1ᄌ(10),ᆫ\1ᄍ(10),ᆫ\1ᄎ(10),ᆫ\1ᄏ(10),ᆫ\1ᄐ(10),ᆫ\1ᄑ(10),ᆫ\1(10)
ᆭ,ᆫ\1ᄒ,ᆫ\1ᄏ(12),ᆫ\1ᄁ,ᆫ\1ᄂ(12),ᆫ\1ᄐ(12),ᆫ\1ᄄ,ᆯ\1ᄅ,ᆫ\1ᄆ,ᆫ\1ᄇ,ᆫ\1ᄈ,ᆫ\1ᄊ(12),ᆫ\1ᄊ,ᆫ\1ᄎ(12),ᆫ\1ᄍ,ᆫ\1ᄎ,ᆫ\1ᄏ,ᆫ\1ᄐ,ᆫ\1ᄑ,ᆫ\1
ᆮ,\1ᄐ(12),ᆮ\1ᄁ(23),,ᆫ\1ᄂ(18),ᆮ\1ᄄ(23),,ᆫ\1ᄂ,ᆫ\1ᄆ(18),ᆮ\1ᄈ(23),,ᆮ\1ᄊ(23),,ᆮ\1ᄍ(23),,,,,,
ᆯ,,,,ᆯ\1ᄅ(20),,,,,,,,,,,,,,,
ᆰ,ᆯ\1ᄏ(12),ᆨ\1ᄁ(11/23),ᆨ\1ᄁ(11),ᆼ\1ᄂ(11/18),ᆨ\1ᄄ(11/23),ᆨ\1ᄄ(11),ᆼ\1ᄂ(11/18),ᆼ\1ᄆ(11/18),ᆨ\1ᄈ(11/23),ᆨ\1ᄈ(11),ᆨ\1ᄊ(11/23),ᆨ\1ᄊ(11),ᆨ\1ᄍ(11/23),ᆨ\1ᄍ(11),ᆨ\1ᄎ(11),1),ᆨ\1ᄑ(11),,ᆨ\1(11)
ᆱ,ᆷ\1ᄒ(11),ᆷ\1ᄀ(11),ᆷ\1ᄁ(11),ᆷ\1ᄂ(11),ᆷ\1ᄃ(11),ᆷ\1ᄄ(11),ᆷ\1ᄅ(11),ᆷ\1ᄆ(11),ᆷ\1ᄇ(11),ᆷ\1ᄈ(11),ᆷ\1ᄉ(11),ᆷ\1ᄊ(11),ᆷ\1ᄌ(11),ᆷ\1ᄍ(11),ᆷ\1ᄎ(11),ᆷ\1ᄏ(11),ᆷ\1ᄐ(11),ᆷ\1ᄑ(11),ᆷ\1(11)
ᆲ,ᆯ\1ᄑ(12),ᆯ\1ᄁ(10/23),ᆯ\1ᄁ(10),ᆷ\1ᄂ(18),ᆯ\1ᄄ(10/23),ᆯ\1ᄄ(10),ᆯ\1ᄅ(10),ᆷ\1ᄆ(18),ᆯ\1ᄈ(10/23),ᆯ\1ᄈ(10),ᆯ\1ᄊ(10/23),ᆯ\1ᄊ(10),ᆯ\1ᄍ(10/23),ᆯ\1ᄍ(10),ᆯ\1ᄎ(10),ᆯ\1ᄏ(10)0),,,ᆯ\1(10)
ᆳ,ᆯ\1ᄒ(10),ᆯ\1ᄁ(10/23),ᆯ\1ᄁ(10),ᆯ\1ᄅ(10/20),ᆯ\1ᄄ(10/23),ᆯ\1ᄄ(10),ᆯ\1ᄅ(10),ᆯ\1ᄆ(10),ᆯ\1ᄈ(10/23),ᆯ\1ᄈ(10),ᆯ\1ᄊ(10/23),ᆯ\1ᄊ(10),ᆯ\1ᄍ(10/23),ᆯ\1ᄍ(10),ᆯ\1ᄎ(10),ᆯ\1ᄏ(ᄑ(10),,,ᆯ\1(10)
ᆴ,ᆯ\1ᄒ(10),ᆯ\1ᄀ(10),ᆯ\1ᄁ(10),ᆯ\1ᄅ(10/20),ᆯ\1ᄃ(10),ᆯ\1ᄄ(10),ᆯ\1ᄅ(10),ᆯ\1ᄆ(10),ᆯ\1ᄇ(10),ᆯ\1ᄈ(10),ᆯ\1ᄉ(10),ᆯ\1ᄊ(10),ᆯ\1ᄌ(10),ᆯ\1ᄍ(10),ᆯ\1ᄎ(10),ᆯ\1ᄏ(10),ᆯ\1ᄐ(10),ᆯ\1ᄑ(10),ᆯ\1(10)
ᆵ,ᆸ\1ᄑ(11/12),ᆸ\1ᄁ(11/23),ᆸ\1ᄁ(11),ᆷ\1ᄂ(18),ᆸ\1ᄄ(11/23),ᆸ\1ᄄ(11),ᆷ\1ᄅ(11),ᆷ\1ᄆ(11/18),ᆸ\1ᄈ(11/23),ᆸ\1ᄈ(11),ᆸ\1ᄊ(11/23),ᆸ\1ᄊ(11),ᆸ\1ᄍ(11/23),ᆸ\1ᄍ(11),ᆸ\1ᄎ(11),ᆸ\1ᆸ\1ᄑ,,,ᆸ\1(11)
ᆶ,ᆯ\1ᄒ(10),ᆯ\1ᄏ(12),ᆯ\1ᄁ,ᆯ\1ᄅ(12/20),ᆯ\1ᄐ(12),ᆯ\1ᄄ,ᆯ\1ᄅ,ᆯ\1ᄆ,ᆯ\1ᄇ,ᆯ\1ᄈ,ᆯ\1ᄊ(12),ᆯ\1ᄊ,ᆯ\1ᄎ(12),ᆯ\1ᄍ,ᆯ\1ᄎ,ᆯ\1ᄏ,ᆯ\1ᄐ,ᆯ\1ᄑ,ᆯ
ᆷ,,,,,,,ᆷ\1ᄂ(19),,,,,,,,,,,,
ᆸ,\1ᄑ(12),ᆸ\1ᄁ(23),,ᆷ\1ᄂ(18),ᆸ\1ᄄ(23),,ᆷ\1ᄂ(19/18),ᆷ\1ᄆ(18),ᆸ\1ᄈ(23),,ᆸ\1ᄊ(23),,ᆸ\1ᄍ(23),,,,,,
ᆹ,ᆸ\1ᄑ(10),ᆸ\1ᄁ(10/23),ᆸ\1ᄁ(10),ᆷ\1ᄂ(10/18),ᆸ\1ᄄ(10/23),ᆸ\1ᄄ(10),ᆷ\1ᄂ(10/19/18),ᆷ\1ᄆ(10/18),ᆸ\1ᄈ(10/23),ᆸ\1ᄈ(10),ᆸ\1ᄊ(10/23),ᆸ\1ᄊ(10),ᆸ\1ᄍ(10/23),ᆸ\1ᄍ(10),ᆸ\1ᄎ(1ᄐ(10),ᆸ\1ᄑ,,,ᆸ\1(10)
ᆺ,\1ᄐ,ᆮ\1ᄁ(9/23),ᆮ\1ᄁ(9),ᆫ\1ᄂ(9/18),ᆮ\1ᄄ(9/23),ᆮ\1ᄄ(9),ᆫ\1ᄂ(9/18),ᆫ\1ᄆ(9/18),ᆮ\1ᄈ(9/23),ᆮ\1ᄈ(9),ᆮ\1ᄊ(9/23),ᆮ\1ᄊ(9),ᆮ\1ᄍ(9/23),ᆮ\1ᄍ(9),ᆮ\1ᄎ(9),ᆮ\1ᄏ(9),ᆮ\1ᄐ(9),ᆮ\1ᄑ(9),ᆮ\1(9)
ᆻ,\1ᄐ,ᆮ\1ᄁ(9/23),ᆮ\1ᄁ(9),ᆫ\1ᄂ(9/18),ᆮ\1ᄄ(9/23),ᆮ\1ᄄ(9),ᆫ\1ᄂ(9/18),ᆫ\1ᄆ(9/18),ᆮ\1ᄈ(9/23),ᆮ\1ᄈ(9),ᆮ\1ᄊ(9/23),ᆮ\1ᄊ(9),ᆮ\1ᄍ(9/23),ᆮ\1ᄍ(9),ᆮ\1ᄎ(9),ᆮ\1ᄏ(9),ᆮ\1ᄐ(9),ᆮ1ᄑ(9),ᆮ\1(9)
ᆼ,,,,,,,ᆼ\1ᄂ(19),,,,,,,,,,,,
ᆽ,\1ᄎ(12),ᆮ\1ᄁ(9/23),ᆮ\1ᄁ(9),ᆫ\1ᄂ(18),ᆮ\1ᄄ(9/23),ᆮ\1ᄄ(9),ᆫ\1ᄂ(9/18),ᆫ\1ᄆ(18),ᆮ\1ᄈ(9/23),ᆮ\1ᄈ(9),ᆮ\1ᄊ(9/23),ᆮ\1ᄊ(9),ᆮ\1ᄍ(9/23),ᆮ\1ᄍ(9),ᆮ\1ᄎ(9),ᆮ\1ᄏ(9),ᆮ\1ᄐ(9),ᆮ\1ᄑ(9),ᆮ\1(9)
ᆾ,\1ᄐ,ᆮ\1ᄁ(9/23),ᆮ\1ᄁ(9),ᆫ\1ᄂ(18),ᆮ\1ᄄ(9/23),ᆮ\1ᄄ(9),ᆫ\1ᄂ(9/18),ᆫ\1ᄆ(18),ᆮ\1ᄈ(9/23),ᆮ\1ᄈ(9),ᆮ\1ᄊ(9/23),ᆮ\1ᄊ(9),ᆮ\1ᄍ(9/23),ᆮ\1ᄍ(9),ᆮ\1ᄎ(9),ᆮ\1ᄏ(9),ᆮ\1ᄐ(9),ᆮ\1ᄑ(9),ᆮ\1(9)
ᆿ,\1ᄏ,ᆨ\1ᄁ(9/23),ᆨ\1ᄁ(9),ᆼ\1ᄂ(18),ᆨ\1ᄄ(9/23),ᆨ\1ᄄ(9),ᆼ\1ᄂ(9/18),ᆼ\1ᄆ(9/18),ᆨ\1ᄈ(9/23),ᆨ\1ᄈ(9),ᆨ\1ᄊ(9/23),ᆨ\1ᄊ(9),ᆨ\1ᄍ(9/23),ᆨ\1ᄍ(9),ᆨ\1ᄎ(9),ᆨ\1ᄏ(9),ᆨ\1ᄐ(9),ᆨ\1ᄑ(9),ᆨ\1(9)
ᇀ,\1ᄐ,ᆮ\1ᄁ(9/23),ᆮ\1ᄁ(9),ᆫ\1ᄂ(18),ᆮ\1ᄄ(9/23),ᆮ\1ᄄ(9),ᆫ\1ᄂ(9/18),ᆫ\1ᄆ(18),ᆮ\1ᄈ(9/23),ᆮ\1ᄈ(9),ᆮ\1ᄊ(9/23),ᆮ\1ᄊ(9),ᆮ\1ᄍ(9/23),ᆮ\1ᄍ(9),ᆮ\1ᄎ(9),ᆮ\1ᄏ(9),ᆮ\1ᄐ(9),ᆮ\1ᄑ(9),ᆮ\1(9)
ᇁ,\1ᄑ,ᆸ\1ᄁ(9/23),ᆸ\1ᄁ(9),ᆷ\1ᄂ(18),ᆸ\1ᄄ(9/23),ᆸ\1ᄄ(9),ᆫ\1ᄂ(9/18),ᆷ\1ᄆ(18),ᆸ\1ᄈ(9/23),ᆸ\1ᄈ(9),ᆸ\1ᄊ(9/23),ᆸ\1ᄊ(9),ᆸ\1ᄍ(9/23),ᆸ\1ᄍ(9),ᆸ\1ᄎ(9),ᆸ\1ᄏ(9),ᆸ\1ᄐ(9),ᆸ\1ᄑ(9),ᆸ\1(9)";

    std::stringstream ss_data(hardcoded_csv_data);
    std::string line;
    std::vector<std::string> onsets;

    // 첫 줄 (onset 목록)
    if (std::getline(ss_data, line)) {
        std::stringstream ss_line(line);
        std::string cell;
        while (std::getline(ss_line, cell, ',')) {
            onsets.push_back(cell);
        }
    }

    // 나머지 줄들 (coda + 각 onset 조합)
    while (std::getline(ss_data, line)) {
        std::stringstream ss_line(line);
        std::vector<std::string> cols;
        std::string cell;

        while (std::getline(ss_line, cell, ',')) {
            cols.push_back(cell);
        }

        if (cols.empty()) continue;
        std::string coda = cols[0];

        for (size_t i = 1; i < onsets.size(); ++i) {
            std::string cell_content = cols[i]; // 'cell'이 이미 위에 있으므로 다른 이름 사용
            if (cell_content.empty()) continue;

            std::string onset = onsets[i];
            std::string str1 = coda + onset;
            std::string str2;
            std::vector<std::wstring> rule_ids;

            // 괄호가 있으면 규칙 포함
            size_t pos = cell_content.find('(');
            if (pos != std::string::npos) {
                str2 = cell_content.substr(0, pos);
                std::string rule_str = cell_content.substr(pos + 1);
                if (!rule_str.empty() && rule_str.back() == ')') {
                    rule_str.pop_back();
                }

                std::stringstream rss(rule_str);
                std::string rule;
                while (std::getline(rss, rule, '/')) {
                    rule_ids.push_back(utf8_to_wstring(rule));
                }
            }
            else {
                str2 = cell_content;
            }

            // table.emplace_back(utf8_to_wstring( str1),utf8_to_wstring(str2), rule_ids);
            table.emplace_back(
                utf8_to_wstring(str1), utf8_to_wstring(str2), rule_ids
            );
        }
    }

    return table;
}

std::wstring remove_pjeb_tags_w(const std::wstring &inp) {
  return std::regex_replace(inp, std::wregex(L"/[PJEB]"), L"");
}
std::wstring apply_rule_step_w(
    const std::wstring& step_name,
    const std::function<std::wstring(const std::wstring&, bool, bool)>& rule_func,
    const std::wstring& input,
    bool descriptive)
{
    std::wstring output = rule_func(input, descriptive, false); // verbose=false 고정
    if (output != input) {
        SHERPA_ONNX_LOGE("[%s] 변환 전: %s\n", wstring_to_utf8(step_name).c_str(), wstring_to_utf8(input).c_str());
        SHERPA_ONNX_LOGE("[%s] 변환 후: %s\n\n", wstring_to_utf8(step_name).c_str(), wstring_to_utf8(output).c_str());
    }
    return output;
}
std::wstring apply_rules_w(std::wstring inp, bool descriptive = false) {
    std::wstring out = inp;

    out = apply_rule_step_w(L"jyeo_w", jyeo_w, out, descriptive);
    out = apply_rule_step_w(L"ye_w", ye_w, out, descriptive);
    out = apply_rule_step_w(L"consonant_ui_w", consonant_ui_w, out, descriptive);
    out = apply_rule_step_w(L"josa_ui_w", josa_ui_w, out, descriptive);
    out = apply_rule_step_w(L"vowel_ui_w", vowel_ui_w, out, descriptive);
    out = apply_rule_step_w(L"jamo_w", jamo_w, out, descriptive);
    out = apply_rule_step_w(L"rieulgiyeok_w", rieulgiyeok_w, out, descriptive);
    out = apply_rule_step_w(L"rieulbieub_w", rieulbieub_w, out, descriptive);
    out = apply_rule_step_w(L"verb_nieun_w", verb_nieun_w, out, descriptive);
    out = apply_rule_step_w(L"balb_w", balb_w, out, descriptive);
    out = apply_rule_step_w(L"palatalize_w", palatalize_w, out, descriptive);
    out = apply_rule_step_w(L"modifying_rieul_w", modifying_rieul_w, out, descriptive);

    // 태그 제거 - 직접 비교 출력
    {
        std::wstring before = out;
        out = remove_pjeb_tags_w(out);
        if (out != before) {
            SHERPA_ONNX_LOGE("[remove_pjeb_tags] 변환 전:%s" ,wstring_to_utf8(before).c_str() );
            SHERPA_ONNX_LOGE("[remove_pjeb_tags] 변환 후:%s" , wstring_to_utf8(out).c_str() );
        }
    }

    // 규칙 기반 치환
    for (const auto& [_wstr1, _wstr2, rule_ids] : table) {
        wstring wstr1 = _wstr1;
        wstring wstr2 = _wstr2;

        wstring wout = (out);

        wstring before = wout;
        //////////////
        std::wstring fixed_str2 = fix_replacement_backrefs_w(wstr2);

        try {
            wstring result = decomposed_and_compose(wout);

            wout = std::regex_replace(result, std::wregex(wstr1), fixed_str2);
            out = (wout);
        }
        catch (std::regex_error& e) {
            std::cerr << "[regex error] pattern: " << wstring_to_utf8(wstr1)
                << ", error: " << e.what() << std::endl;
            continue;
        }

        if (wout != before) {  // 변경이 있을 때만 출력
            SHERPA_ONNX_LOGE("[regex_replace] wstr1:%s,wstr2:%s<<" , wstring_to_utf8(wstr1).c_str(), wstring_to_utf8(wstr2).c_str());
            SHERPA_ONNX_LOGE("[regex_replace] 변환 전:%s<<" , wstring_to_utf8(before).c_str());
            SHERPA_ONNX_LOGE("[regex_replace] 변환 후:%s<<" , wstring_to_utf8(wout).c_str());

            std::string rule;
            if (!rule_ids.empty()) {
                for (const std::wstring& rule_id : rule_ids) {
                    auto it = rule_id2text_map.find(wstring_to_utf8(rule_id));
                    if (it != rule_id2text_map.end()) {
                        rule += it->second + "\n";
                    }
                }
            }

            gloss(true, out, (before), rule);  // gloss는 내부에서 필요한 출력 처리
        }
    }
    out = apply_rule_step_w(L"link1_w", link1_w, out, descriptive);
    out = apply_rule_step_w(L"link2_w", link2_w, out, descriptive);
    out = apply_rule_step_w(L"link3_w", link3_w, out, descriptive);
    out = apply_rule_step_w(L"link4_w", link4_w, out, descriptive);

    out = convertNumbersInText_w(out);
    return out;
}

// 유니코드 시퀀스 출력
// void print_unicode(const wstring &text) {
//   wcout << L"[";
//   for (size_t i = 0; i < text.size(); ++i) {
//     wcout << L"U+" << hex << uppercase << setw(4) << setfill(L'0')
//           << (int)text[i];
//     if (i != text.size() - 1) wcout << L", ";
//   }
//   wcout << L"]" << endl;
// }
std::unordered_map<wchar_t, int> jamo_to_id = {
    {L'!', 210},   // !
    {L'?', 211},   // ?
    {L'…', 212},   // …
    {L',', 213},   // ,
    {L'.', 214},   // .
    {L'\'', 215},  // '
    {L'-', 216},   // -
    {L' ', 217},   // SP (공백)
    // {L'�', 218},  // UNK (Replacement Character, U+FFFD)

    {L'ᄀ', 162},  // ㄱ
    {L'ᄁ', 163},  // ㄲ
    {L'ᄂ', 164},  // ㄴ
    {L'ᄃ', 165},  // ㄷ
    {L'ᄄ', 166},  // ㄸ
    {L'ᄅ', 167},  // ㄹ
    {L'ᄆ', 168},  // ㅁ
    {L'ᄇ', 169},  // ㅂ
    {L'ᄈ', 170},  // ㅃ
    {L'ᄉ', 171},  // ㅅ
    {L'ᄊ', 172},  // ㅆ
    {L'ᄋ', 173},  // ㅇ
    {L'ᄌ', 174},  // ㅈ
    {L'ᄍ', 175},  // ㅉ
    {L'ᄎ', 176},  // ㅊ
    {L'ᄏ', 177},  // ㅋ
    {L'ᄐ', 178},  // ㅌ
    {L'ᄑ', 179},  // ㅍ
    {L'ᄒ', 180},  // ㅎ

    {L'ᅡ', 181},  // ㅏ
    {L'ᅢ', 182},  // ㅐ
    {L'ᅣ', 183},  // ㅑ
    {L'ᅤ', 184},  // ㅒ
    {L'ᅥ', 185},  // ㅓ
    {L'ᅦ', 186},  // ㅔ
    {L'ᅧ', 187},  // ㅕ
    {L'ᅨ', 188},  // ㅖ
    {L'ᅩ', 189},  // ㅗ
    {L'ᅪ', 190},  // ㅘ
    {L'ᅫ', 191},  // ㅙ
    {L'ᅬ', 192},  // ㅚ
    {L'ᅭ', 193},  // ㅛ
    {L'ᅮ', 194},  // ㅜ
    {L'ᅯ', 195},  // ㅝ
    {L'ᅰ', 196},  // ㅞ
    {L'ᅱ', 197},  // ㅟ
    {L'ᅲ', 198},  // ㅠ
    {L'ᅳ', 199},  // ㅡ
    {L'ᅴ', 200},  // ㅢ
    {L'ᅵ', 201},  // ㅣ

    {L'ᆨ', 202},  // ㄱ
    {L'ᆫ', 203},  // ㄴ
    {L'ᆮ', 204},  // ㄷ
    {L'ᆯ', 205},  // ㄹ
    {L'ᆷ', 206},  // ㅁ
    {L'ᆸ', 207},  // ㅂ
    {L'ᆼ', 208},  // ㅇ
    {L'ㄸ', 209}  // ㄸ
};

// Hangul 분해된 자모 → 숫자 인덱스로 변환
vector<int64_t> convert_to_jamo_ids(const wstring &input,bool isFullSentence = false) {
  vector<wchar_t> decomposed = decompose_hangul(input);
  vector<int64_t> result;

  // 문장 시작을 위한 padding
  if(isFullSentence) {
  result.push_back(0);  // '_' == 0
  }

  for (wchar_t jamo : decomposed) {
    auto it = jamo_to_id.find(jamo);
    if (it != jamo_to_id.end()) {
      result.push_back(it->second);
    } else {
      // UNK 처리 or 스킵
      result.push_back(218);  // 예: UNK == 218
    }
  }

  if(isFullSentence) {
    result.push_back(0);  // '_' == 0
  }
  // 문장 끝 padding
  // result.push_back(0);  // '_' == 0

  return result;
}

std::vector<std::int64_t> AddZeroesPhone(
    const std::vector<std::int64_t> &original_vec) {
  std::vector<std::int64_t> transformed_vec;
  transformed_vec.reserve(original_vec.size() * 2 +
                          2);  // 미리 공간을 할당하여 효율성 증대

  // 1. 맨 앞에 0 추가
  // transformed_vec.push_back(0);

  // 2. 각 요소 앞에 0을 추가하고 원래 요소 추가
  for (std::int64_t element : original_vec) {
    transformed_vec.push_back(0);
    transformed_vec.push_back(element);
  }

  // 3. 맨 뒤에 0 추가
  transformed_vec.push_back(0);

  return transformed_vec;
}
std::vector<std::int64_t> CreateToneKo(size_t desired_size) {
  std::vector<std::int64_t> result_vec;
  result_vec.reserve(desired_size);  // 미리 공간을 할당하여 효율성 증대

  for (size_t i = 0; i < desired_size; ++i) {
    if (i % 2 == 0) {  // 인덱스가 짝수일 경우 0 추가
      result_vec.push_back(0);
    } else {  // 인덱스가 홀수일 경우 1 추가
      result_vec.push_back(11);
    }
  }
  return result_vec;
}
void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // 교체된 문자열의 길이만큼 다음 검색 시작 위치 이동
    }
}

vector<string> split(string str, char Delimiter) {
    istringstream iss(str);             // istringstream에 str을 담는다.
    string buffer;                      // 구분자를 기준으로 절삭된 문자열이 담겨지는 버퍼

    vector<string> result;

    // istringstream은 istream을 상속받으므로 getline을 사용할 수 있다.
    while (getline(iss, buffer, Delimiter)) {
        result.push_back(buffer);               // 절삭된 문자열을 vector에 저장
    }

    return result;
}
vector<wstring> split(const wstring &str, wchar_t delimiter) {
  vector<wstring> result;
  wstringstream ss(str);
  wstring item;
  while (getline(ss, item, delimiter)) {
    result.push_back(item);
  }
  return result;
}

// std::vector<int64_t> TokensToPhoneId(const std::vector<std::string> &tokens){
//   if(table.empty()){
//     table = parse_table_csv_hardcoded();
//   }
//   SHERPA_ONNX_LOGE(">>> TokensToPhoneId table size: %lu", table.size());
//  vector<string> out_applied_rules;
//   for(auto token : tokens){
//     // SHERPA_ONNX_LOGE(">>> TokensToPhoneId token: %s", token.c_str());
//     replaceAll(token, "#", "");
//      std::wstring text = utf8_to_wstring(token); 
//      std::vector<wchar_t> jamos = decompose_hangul(text);
//      std::wstring result(jamos.begin(), jamos.end());
//      std::string out = apply_rules(wstring_to_utf8(result));
//      out_applied_rules.push_back(out);

//   }
//     // 규칙 적용 (jyeo_wstring 포함)
//   std::wstring rs;
//   // std::int8_t idx = 0;
//   for (auto &out : out_applied_rules) {
//     rs += compose_hangul(utf8_to_wstring(out));
//   }
//   std::vector<int64_t> phone_ids = convert_to_jamo_ids(rs);
//   SHERPA_ONNX_LOGE(">>> TokensToPhoneId phone_ids size: %lu", phone_ids.size());
//   return phone_ids;
// }
std::string TextToPhone(const std::string& _text) {
    std::wstring text = utf8_to_wstring(_text);

    // 한글 분해
    std::vector<wchar_t> jamos = decompose_hangul(text);
    std::wstring result(jamos.begin(), jamos.end());
    vector<wstring> tokens = split(result, ' ');

    vector<wstring> out_applied_rules;
    for (auto& token : tokens) {
        wstring out = apply_rules_w(token);
        out_applied_rules.push_back(out);
    }
    // 규칙 적용 (jyeo_wstring 포함)
    std::wstring rs;
    std::vector<int8_t> word2ph;
    // std::int8_t idx = 0;
    std::wstring temp;
    std::wstring temp_str;
    for (auto& out : out_applied_rules) {
        temp = (out);
        temp_str = compose_hangul(temp);
        // std::cout << "temp_str.size" << temp_str.size() << endl;
        word2ph.push_back(temp_str.size());
        rs += temp_str;
        // std::wcout << L"out: " << temp << wstring_to_utf8(temp_str).size() << std::endl;
        // if (idx == 0) {
        // } else {
        //     rs += L" " + compose_hangul(utf8_to_wstring(out));
        // }
        // idx++;
    }


    // std::wcout << L"rs: " << rs << std::endl;
    return wstring_to_utf8(rs);
}
int64_t charToPhoneId(const wchar_t& _char) {
    auto it = jamo_to_id.find(_char);
    if (it != jamo_to_id.end()) {
        return it->second;
    }
    else {
        // UNK 처리 or 스킵
        return 218; // 예: UNK == 218
    }
}
std::vector<int64_t> TextToPhoneId(const std::string &_text, bool isFullSentence) {
  if (table.empty()) {
    // table = parse_table_csv("table.csv");
    table = parse_table_csv_hardcoded();
  }
  SHERPA_ONNX_LOGE(">>> TextToPhoneId table size: %lu", table.size());
  SHERPA_ONNX_LOGE(">>> TextToPhoneId %s", _text.c_str());
  // UTF-8 문자열을 wstring으로 변환
  std::wstring text = utf8_to_wstring(_text);
  SHERPA_ONNX_LOGE(">>> TextToPhoneId utf8_to_wstring text");

  // 한글 분해
  std::vector<wchar_t> jamos = decompose_hangul(text);
  SHERPA_ONNX_LOGE(">>> TextToPhoneId decompose_hangle");
  std::wstring result(jamos.begin(), jamos.end());
  SHERPA_ONNX_LOGE(">>> TextToPhoneId result");
  vector<wstring> tokens = split(result, ' ');

  SHERPA_ONNX_LOGE(">>> TextToPhoneId apply rules");
  vector<wstring> out_applied_rules;
  for (auto &token : tokens) {
    wstring out = apply_rules_w((token));
    out_applied_rules.push_back(out);
  }
  // 규칙 적용 (jyeo_wstring 포함)
  std::wstring rs;
  // std::int8_t idx = 0;
  for (auto &out : out_applied_rules) {
    rs += compose_hangul((out));
    // if (idx == 0) {
    // } else {
    //     rs += L" " + compose_hangul(utf8_to_wstring(out));
    // }
    // idx++;
  }
//   std::wcout << L"rs: " << rs << std::endl;
  // 규칙 적용 (jyeo_wstring 포함)
//   std::string rs_string = apply_rules(wstring_to_utf8(result));
//   SHERPA_ONNX_LOGE(">>> TextToPhoneId apply rules reult %s", rs_string.c_str());
//   std::wstring rs = compose_hangul(utf8_to_wstring(rs_string));
  SHERPA_ONNX_LOGE(">>> TextToPhoneId apply compose_hangle result");

  // 자모 ID로 변환
  std::vector<int64_t> phoneme_ids = convert_to_jamo_ids(rs, isFullSentence);
  SHERPA_ONNX_LOGE(">>> TextToPhoneId convert_to_jamo_ids");
  //   std::vector<int64_t> tone_ids(phoneme_ids.size(), 0);  // 톤 ID 기본값 0

  return std::vector<int64_t>(phoneme_ids.begin(), phoneme_ids.end());
}
// void ReadJaBert(std::vector<float> &ja_bert_vec) {

//     //  4.2533e-01,  4.2533e-01,  4.2533e-01, -2.6758e-01, -2.6758e-01,
//         //  -2.6758e-01, -2.6758e-01, -2.6758e-01, -2.6758e-01, -2.6758e-01,
//         //  -2.6758e-01, -2.6758e-01, -2.6758e-01, -2.6758e-01, -2.6758e-01,
//         //  -2.6758e-01, -2.6758e-01, -8.8685e-01, -8.8685e-01, -8.8685e-01,
//         //  -8.8685e-01, -8.8685e-01, -8.8685e-01, -8.8685e-01, -8.8685e-01,
//         //  -8.8685e-01, -8.8685e-01, -8.8685e-01, -8.8685e-01, -8.8685e-01,
//         //  -8.8685e-01, -8.8685e-01, -8.8685e-01, -4.5227e-01, -4.5227e-01,
//         //  -4.5227e-01, -4.5227e-01, -4.5227e-01, -4.5227e-01, -4.5227e-01,


//     // std::vector<float> ja_bert_vec;
//     std::ifstream file("ja_bert.txt");
//     if (file.is_open()) {
//         SHERPA_ONNX_LOGE(">>> ReadJaBert read ja_bert.txt");
//         std::string line;
//         while (std::getline(file, line)) {
//             auto v = split(line,',');
//             for(int i = 0; i < v.size(); i++) {
//                 ja_bert_vec.push_back(stof(v[i]));
//             }
//         }
//         file.close();
//     }
//     else {
//         std::cerr << "Failed to open ja_bert.txt" << std::endl;
//         SHERPA_ONNX_LOGE(">>> ReadJaBert Failed to open ja_bert.txt");
//     }
//     // return ja_bert_vec;

// }
bool containsDigit(const std::string& text) {
    // 문자열의 각 문자를 순회합니다.
    for (char ch : text) {
        // 현재 문자가 숫자인지 확인합니다.
        // isdigit 함수는 해당 문자가 0부터 9까지의 숫자를 나타내면 true를 반환합니다.
        if (std::isdigit(ch)) {
            return true; // 숫자를 찾으면 즉시 true를 반환하고 함수를 종료합니다.
        }
    }
    return false; // 문자열 전체를 순회했지만 숫자를 찾지 못했습니다.
}

std::string process_num(std::string num, bool sino = true) {
    // Remove commas from the number string
    num = std::regex_replace(num, std::regex(","), "");

    if (num == "0") {
        return "영";
    }
    if (!sino && num == "20") {
        return "스무";
    }

    // Initialize maps for digit to Korean name mappings
    std::map<char, std::string> digit2name;
    std::map<char, std::string> digit2mod;
    std::map<char, std::string> digit2dec;

    std::string digits = "123456789";
    std::string names[] = {"일", "이", "삼", "사", "오", "육", "칠", "팔", "구"};
    std::string modifiers[] = {"한", "두", "세", "네", "다섯", "여섯", "일곱", "여덟", "아홉"};
    std::string decimals[] = {"", "열", "스물", "서른", "마흔", "쉰", "예순", "일흔", "여든", "아흔"}; // Note: decimals[0] is empty for 0

    for (size_t i = 0; i < digits.length(); ++i) {
        digit2name[digits[i]] = names[i];
        digit2mod[digits[i]] = modifiers[i];
    }
    for (size_t i = 0; i <= 9; ++i) { // decimals map includes '0' to '9'
        digit2dec[std::to_string(i)[0]] = decimals[i];
    }

    std::vector<std::string> spelledout_reversed; // Build in reverse, then reverse back

    for (int i = num.length() - 1; i >= 0; --i) {
        char digit = num[i];
        int position = num.length() - 1 - i; // Position from the rightmost digit (0-indexed)
        std::string name = "";

        if (digit == '0') {
            // Handle zeros based on their position and context, similar to Python
            if (position % 4 == 0) { // Unit position (ones, thousands, ten thousands, etc.)
                bool last_three_empty = true;
                if (!spelledout_reversed.empty()) {
                    // Check if the last three non-empty elements are all empty (representing zeroes)
                    int count_empty = 0;
                    for (int k = 0; k < std::min((int)spelledout_reversed.size(), 3); ++k) {
                        if (spelledout_reversed[k].empty()) {
                            count_empty++;
                        }
                    }
                    if (count_empty == std::min((int)spelledout_reversed.size(), 3)) {
                         last_three_empty = true;
                    } else {
                        last_three_empty = false;
                    }
                }

                if (last_three_empty) {
                    spelledout_reversed.push_back("");
                    continue;
                }
            } else {
                spelledout_reversed.push_back("");
                continue;
            }
        }

        if (sino) {
            if (position == 0) { // Ones place
                name = digit2name.count(digit) ? digit2name[digit] : "";
            } else if (position == 1) { // Tens place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "십";
                if (name == "일십") {
                    name = "십";
                }
            } else if (position == 2) { // Hundreds place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "백";
                if (name == "일백") {
                    name = "백";
                }
            } else if (position == 3) { // Thousands place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "천";
                if (name == "일천") {
                    name = "천";
                }
            } else if (position == 4) { // Ten thousands place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "만";
                if (name == "일만") {
                    name = "만";
                }
            } else if (position == 5) { // Hundred thousands place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "십";
                if (name == "일십") {
                    name = "십";
                }
            } else if (position == 6) { // Millions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "백";
                if (name == "일백") {
                    name = "백";
                }
            } else if (position == 7) { // Ten millions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "천";
                if (name == "일천") {
                    name = "천";
                }
            } else if (position == 8) { // Billions place (억)
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "억";
            } else if (position == 9) { // Ten billions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "십";
            } else if (position == 10) { // Hundred billions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "백";
            } else if (position == 11) { // Trillions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "천";
            } else if (position == 12) { // Quadrillions place (조)
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "조";
            } else if (position == 13) { // Ten quadrillions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "십";
            } else if (position == 14) { // Hundred quadrillions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "백";
            } else if (position == 15) { // Quintillions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "천";
            }
        } else { // Pure Korean numerals
            if (position == 0) { // Ones place
                name = digit2mod.count(digit) ? digit2mod[digit] : "";
            } else if (position == 1) { // Tens place
                name = digit2dec.count(digit) ? digit2dec[digit] : "";
            }
            // For pure Korean, higher magnitudes usually don't have distinct modifiers
            // for "백", "천", "만", "억", etc., and often use Sino-Korean for these.
            // The original Python code only handled position 0 and 1 differently for sino=False.
            // If you need more complex pure Korean numeral rules for higher magnitudes,
            // you'll need to expand this part.
            else if (position == 2) { // Hundreds place (usually Sino-Korean '백')
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "백";
                if (name == "일백") {
                    name = "백";
                }
            } else if (position == 3) { // Thousands place (usually Sino-Korean '천')
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "천";
                if (name == "일천") {
                    name = "천";
                }
            } else if (position == 4) { // Ten thousands place (usually Sino-Korean '만')
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "만";
                if (name == "일만") {
                    name = "만";
                }
            } else if (position == 5) { // Hundred thousands place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "십";
                if (name == "일십") {
                    name = "십";
                }
            } else if (position == 6) { // Millions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "백";
                if (name == "일백") {
                    name = "백";
                }
            } else if (position == 7) { // Ten millions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "천";
                if (name == "일천") {
                    name = "천";
                }
            } else if (position == 8) { // Billions place (억)
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "억";
            } else if (position == 9) { // Ten billions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "십";
            } else if (position == 10) { // Hundred billions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "백";
            } else if (position == 11) { // Trillions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "천";
            } else if (position == 12) { // Quadrillions place (조)
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "조";
            } else if (position == 13) { // Ten quadrillions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "십";
            } else if (position == 14) { // Hundred quadrillions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "백";
            } else if (position == 15) { // Quintillions place
                name = (digit2name.count(digit) ? digit2name[digit] : "") + "천";
            }
        }
        spelledout_reversed.push_back(name);
    }

    std::reverse(spelledout_reversed.begin(), spelledout_reversed.end());

    std::string result = "";
    for (const std::string& s : spelledout_reversed) {
        result += s;
    }
    return result;
}

// Structure to hold the return values of g2pk

std::vector<int> distribute_phone(int n_phone, int n_word) {
    std::vector<int> phones_per_word(n_word, 0); // Initialize with n_word zeros

    for (int task = 0; task < n_phone; ++task) {
        // Find the minimum value in phones_per_word
        auto min_it = std::min_element(phones_per_word.begin(), phones_per_word.end());
        // Get the index of the first occurrence of the minimum value
        int min_index = std::distance(phones_per_word.begin(), min_it);
        phones_per_word[min_index]++;
    }
    return phones_per_word;
}
std::vector<std::string> convert_wchar_vector_to_string_vector(const std::vector<wchar_t>& wchars) {
    std::vector<std::string> strings;

    // std::wstring_convert와 std::codecvt_utf8를 사용하여 wchar_t를 UTF-8 string으로 변환
    // C++17에서 deprecated 되었지만, 여전히 유용하게 사용됩니다.
    // 실제 배포 코드에서는 이 부분의 안정성 또는 대체 방법을 고려할 수 있습니다.
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

    for (wchar_t wc : wchars) {
        // 단일 wchar_t를 std::wstring으로 만든 후, converter를 사용하여 UTF-8 std::string으로 변환
        std::wstring ws(1, wc); // 단일 wchar_t를 포함하는 wstring 생성
        strings.push_back(converter.to_bytes(ws));
    }

    return strings;
}
const std::set<std::string> punctuation = { ".", ",", "!", "?", "[UNK]" };
// Your existing C++ g2pk function
G2PResult g2pk(const std::string& norm_text, WordPieceTokenizer& tokenizer) {
    std::vector<std::string> tokenized = tokenizer.tokenize(norm_text);
    // std::cout << "korean.py g2pk tokenized: ";
    // for (const auto& t : tokenized) {
    //     std::cout << t << " ";
    // }
    // std::cout << std::endl;

    std::vector<std::string> phs;
    std::vector<std::vector<std::string>> ph_groups;

    for (const auto& t : tokenized) {
        if (!t.empty() && t[0] != '#') {
            ph_groups.push_back({ t });
        }
        else if (!ph_groups.empty()) {
            std::string cleaned_t = t;
            if (!cleaned_t.empty() && cleaned_t[0] == '#' && cleaned_t.size() > 2) {
                cleaned_t.erase(0, 2);
            }
            ph_groups.back().push_back(cleaned_t);
        }
    }

    std::vector<int> word2ph_local;
    std::vector<int> ph_ids;

    for (const auto& group : ph_groups) {
        std::string text = "";
        for (const auto& ch : group) {
            text += ch;
        }

        
        if (text == "[UNK]") {
            phs.push_back("_");
            word2ph_local.push_back(1);
            /// 추가 이게 맞나?
            // ph_ids.push_back(0);
            continue;
        }
        else if (punctuation.count(text)) {
            phs.push_back(text);
            word2ph_local.push_back(1);
            ph_ids.push_back(charToPhoneId(text[0]));
            continue;
        }

        std::string phonemes_ = TextToPhone(text); // This should return space-separated phonemes
        SHERPA_ONNX_LOGE("---------- after textToPhone: %s\n text: %s", phonemes_.c_str(),text.c_str());
        auto ph_ids_ = TextToPhoneId(phonemes_,false);
        SHERPA_ONNX_LOGE("ph_ids");
        std::ostringstream oss;
        oss << "ph_ids: ";
        for (auto p : ph_ids_) {
            oss << p << ",";
        }
        // cout << "--------- " << endl;
        SHERPA_ONNX_LOGE("%s",oss.str().c_str());

        ph_ids.insert(ph_ids.end(), ph_ids_.begin(), ph_ids_.end());
        //  textToPhoneId(text);

        std::vector<wchar_t> phonemes_w = decompose_hangul(utf8_to_wstring(phonemes_));
        std::vector<std::string> phonemes = convert_wchar_vector_to_string_vector(phonemes_w);
        std::ostringstream oss2;
        oss2 << "phonemes: ";
        for (auto p : phonemes) {
            oss2 << p << ",";
        }
        SHERPA_ONNX_LOGE("%s",oss2.str().c_str());
        SHERPA_ONNX_LOGE("phonemes.size(): %d", phonemes.size());
        // cout << "phonemes.size(): " << phonemes.size() << endl;

        // cout << endl;

        // Example: textToPhone("대통령") should return "d ae t o ng n yeo ng"
        // If it returns "대통녕", then `split` will only give one element.

        // std::vector<std::string> phonemes = split(phonemes_str, ' ');

        // --- Crucial Check and Correction ---
        // If `phonemes_str` was "대통녕" and `split` gives `{"대통녕"}` (size 1)
        // BUT you *expect* 8 phonemes for "대통령"
        // YOU MUST ensure `phonemes` vector contains 8 distinct phoneme strings.
        // This likely means your `textToPhone` or `split` is not yielding individual phonemes.

        // If for some reason `phonemes` still ends up empty (e.g., textToPhone returns empty string)
        if (phonemes.empty()) {
            std::cerr << "WARNING: No phonemes generated for text: \"" << text << "\". Treating as unknown." << std::endl;
            phs.push_back("_"); // Fallback phoneme
            word2ph_local.push_back(1); // Assign 1 phoneme to the token
            continue; // Skip to next group
        }
        // --- End Crucial Check ---

        int phone_len = phonemes.size();
        int word_len = group.size(); // Number of WordPiece tokens for this combined text

        // Debug prints to verify `phone_len` and `word_len`
        SHERPA_ONNX_LOGE("Processing group: ");
        std::ostringstream oss3;
        for (const auto& t : group){
          oss3 << t << " ";
        } 
        oss3 << " (combined text: \"" << text << "\")" << std::endl;
        oss3 << "  Expected phonemes count (phone_len): " << phone_len << std::endl;
        oss3 << "  WordPiece tokens in group (word_len): " << word_len << std::endl;
        SHERPA_ONNX_LOGE("%s",oss3.str().c_str());


        std::vector<int> aaa = distribute_phone(phone_len, word_len);

        if (aaa.size() != word_len) {
            // std::cerr << "Assertion failed: distribute_phone result size mismatch for group: ";
            SHERPA_ONNX_LOGE("Assertion failed: distribute_phone result size mismatch for group: ");
            std::ostringstream oss4;
            for (const auto& t : group) {
              oss4 << t << " ";
            }
            oss4 << " (expected " << word_len << ", got " << aaa.size() << ")" << std::endl;
            // std::cerr << " (expected " << word_len << ", got " << aaa.size() << ")" << std::endl;
            SHERPA_ONNX_LOGE("%s",oss4.str().c_str());
            // Handle this error robustly if it happens (e.g., throw exception)
        }
        word2ph_local.insert(word2ph_local.end(), aaa.begin(), aaa.end());

        phs.insert(phs.end(), phonemes.begin(), phonemes.end());
    }

    std::vector<std::string> phones_final = { "_" };
    phones_final.insert(phones_final.end(), phs.begin(), phs.end());
    phones_final.push_back("_");

    std::vector<int64_t> tones_final(phones_final.size(), 0);

    std::vector<int64_t> word2ph_final = { 1 };
    word2ph_final.insert(word2ph_final.end(), word2ph_local.begin(), word2ph_local.end());
    word2ph_final.push_back(1);

    std::vector<int64_t> ph_ids_final = { 0 };
    ph_ids_final.insert(ph_ids_final.end(), ph_ids.begin(), ph_ids.end());
    ph_ids_final.push_back(0);
    // ph_ids.push_back(0);

    if (word2ph_final.size() != tokenized.size() + 2) {
        // std::cerr << "Assertion failed: final word2ph size mismatch! (expected " << tokenized.size() + 2 << ", got " << word2ph_final.size() << ")" << std::endl;
        SHERPA_ONNX_LOGE("Assertion failed: final word2ph size mismatch! (expected %d, got %d)", tokenized.size() + 2, word2ph_final.size());
        // This indicates a problem in the overall logic of `ph_groups` or `word2ph_local` accumulation.
        // It implies `word2ph_local.size()` is not equal to `tokenized.size()`.
        // This can happen if `ph_groups` doesn't correctly capture all `tokenized` elements.
    }

    // G2PResult result;
    // result.phones = phones_final;
    // result.tones = tones_final;
    // result.word2ph = word2ph_final;
    // result.phone_ids = ph_ids;

    SHERPA_ONNX_LOGE("phones_final.size(): %d", phones_final.size());
    SHERPA_ONNX_LOGE("tones_final.size(): %d", tones_final.size());
    SHERPA_ONNX_LOGE("word2ph_final.size(): %d", word2ph_final.size());
    // sum of word2ph_final
    int64_t sum = 0;
    for(int i = 0; i < word2ph_final.size(); i++){
      sum += word2ph_final[i];
    }
    SHERPA_ONNX_LOGE("sum of word2ph_final: %d", sum);
    SHERPA_ONNX_LOGE("ph_ids_final.size(): %d", ph_ids_final.size());
    // return G2PResult{ phones_final, tones_final, word2ph_final };
    return G2PResult{ phones_final,ph_ids_final, tones_final, word2ph_final };
}


// Forward declaration for merge_short_sentences_ko
std::vector<std::string> merge_short_sentences_ko(std::vector<std::string> sentences, int min_len_merge = 30);


/// @param text 
/// @param min_len  byte length
/// @return  vector of sentences
std::vector<std::string> split_sentences_ko(const std::string& text, int min_len) {
    std::string processed_text = text;

    // 1. 텍스트 내의 여러 종류의 공백, 탭, 줄바꿈 등을 하나의 공백으로 정규화
    processed_text = std::regex_replace(processed_text, std::regex("[\\n\\t ]+"), " ");

    // 2. 한국어 문장 종결 구두점 (온점, 물음표, 느낌표) 뒤에 특수 구분자 "$#!" 삽입
    //    쉼표(,) 뒤에도 삽입하여 초기 분리 기준으로 사용
    //    주의: 구두점 뒤에 공백을 추가하여, 공백도 분리 기준으로 삼을 수 있게 합니다.
    processed_text = std::regex_replace(processed_text, std::regex("([.!?])"), "$1 $#!"); // 구두점 뒤에 공백과 구분자 삽입
    processed_text = std::regex_replace(processed_text, std::regex("([,])"), "$1 $#!");   // 쉼표 뒤에 공백과 구분자 삽입

    // 3. 공백도 특수 구분자 "$#!"로 간주하여 문장 분리.
    //    여기서 중요한 것은 공백 자체를 버리지 않고 구분자의 일부로 처리하거나,
    //    분리된 토큰 사이에 공백을 다시 삽입하는 로직이 필요하다는 것입니다.
    //    가장 간단한 방법은 모든 공백을 포함한 '토큰'을 만드는 것입니다.
    //    하지만, `std::string::find`로는 공백을 포함한 토큰을 만들 수 없습니다.
    //    따라서 `std::regex_token_iterator`를 사용하여 공백을 포함한 분리를 수행합니다.

    std::vector<std::string> sentences_raw;
    // 공백 ( ) 또는 특수 구분자 ($#!)를 분리 기준으로 사용
    // 이 정규식은 ' ' 또는 '$#!'를 분리자로 인식합니다.
    std::regex re(" |\\$#!"); // 공백 또는 $#!

    // std::sregex_token_iterator를 사용하여 분리 (토큰 추출 모드)
    // -1은 분리자 자체가 아닌, 분리자 사이의 토큰을 가져오라는 의미입니다.
    std::sregex_token_iterator it(processed_text.begin(), processed_text.end(), re, -1);
    std::sregex_token_iterator end;

    for (; it != end; ++it) {
        std::string token = *it;
        if (!token.empty()) { // 빈 문자열은 스킵 (연속된 분리자 등으로 인해 발생 가능)
            sentences_raw.push_back(token);
        }
    }

    // 4. 짧은 문장들을 `min_len` 기준에 따라 병합
    std::vector<std::string> new_sentences;
    std::vector<std::string> new_sent_buffer;
    int count_len = 0;

    for (size_t i = 0; i < sentences_raw.size(); ++i) {
        // 버퍼에 현재 토큰을 추가
        new_sent_buffer.push_back(sentences_raw[i]);
        count_len += sentences_raw[i].length(); // 바이트 길이

        // 현재 토큰이 마지막 토큰이거나, 버퍼의 길이가 min_len을 초과했을 때
        if (count_len >= min_len || i == sentences_raw.size() - 1) {
            std::string joined_sentence;
            for (size_t j = 0; j < new_sent_buffer.size(); ++j) {
                joined_sentence += new_sent_buffer[j];
                // 각 토큰 사이에 공백을 추가합니다.
                // 단, 마지막 토큰 뒤에는 공백을 추가하지 않습니다.
                // 그리고 구두점 바로 뒤에 공백이 붙는 것을 막으려면 추가 로직이 필요합니다.
                // (예: 현재 토큰이 구두점일 경우 다음 토큰에 공백을 붙이지 않음)
                // 여기서는 간단하게 모든 토큰 뒤에 공백을 추가하되, 마지막 토큰은 제외합니다.
                if (j < new_sent_buffer.size() - 1) {
                    joined_sentence += " ";
                }
            }
            new_sentences.push_back(trim(joined_sentence)); // 최종적으로 양 끝 공백 제거
            new_sent_buffer.clear(); // 버퍼 비우기
            count_len = 0;           // 길이 초기화
        }
    }

    // 5. 최종적으로 짧은 문장을 한 번 더 병합 (선택적)
    return merge_short_sentences_ko(new_sentences);
}

// 짧은 문장들을 병합하는 함수
// Python의 merge_short_sentences_zh와 유사한 역할
std::vector<std::string> merge_short_sentences_ko(std::vector<std::string> sentences, int min_len_merge) {
    if (sentences.empty()) {
        return {};
    }

    std::vector<std::string> merged_result;
    std::string current_merged_sentence = sentences[0];

    for (size_t i = 1; i < sentences.size(); ++i) {
        // 현재 병합 중인 문장이 너무 짧고, 다음 문장이 있다면 병합 시도
        if (current_merged_sentence.length() < min_len_merge && !sentences[i].empty()) {
            // 문장 연결 시 원래 공백이 있었을 가능성이 있으므로, 쉼표 뒤가 아니라면 공백 추가 고려
            // 간단하게는 그냥 합치거나, 기존 문장의 끝이 구두점인지 확인 후 공백 추가 등 복잡하게 할 수 있음.
            // 여기서는 단순 병합
            current_merged_sentence += sentences[i];
        } else {
            // 충분히 길거나, 다음 문장이 짧더라도 병합할 수 없는 경우 (다른 기준)
            merged_result.push_back(trim(current_merged_sentence));
            current_merged_sentence = sentences[i];
        }
    }
    // 마지막으로 남은 문장 추가
    merged_result.push_back(trim(current_merged_sentence));

    // 혹시 병합 후에도 너무 짧은 문장이 생길 경우 (예: "안녕.")
    // 최종적으로 너무 짧은 문장을 제거하거나, 앞/뒤 문장과 강제로 병합하는 로직을 추가할 수 있습니다.
    // 여기서는 일단 간략하게 구현.

    return merged_result;
}