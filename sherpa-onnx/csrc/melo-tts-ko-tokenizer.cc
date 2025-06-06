#include "melo-tts-ko-tokenizer.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
// #include <strstream>
#include "sherpa-onnx/csrc/macros.h"

// to_lower 함수도 cpp파일에 넣어줘요
static std::string to_lower(const std::string &str) {
  std::string ret = str;
  for (auto &ch : ret) ch = std::tolower(ch);
  return ret;
}

WordPieceTokenizer::WordPieceTokenizer(std::istream &vocab_file,
                                       bool do_lower_case)
    : do_lower_case(do_lower_case) {
  SHERPA_ONNX_LOGE(
      ">>>> WordPieceTokenizer::WordPieceTokenizer init load_vocab start");
  load_vocab(vocab_file);
  SHERPA_ONNX_LOGE(
      ">>>> WordPieceTokenizer::WordPieceTokenizer init load_vocab end");
}

void WordPieceTokenizer::load_vocab(std::istream &vocab_file) {
    SHERPA_ONNX_LOGE(">>>> WordPieceTokenizer::load_vocab start");
//   std::ifstream file(vocab_file);
  SHERPA_ONNX_LOGE(">>>> WordPieceTokenizer::load_vocab file read");
//   if (!file.is_open()) {
//     SHERPA_ONNX_LOGE("Failed to open vocab file");
//     throw std::runtime_error("Failed to open vocab file");
//   }
  std::string line;
  int index = 0;
  while (std::getline(vocab_file, line)) {
    vocab[line] = index++;
  }
  SHERPA_ONNX_LOGE(">>>> WordPieceTokenizer::load_vocab end vocab size: %d,%d", vocab.size(), index);
  // for(int i = 0; i < 10; i++) {
  char * vocab_key0 = "릱";
  SHERPA_ONNX_LOGE(">>>> WordPieceTokenizer::load_vocab vocab at %s : %d", vocab_key0, vocab[vocab_key0]);
  char * vocab_key1 = "릿";
  SHERPA_ONNX_LOGE(">>>> WordPieceTokenizer::load_vocab vocab at %s : %d", vocab_key1, vocab[vocab_key1]);
  char * vocab_key2 = "##조개";
  SHERPA_ONNX_LOGE(">>>> WordPieceTokenizer::load_vocab vocab at %s : %d", vocab_key2, vocab[vocab_key2]);

  // }
}

std::vector<std::string> WordPieceTokenizer::basic_tokenize(
    const std::string &text) {
        SHERPA_ONNX_LOGE( ">>>> WordPieceTokenizer::basic_tokenize start text: %s", text.c_str());
  std::string processed = text;
  if (do_lower_case) {
    processed = to_lower(text);
  }

  std::vector<std::string> tokens;
  std::istringstream iss(processed);
  std::string token;
  while (iss >> token) {
    tokens.push_back(token);
  }
  SHERPA_ONNX_LOGE( ">>>> WordPieceTokenizer::basic_tokenize end");
  return tokens;
}

std::vector<std::string> WordPieceTokenizer::wordpiece_tokenize(
    const std::string &token) {
        // SHERPA_ONNX_LOGE( ">>>> WordPieceTokenizer::wordpiece_tokenize start token: ");
  std::vector<std::string> output;
  int start = 0;
  const int len = (int)token.size();

  while (start < len) {
    int end = len;
    std::string curr_substr;
    bool found = false;

    while (start < end) {
      std::string sub = token.substr(start, end - start);
      if (start > 0) {
        sub = "##" + sub;
      }
      if (vocab.find(sub) != vocab.end()) {
        curr_substr = sub;
        found = true;
        break;
      }
      --end;
    }

    if (!found) {
      output.push_back("[UNK]");
      break;
    }

    output.push_back(curr_substr);
  
    start = end;
  }
  // for(int i = 0; i < 3; i++){
  //     SHERPA_ONNX_LOGE( ">>>> WordPieceTokenizer::wordpiece_tokenize output at %d : %s :",i, output[i].c_str());
  //   }
  // SHERPA_ONNX_LOGE( ">>>> WordPieceTokenizer::wordpiece_tokenize end");
  return output;
}

std::vector<std::string> WordPieceTokenizer::tokenize(const std::string &text) {
  SHERPA_ONNX_LOGE(">>>> WordPieceTokenizer::tokenize start");
  std::vector<std::string> basic_tokens = basic_tokenize(text);
  std::vector<std::string> wordpieces;

  for (const auto &token : basic_tokens) {
    std::vector<std::string> pieces = wordpiece_tokenize(token);
    wordpieces.insert(wordpieces.end(), pieces.begin(), pieces.end());
  }

  SHERPA_ONNX_LOGE( ">>>> WordPieceTokenizer::tokenize end");
  return wordpieces;
}

/// @brief tokenize with ids
/// @param text
/// @return  tokenized wordpieces and ids(wordpieces's index) ; this size ==
/// tokenized wordpieces.size() + 2 (first;2 and last;3)
std::pair<std::vector<std::string>, std::vector<int>>
WordPieceTokenizer::tokenize_with_ids(const std::string &text) {
    SHERPA_ONNX_LOGE(">>>> WordPieceTokenizer::tokenize_with_ids start");
  std::vector<std::string> basic_tokens = basic_tokenize(text);
  std::vector<std::string> wordpieces;
  std::vector<int> ids;

  ids.push_back(2);
  for (const auto &token : basic_tokens) {
    std::vector<std::string> pieces = wordpiece_tokenize(token);
    for (const auto &p : pieces) {
      wordpieces.push_back(p);
      auto it = vocab.find(p);
      if (it != vocab.end()) {
        ids.push_back(it->second);
      } else {
        ids.push_back(vocab.at("[UNK]"));
      }
    }
  }
  ids.push_back(3);
  SHERPA_ONNX_LOGE( ">>>> WordPieceTokenizer::tokenize_with_ids end");

  return {wordpieces, ids};
}
