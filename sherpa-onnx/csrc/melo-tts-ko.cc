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
std::wstring utf8_to_wstring(const std::string &str) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  return conv.from_bytes(str);
}
std::string wstring_to_utf8(const std::wstring &wstr) {
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  return conv.to_bytes(wstr);
}
std::map<std::string, std::string> rule_id2text_map;

// --- gloss 함수 (로깅/디버깅용) ---
void gloss(bool verbose, const std::string &out, const std::string &inp,
           const std::string &rule) {
  if (verbose) {
    if (out != inp) {
      std::cout << "\tRule: " << rule << std::endl;
      std::cout << "\tChanged: '" << inp << "' -> '" << out << "'" << std::endl;
    } else {
      std::cout << "\tNo change for: '" << inp << "'" << std::endl;
    }
  }
}

// ############################ vowels ############################

// 파이썬의 jyeo 함수 대응
std::string jyeo(std::string inp, bool descriptive = false,
                 bool verbose = false) {
  std::string rule = rule_id2text_map["5.1"];
  std::string out = std::regex_replace(inp, std::regex("([ᄌᄍᄎ])ᅧ"), "$1ᅥ");
  gloss(verbose, out, inp, rule);
  return out;
}

// 파이썬의 ye 함수 대응
std::string ye(std::string inp, bool descriptive = false,
               bool verbose = false) {
  std::string rule = rule_id2text_map["5.2"];
  std::string out = inp;
  if (descriptive) {
    // '\S' 대신 `.`을 사용하거나, 더 정확하게는 한글 자모 유니코드 범위 사용
    out = std::regex_replace(
        inp, std::regex("([ᄀᄁᄃᄄᄅᄆᄇᄈᄌᄍᄎᄏᄐᄑᄒ])ᅨ"), "$1ᅦ");
  }
  gloss(verbose, out, inp, rule);
  return out;
}

// 파이썬의 consonant_ui 함수 대응
std::string consonant_ui(std::string inp, bool descriptive = false,
                         bool verbose = false) {
  std::string rule = rule_id2text_map["5.3"];
  std::string out = std::regex_replace(
      inp, std::regex("([ᄀᄁᄂᄃᄄᄅᄆᄇᄈᄉᄊᄌᄍᄎᄏᄐᄑᄒ])ᅴ"), "$1ᅵ");
  gloss(verbose, out, inp, rule);
  return out;
}

// 파이썬의 josa_ui 함수 대응
std::string josa_ui(std::string inp, bool descriptive = false,
                    bool verbose = false) {
  std::string rule = rule_id2text_map["5.4.2"];
  std::string out = inp;
  if (descriptive) {
    out = std::regex_replace(inp, std::regex("의/J"), "에");
  } else {
    size_t pos = out.find("/J");
    if (pos != std::string::npos) {
      out.replace(pos, 2, "");
    }
  }
  gloss(verbose, out, inp, rule);
  return out;
}

// 파이썬의 vowel_ui 함수 대응
std::string vowel_ui(std::string inp, bool descriptive = false,
                     bool verbose = false) {
  std::string rule = rule_id2text_map["5.4.1"];
  std::string out = inp;
  if (descriptive) {
    // 경고 메시지 '\S'를 해결하기 위해 '.' 사용
    // 한글 음절 단위의 '의'를 찾아야 하므로, 실제로 'ᄋ' 앞에 어떤 자모가 와도
    // 되는 패턴. NFD 상태이므로 'ᄋ'은 초성으로 단독으로 존재할 수 있습니다. 이
    // 패턴은 `\S`가 아닌 `.`으로 충분합니다.
    out = std::regex_replace(inp, std::regex("(.ᄋ)ᅴ"), "$1ᅵ");
  }
  gloss(verbose, out, inp, rule);
  return out;
}

// 파이썬의 jamo 함수 대응
std::string jamo(std::string inp, bool descriptive = false,
                 bool verbose = false) {
  std::string rule = rule_id2text_map["16"];
  std::string out = inp;

  out = std::regex_replace(out, std::regex("([그])ᆮᄋ"), "$1ᄉ");
  out = std::regex_replace(out, std::regex("([으])[ᆽᆾᇀᇂ]ᄋ"), "$1ᄉ");
  out = std::regex_replace(out, std::regex("([으])[ᆿ]ᄋ"), "$1ᄀ");
  out = std::regex_replace(out, std::regex("([으])[ᇁ]ᄋ"), "$1ᄇ");

  gloss(verbose, out, inp, rule);
  return out;
}

