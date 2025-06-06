// korean_tokenizer.h
#ifndef KOREAN_TOKENIZER_H
#define KOREAN_TOKENIZER_H
#include <string>
#include <tuple>
#include <vector>
#include "melo-tts-ko-tokenizer.h"
// std::vector<float> ReadJaBert();
// void ReadJaBert(std::vector<float> &ja_bert_vec);
struct G2PResult {
    std::vector<std::string> phones;
    std::vector<int64_t> phone_ids;
    std::vector<int64_t> tones;
    std::vector<int64_t> word2ph;
};
std::vector<int64_t> TextToPhoneId(const std::string& _text,bool isFullSentence = false);
G2PResult g2pk(const std::string& norm_text, WordPieceTokenizer& tokenizer);
std::vector<std::int64_t> CreateToneKo(size_t desired_size);
std::vector<std::int64_t> AddZeroesPhone(
      const std::vector<std::int64_t> &original_vec);


#endif