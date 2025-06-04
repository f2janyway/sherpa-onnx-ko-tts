#ifndef WORDPIECE_TOKENIZER_H
#define WORDPIECE_TOKENIZER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>  // std::pair
#include <strstream>

class WordPieceTokenizer {
public:
    WordPieceTokenizer(std::istream& vocab_file, bool do_lower_case = true);

    std::vector<std::string> tokenize(const std::string& text);
    std::pair<std::vector<std::string>, std::vector<int>> tokenize_with_ids(const std::string& text);

private:
    std::unordered_map<std::string, int> vocab;
    bool do_lower_case;

    void load_vocab(std::istream& vocab_file);
    std::vector<std::string> basic_tokenize(const std::string& text);
    std::vector<std::string> wordpiece_tokenize(const std::string& token);
};

#endif // WORDPIECE_TOKENIZER_H