// ############################ 어간 받침 ############################

// 파이썬의 rieulgiyeok 함수 대응
std::string rieulgiyeok(std::string inp, bool descriptive = false,
                        bool verbose = false) {
  std::string rule = rule_id2text_map["11.1"];
  std::string out = std::regex_replace(inp, std::regex("ᆰ/P([ᄀᄁ])"), "ᆯᄁ");
  gloss(verbose, out, inp, rule);
  return out;
}

// 파이썬의 rieulbieub 함수 대응
std::string rieulbieub(std::string inp, bool descriptive = false,
                       bool verbose = false) {
  std::string rule = rule_id2text_map["25"];
  std::string out = inp;

  out = std::regex_replace(out, std::regex("([ᆲᆴ])/Pᄀ"), "$1ᄁ");
  out = std::regex_replace(out, std::regex("([ᆲᆴ])/Pᄃ"), "$1ᄄ");
  out = std::regex_replace(out, std::regex("([ᆲᆴ])/Pᄉ"), "$1ᄊ");
  out = std::regex_replace(out, std::regex("([ᆲᆴ])/Pᄌ"), "$1ᄍ");

  gloss(verbose, out, inp, rule);
  return out;
}

// 파이썬의 verb_nieun 함수 대응
std::string verb_nieun(std::string inp, bool descriptive = false,
                       bool verbose = false) {
  std::string rule = rule_id2text_map["24"];
  std::string out = inp;

  out = std::regex_replace(out, std::regex("([ᆫᆷ])/Pᄀ"), "$1ᄁ");
  out = std::regex_replace(out, std::regex("([ᆫᆷ])/Pᄃ"), "$1ᄄ");
  out = std::regex_replace(out, std::regex("([ᆫᆷ])/Pᄉ"), "$1ᄊ");
  out = std::regex_replace(out, std::regex("([ᆫᆷ])/Pᄌ"), "$1ᄍ");

  out = std::regex_replace(out, std::regex("ᆬ/Pᄀ"), "ᆫᄁ");
  out = std::regex_replace(out, std::regex("ᆬ/Pᄃ"), "ᆫᄄ");
  out = std::regex_replace(out, std::regex("ᆬ/Pᄉ"), "ᆫᄊ");
  out = std::regex_replace(out, std::regex("ᆬ/Pᄌ"), "ᆫᄍ");

  out = std::regex_replace(out, std::regex("ᆱ/Pᄀ"), "ᆷᄁ");
  out = std::regex_replace(out, std::regex("ᆱ/Pᄃ"), "ᆷᄄ");
  out = std::regex_replace(out, std::regex("ᆱ/Pᄉ"), "ᆷᄊ");
  out = std::regex_replace(out, std::regex("ᆱ/Pᄌ"), "ᆷᄍ");

  // grok Removed /P, apply directly
  // out = std::regex_replace(out, std::regex("([ᆫᆷ])([ᄀ])"), "$1ᄁ");
  // out = std::regex_replace(out, std::regex("([ᆫᆷ])([ᄃ])"), "$1ᄄ");
  // out = std::regex_replace(out, std::regex("([ᆫᆷ])([ᄉ])"), "$1ᄊ");
  // out = std::regex_replace(out, std::regex("([ᆫᆷ])([ᄌ])"), "$1ᄍ");

  // out = std::regex_replace(out, std::regex("ᆬ([ᄀ])"), "ᆫᄁ");
  // out = std::regex_replace(out, std::regex("ᆬ([ᄃ])"), "ᆫᄄ");
  // out = std::regex_replace(out, std::regex("ᆬ([ᄉ])"), "ᆫᄊ");
  // out = std::regex_replace(out, std::regex("ᆬ([ᄌ])"), "ᆫᄍ");

  // out = std::regex_replace(out, std::regex("ᆱ([ᄀ])"), "ᆷᄁ");
  // out = std::regex_replace(out, std::regex("ᆱ([ᄃ])"), "ᆷᄄ");
  // out = std::regex_replace(out, std::regex("ᆱ([ᄉ])"), "ᆷᄊ");
  // out = std::regex_replace(out, std::regex("ᆱ([ᄌ])"), "ᆷᄍ");
  gloss(verbose, out, inp, rule);
  return out;
}

