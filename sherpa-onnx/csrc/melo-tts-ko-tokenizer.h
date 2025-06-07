#ifndef WORDPIECE_TOKENIZER_H
#define WORDPIECE_TOKENIZER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>  // std::pair
#include <strstream>
#include <regex>
#include <sstream>

class WordPieceTokenizer {
public:
    WordPieceTokenizer(std::istream& vocab_file, bool do_lower_case = true);

    std::vector<std::string> tokenize(const std::string& text);
    std::pair<std::vector<std::string>, std::vector<int>> tokenize_with_ids(const std::string& text);
    std::vector<std::string> basic_tokenize(const std::string& text) {
        std::string processed_text = text;
        // if (do_lower_case_) {
        //     processed_text = to_lower(text);
        // }

        // 구두점을 별도의 토큰으로 분리하기 위한 정규 표현식.
        // C++ regex는 UTF-8을 직접 처리하지 못하므로,
        // 이 패턴은 아스키 구두점에만 작동하거나,
        // 입력 텍스트의 구두점이 이미 아스키로 정규화되어 있다고 가정합니다.
        // 중국어 구두점(。！？，；)이 입력에 있다면,
        // 이들을 아스키 구두점으로 미리 변환하는 전처리 과정이 필요합니다.
        // 예를 들어, Python 코드의 `re.sub('[。！？；]', '.', text)`와 같은 작업.
        // 현재 이 함수는 아스키 구두점 (.,!?;)에 대해 작동합니다.
        // 한글 텍스트에 포함된 아스키 구두점도 분리할 수 있습니다.
        std::regex punctuation_regex("([.,!?;])");
        // 구두점 앞에 공백, 구두점 뒤에 공백을 추가하여 독립적인 토큰으로 분리될 수 있도록 합니다.
        processed_text = std::regex_replace(processed_text, punctuation_regex, " $1 ");

        std::vector<std::string> tokens;
        std::istringstream iss(processed_text);
        std::string token;
        while (iss >> token) {
            // 토큰을 trim할 필요는 없지만, 혹시 모를 경우를 대비
            // token = trim(token); // trim 함수가 있다면 사용
            if (!token.empty()) {
                tokens.push_back(token);
            }
        }
        return tokens;
    }

private:
    std::unordered_map<std::string, int> vocab;
    bool do_lower_case;

    void load_vocab(std::istream& vocab_file);
    // std::vector<std::string> basic_tokenize(const std::string& text);
    std::vector<std::string> wordpiece_tokenize(const std::string& token);
};

#endif // WORDPIECE_TOKENIZER_H
