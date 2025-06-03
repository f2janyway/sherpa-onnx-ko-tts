// korean_tokenizer.h
#ifndef KOREAN_TOKENIZER_H
#define KOREAN_TOKENIZER_H
#include <string>
#include <tuple>
#include <vector>
// std::vector<float> ReadJaBert();
// void ReadJaBert(std::vector<float> &ja_bert_vec);
std::vector<int64_t> TextToPhoneId(const std::string& _text);
std::vector<std::int64_t> CreateToneKo(size_t desired_size);
std::vector<std::int64_t> AddZeroesPhone(
      const std::vector<std::int64_t> &original_vec);


#endif