// // 파이썬의 balb 함수 대응
// std::string balb(std::string inp, bool descriptive = false, bool verbose =
// false) {
//     std::string rule = rule_id2text_map["10.1"];
//     std::string out = inp;

//     // gemeni `$`는 문자열의 끝을 의미, `[^ᄋᄒ]`는 'ᄋ' 또는 'ᄒ'이 아닌
//     모든 문자
//     // out = std::regex_replace(out, std::regex("(바)ᆲ(\\$|[^ᄋᄒ])"),
//     "$1ᆸ$2");
//     // out = std::regex_replace(out, std::regex("(너)ᆲ([ᄌᄍ]ᅮ|[ᄃᄄ]ᅮ)"),
//     "$1ᆸ$2");

//     //grok Simplified regex to apply to ᆲ followed by any consonant or end
//     out = std::regex_replace(out, std::regex("(바)ᆲ([ᄀ-ᄒ]?|$)"), "$1ᆸ$2");
//     out = std::regex_replace(out, std::regex("(너)ᆲ([ᄌᄍ]ᅮ|[ᄃᄄ]ᅮ)"),
//     "$1ᆸ$2");

//     gloss(verbose, out, inp, rule);
//     return out;
// }

std::string balb(std::string inp, bool descriptive = false,
                 bool verbose = false) {
  std::string rule = rule_id2text_map["10.1"];
  std::string out = inp;

  // C++ 정규식에서 우선순위 명확히 하기 위해 괄호로 묶음
  // (바)ᆲ 뒤에 문자열 끝이거나 ᄋ, ᄒ가 아닌 문자 있을 때만 변경
  out = std::regex_replace(out, std::regex("(바)ᆲ($|[^ᄋᄒ])"), "$1ᆸ$2");

  // 두번째 규칙은 파이썬과 동일하게 유지
  out = std::regex_replace(out, std::regex("(너)ᆲ([ᄌᄍ]ᅮ|[ᄃᄄ]ᅮ)"), "$1ᆸ$2");

  gloss(verbose, out, inp, rule);
  return out;
}
std::string palatalize(std::string inp, bool descriptive = false,
                       bool verbose = false) {
  std::wstring winp = utf8_to_wstring(inp);
  std::wstring wout = winp;

  std::wregex r1(L"ᆮᄋ([ᅵᅧ])");
  std::wregex r2(L"ᇀᄋ([ᅵᅧ])");
  std::wregex r3(L"ᆴᄋ([ᅵᅧ])");
  std::wregex r4(L"ᆮᄒ([ᅵ])");

  wout = std::regex_replace(wout, r1, L"ᄌ$1");
  wout = std::regex_replace(wout, r2, L"ᄎ$1");
  wout = std::regex_replace(wout, r3, L"ᆯᄎ$1");
  wout = std::regex_replace(wout, r4, L"ᄎ$1");

  std::string out = wstring_to_utf8(wout);

  gloss(verbose, out, inp, rule_id2text_map["17"]);
  return out;
}
// 파이썬의 palatalize 함수 대응
// std::string palatalize(std::string inp, bool descriptive = false, bool
// verbose = false) {
//     std::string rule = rule_id2text_map["17"];
//     std::string out = inp;

//     out = std::regex_replace(out, std::regex("ᆮᄋ([ᅵᅧ])"), "ᄌ$1");
//     out = std::regex_replace(out, std::regex("ᇀᄋ([ᅵᅧ])"), "ᄎ$1");
//     out = std::regex_replace(out, std::regex("ᆴᄋ([ᅵᅧ])"), "ᆯᄎ$1");

//     out = std::regex_replace(out, std::regex("ᆮᄒ([ᅵ])"), "ᄎ$1");

//     gloss(verbose, out, inp, rule);
//     return out;
// }

// 파이썬의 modifying_rieul 함수 대응
std::string modifying_rieul(std::string inp, bool descriptive = false,
                            bool verbose = false) {
  std::string rule = rule_id2text_map["27"];
  std::string out = inp;

  out = std::regex_replace(out, std::regex("ᆯ/E ᄀ"), "ᆯ ᄁ");
  out = std::regex_replace(out, std::regex("ᆯ/E ᄃ"), "ᆯ ᄄ");
  out = std::regex_replace(out, std::regex("ᆯ/E ᄇ"), "ᆯ ᄈ");
  out = std::regex_replace(out, std::regex("ᆯ/E ᄉ"), "ᆯ ᄊ");
  out = std::regex_replace(out, std::regex("ᆯ/E ᄌ"), "ᆯ ᄍ");

  out = std::regex_replace(out, std::regex("ᆯ걸"), "ᆯ껄");
  out = std::regex_replace(out, std::regex("ᆯ밖에"), "ᆯ빠께");
  out = std::regex_replace(out, std::regex("ᆯ세라"), "ᆯ쎄라");
  out = std::regex_replace(out, std::regex("ᆯ수록"), "ᆯ쑤록");
  out = std::regex_replace(out, std::regex("ᆯ지라도"), "ᆯ찌라도");
  out = std::regex_replace(out, std::regex("ᆯ지언정"), "ᆯ찌언정");
  out = std::regex_replace(out, std::regex("ᆯ진대"), "ᆯ찐대");

  gloss(verbose, out, inp, rule);
  return out;
}
// 유틸 함수: 여러 문자열 치환
string replace_pairs(const string &inp,
                     const vector<pair<string, string>> &pairs) {
  string out = inp;
  for (const auto &[from, to] : pairs) {
    size_t pos = 0;
    while ((pos = out.find(from, pos)) != string::npos) {
      out.replace(pos, from.length(), to);
      pos += to.length();
    }
  }
  return out;
}

// link1: 홑받침 또는 쌍받침 + ᄋ → 초성으로 변경
string link1(const string &inp, bool descriptive = false,
             bool verbose = false) {
  string rule = rule_id2text_map["13"];
  vector<pair<string, string>> pairs = {
      {"ᆨᄋ", "ᄀ"}, {"ᆩᄋ", "ᄁ"}, {"ᆫᄋ", "ᄂ"}, {"ᆮᄋ", "ᄃ"}, {"ᆯᄋ", "ᄅ"},
      {"ᆷᄋ", "ᄆ"}, {"ᆸᄋ", "ᄇ"}, {"ᆺᄋ", "ᄉ"}, {"ᆻᄋ", "ᄊ"}, {"ᆽᄋ", "ᄌ"},
      {"ᆾᄋ", "ᄎ"}, {"ᆿᄋ", "ᄏ"}, {"ᇀᄋ", "ᄐ"}, {"ᇁᄋ", "ᄑ"},
  };
  string out = replace_pairs(inp, pairs);
  gloss(verbose, out, inp, rule);
  return out;
}

// link2: 겹받침 + ᄋ → 초성 2개로 분해
string link2(const string &inp, bool descriptive = false,
             bool verbose = false) {
  string rule = rule_id2text_map["14"];
  vector<pair<string, string>> pairs = {
      {"ᆪᄋ", "ᆨᄊ"}, {"ᆬᄋ", "ᆫᄌ"}, {"ᆰᄋ", "ᆯᄀ"},
      {"ᆱᄋ", "ᆯᄆ"}, {"ᆲᄋ", "ᆯᄇ"}, {"ᆳᄋ", "ᆯᄊ"},
      {"ᆴᄋ", "ᆯᄐ"}, {"ᆵᄋ", "ᆯᄑ"}, {"ᆹᄋ", "ᆸᄊ"},
  };
  string out = replace_pairs(inp, pairs);
  gloss(verbose, out, inp, rule);
  return out;
}

// link3: 받침 + 공백 + ᄋ 처리
string link3(const string &inp, bool descriptive = false,
             bool verbose = false) {
  string rule = rule_id2text_map["15"];
  vector<pair<string, string>> pairs = {
      {"ᆨ ᄋ", " ᄀ"},  {"ᆩ ᄋ", " ᄁ"},  {"ᆫ ᄋ", " ᄂ"},  {"ᆮ ᄋ", " ᄃ"},
      {"ᆯ ᄋ", " ᄅ"},  {"ᆷ ᄋ", " ᄆ"},  {"ᆸ ᄋ", " ᄇ"},  {"ᆺ ᄋ", " ᄉ"},
      {"ᆻ ᄋ", " ᄊ"},  {"ᆽ ᄋ", " ᄌ"},  {"ᆾ ᄋ", " ᄎ"},  {"ᆿ ᄋ", " ᄏ"},
      {"ᇀ ᄋ", " ᄐ"},  {"ᇁ ᄋ", " ᄑ"},  {"ᆪ ᄋ", "ᆨ ᄊ"}, {"ᆬ ᄋ", "ᆫ ᄌ"},
      {"ᆰ ᄋ", "ᆯ ᄀ"}, {"ᆱ ᄋ", "ᆯ ᄆ"}, {"ᆲ ᄋ", "ᆯ ᄇ"}, {"ᆳ ᄋ", "ᆯ ᄊ"},
      {"ᆴ ᄋ", "ᆯ ᄐ"}, {"ᆵ ᄋ", "ᆯ ᄑ"}, {"ᆹ ᄋ", "ᆸ ᄊ"},
  };
  string out = replace_pairs(inp, pairs);
  gloss(verbose, out, inp, rule);
  return out;
}

// link4: ㅎ+ᄋ → 생략 규칙
string link4(const string &inp, bool descriptive = false,
             bool verbose = false) {
  string rule = rule_id2text_map["12.4"];
  vector<pair<string, string>> pairs = {
      {"ᇂᄋ", "ᄋ"}, {"ᆭᄋ", "ᄂ"}, {"ᆶᄋ", "ᄅ"}};
  string out = replace_pairs(inp, pairs);
  gloss(verbose, out, inp, rule);
  return out;
}

using RuleEntry = tuple<string, string, vector<string>>;
vector<RuleEntry> table;
// tuple: (str1, str2, rule_ids)

std::string fix_replacement_backrefs(const std::string &replacement) {
  std::string fixed;
  fixed.reserve(replacement.size());

  for (size_t i = 0; i < replacement.size(); ++i) {
    if (replacement[i] == '\\' && i + 1 < replacement.size()) {
      char next = replacement[i + 1];
      if (next >= '0' && next <= '9') {
        // \숫자 -> $숫자 로 변경
        fixed += '$';
        fixed += next;
        ++i;  // 다음 문자 건너뜀
        continue;
      }
    }
    fixed += replacement[i];
  }
  return fixed;
}
std::vector<RuleEntry> parse_table_csv_hardcoded() {
    std::vector<RuleEntry> table;

    // CSV 데이터를 하드코딩된 문자열로 정의
    const std::string hardcoded_csv_data = R"(,( ?)ᄒ,( ?)ᄀ,( ?)ᄁ,( ?)ᄂ,( ?)ᄃ,( ?)ᄄ,( ?)ᄅ,( ?)ᄆ,( ?)ᄇ,( ?)ᄈ,( ?)ᄉ,( ?)ᄊ,( ?)ᄌ,( ?)ᄍ,( ?)ᄎ,( ?)ᄏ,( ?)ᄐ,( ?)ᄑ,(\W|$)
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

        for (size_t i = 1; i < cols.size(); ++i) {
            std::string cell_content = cols[i]; // 'cell'이 이미 위에 있으므로 다른 이름 사용
            if (cell_content.empty()) continue;

            std::string onset = onsets[i];
            std::string str1 = coda + onset;
            std::string str2;
            std::vector<std::string> rule_ids;

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
                    rule_ids.push_back(rule);
                }
            } else {
                str2 = cell_content;
            }

            table.emplace_back(str1, str2, rule_ids);
        }
    }

    return table;
}
vector<RuleEntry> parse_table_csv(const string &filename) {
  vector<RuleEntry> table;

  ifstream file(filename);
  if (!file.is_open()) {
    cerr << "파일을 열 수 없습니다: " << filename << endl;
    return table;
  }

  string line;
  vector<string> onsets;

  // 첫 줄 (onset 목록)
  if (getline(file, line)) {
    stringstream ss(line);
    string cell;
    while (getline(ss, cell, ',')) {
      onsets.push_back(cell);
    }
  }

  // 나머지 줄들 (coda + 각 onset 조합)
  while (getline(file, line)) {
    stringstream ss(line);
    vector<string> cols;
    string cell;

    while (getline(ss, cell, ',')) {
      cols.push_back(cell);
    }

    if (cols.empty()) continue;
    string coda = cols[0];

    for (size_t i = 1; i < cols.size(); ++i) {
      string cell = cols[i];
      if (cell.empty()) continue;

      string onset = onsets[i];
      string str1 = coda + onset;
      string str2;
      vector<string> rule_ids;

      // 괄호가 있으면 규칙 포함
      size_t pos = cell.find('(');
      if (pos != string::npos) {
        str2 = cell.substr(0, pos);
        string rule_str = cell.substr(pos + 1);
        if (!rule_str.empty() && rule_str.back() == ')') {
          rule_str.pop_back();
        }

        stringstream rss(rule_str);
        string rule;
        while (getline(rss, rule, '/')) {
          rule_ids.push_back(rule);
        }
      } else {
        str2 = cell;
      }

      table.emplace_back(str1, str2, rule_ids);
    }
  }

  return table;
}
std::string remove_pjeb_tags(const std::string &inp) {
  return std::regex_replace(inp, std::regex("/[PJEB]"), "");
}
std::string apply_rule_step(
    const std::string &step_name,
    const std::function<std::string(const std::string &, bool, bool)>
        &rule_func,
    const std::string &input, bool descriptive) {
  std::string output =
      rule_func(input, descriptive, false);  // verbose=false 고정
  if (output != input) {
    std::cout << "[" << step_name << "] 변환 전: " << input << "\n";
    std::cout << "[" << step_name << "] 변환 후: " << output << "\n\n";
  }
  return output;
}
// // 텍스트 하나받고 &text 위의 함수 지나게 하기
std::string apply_rules(std::string inp, bool descriptive = false) {
  std::string out = inp;

  out = apply_rule_step("jyeo", jyeo, out, descriptive);
  out = apply_rule_step("ye", ye, out, descriptive);
  out = apply_rule_step("consonant_ui", consonant_ui, out, descriptive);
  out = apply_rule_step("josa_ui", josa_ui, out, descriptive);
  out = apply_rule_step("vowel_ui", vowel_ui, out, descriptive);
  out = apply_rule_step("jamo", jamo, out, descriptive);
  out = apply_rule_step("rieulgiyeok", rieulgiyeok, out, descriptive);
  out = apply_rule_step("rieulbieub", rieulbieub, out, descriptive);
  out = apply_rule_step("verb_nieun", verb_nieun, out, descriptive);
  out = apply_rule_step("balb", balb, out, descriptive);
  out = apply_rule_step("palatalize", palatalize, out, descriptive);
  out = apply_rule_step("modifying_rieul", modifying_rieul, out, descriptive);

  // 태그 제거 - 직접 비교 출력
  {
    std::string before = out;
    out = remove_pjeb_tags(out);
    if (out != before) {
      std::cout << "[remove_pjeb_tags] 변환 전: " << before << "\n";
      std::cout << "[remove_pjeb_tags] 변환 후: " << out << "\n\n";
    }
  }

  // 규칙 기반 치환
  for (const auto &[str1, str2, rule_ids] : table) {
    std::string before = out;
    std::string fixed_str2 = fix_replacement_backrefs(str2);
    // try {
    //     out = std::regex_replace(out, std::regex(str1), str2);
    // }
    try {
      out = std::regex_replace(out, std::regex(str1), fixed_str2);
    } catch (std::regex_error &e) {
      std::cerr << "[regex error] pattern: " << str1 << ", error: " << e.what()
                << std::endl;
      continue;
    }

    if (out != before) {  // 변경이 있을 때만 출력
      std::cout << "[regex_replace] 패턴: " << str1 << "\n";
      std::cout << "[regex_replace] 변환 전: " << before << "\n";
      std::cout << "[regex_replace] 변환 후: " << out << "\n";

      std::string rule;
      if (!rule_ids.empty()) {
        for (const std::string &rule_id : rule_ids) {
          auto it = rule_id2text_map.find(rule_id);
          if (it != rule_id2text_map.end()) {
            rule += it->second + "\n";
          }
        }
      }

      gloss(true, out, before, rule);  // gloss는 내부에서 필요한 출력 처리
    }
  }
  out = apply_rule_step("link1", link1, out, descriptive);
  out = apply_rule_step("link2", link2, out, descriptive);
  out = apply_rule_step("link3", link3, out, descriptive);
  out = apply_rule_step("link4", link4, out, descriptive);

  return out;
}

// 유니코드 시퀀스 출력
void print_unicode(const wstring &text) {
  wcout << L"[";
  for (size_t i = 0; i < text.size(); ++i) {
    wcout << L"U+" << hex << uppercase << setw(4) << setfill(L'0')
          << (int)text[i];
    if (i != text.size() - 1) wcout << L", ";
  }
  wcout << L"]" << endl;
}
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
vector<int64_t> convert_to_jamo_ids(const wstring &input) {
  vector<wchar_t> decomposed = decompose_hangul(input);
  vector<int64_t> result;

  // 문장 시작을 위한 padding
  result.push_back(0);  // '_' == 0

  for (wchar_t jamo : decomposed) {
    auto it = jamo_to_id.find(jamo);
    if (it != jamo_to_id.end()) {
      result.push_back(it->second);
    } else {
      // UNK 처리 or 스킵
      result.push_back(218);  // 예: UNK == 218
    }
  }

  // 문장 끝 padding
  result.push_back(0);  // '_' == 0

  return result;
}

#include "sherpa-onnx/csrc/macros.h"
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
std::vector<int64_t> TextToPhoneId(const std::string &_text) {
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
  vector<string> out_applied_rules;
  for (auto &token : tokens) {
    string out = apply_rules(wstring_to_utf8(token));
    out_applied_rules.push_back(out);
  }
  // 규칙 적용 (jyeo_wstring 포함)
  std::wstring rs;
  // std::int8_t idx = 0;
  for (auto &out : out_applied_rules) {
    rs += compose_hangul(utf8_to_wstring(out));
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
  std::vector<int64_t> phoneme_ids = convert_to_jamo_ids(rs);
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
int main() {
  std::locale::global(std::locale("ko_KR.UTF-8"));  // 한글 출력 가능하도록 설정
  //    std::locale::global(std::locale("")); // 한글 출력 가능하도록 설정
  std::wcout.imbue(std::locale());
  table = parse_table_csv("table.csv");
  std::vector<wstring> test_cases = {
      L"밟아",
      L"않았다",
      L"굳이",
  };

  // for (const auto& input : test_cases) {
  //     wcout << L"입력: " << input << endl;
  //     print_unicode(input);
  //     vector<wchar_t> jamos = decompose_hangul(input);
  //     wstring result(jamos.begin(), jamos.end());
  //     wcout << L"분해 결과: " << result << endl;
  //     print_unicode(result);
  //     wcout << L"적용 규칙: " << endl;
  //     wcout << L"결과: " <<
  //     compose_hangul(utf8_to_wstring(apply_rules(wstring_to_utf8(result))))
  //     << endl; wcout << endl;
  // }
  // for (const auto& input : test_cases) {
  //     wcout << L"입력: " << input << endl;
  //     print_unicode(input);
  //     vector<wchar_t> jamos = decompose_hangul(input);
  //     wstring result(jamos.begin(), jamos.end());
  //     wcout << L"분해 결과: " << result << endl;
  //     print_unicode(result);
  //     wcout << L"적용 규칙: " << endl;
  //     wcout << L"결과: " <<
  //     compose_hangul(utf8_to_wstring(apply_rules(wstring_to_utf8(result))))
  //     << endl; wcout << endl;
  // 며

  // wstring text = L"하늘에 계신 우리아버지여 이름이 거룩히 여김을 받으시오며
  // 이름이 거룩히 여김을 받으시며 나라이 임하옵시며 뜻이 하늘에서 이룬것 같이
  // 땅에서도 이루어지이다. 오늘날 우리에게 일용할 양식을 주옵시고 우리가
  // 우리에게 죄지은 자를 사하여 준것 같이 우리 죄를 사하여 주옵시고 우리를
  // 시험에 들지 말게 하옵시고 다만 악에서 구하옵소서 대게 나라와 권세와 영광이
  // 아버지께 영원히 있사옵나이다. 아멘 ";
  // // wstring text = L"받으시오며";

  // wcout << L"입력: " << text << endl;
  // print_unicode(text);
  // vector<wchar_t> jamos = decompose_hangul(text);
  // wstring result(jamos.begin(), jamos.end());
  // // wcout << L"분해 결과: " << result << endl;
  // print_unicode(result);
  // wcout << L"적용 규칙: " << endl;
  // wcout << L"결과: " <<
  // compose_hangul(utf8_to_wstring(apply_rules(wstring_to_utf8(result)))) <<
  // endl; wcout << endl;
  wstring text = L"받으시오며";
  vector<wchar_t> jamos = decompose_hangul(text);
  wstring result(jamos.begin(), jamos.end());
  wstring rs =
      compose_hangul(utf8_to_wstring(apply_rules(wstring_to_utf8(result))));
  for (wchar_t j : rs) {
    wcout << j << " ";  // 받으...
  }
  wcout << endl;
  // wstring result(jamos.begin(), jamos.end());

  vector<int64_t> v = convert_to_jamo_ids(rs);
  for (auto i : v) {
    wcout << i << " ";
  }
  wcout << endl;

  return 0;
}
