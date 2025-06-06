// sherpa-onnx/csrc/offline-tts-vits-impl.h
//
// Copyright (c)  2023  Xiaomi Corporation
#ifndef SHERPA_ONNX_CSRC_OFFLINE_TTS_VITS_IMPL_H_
#define SHERPA_ONNX_CSRC_OFFLINE_TTS_VITS_IMPL_H_

#include <memory>
#include <string>
#include <strstream>
#include <utility>
#include <vector>

#include "fst/extensions/far/far.h"
#include "kaldifst/csrc/kaldi-fst-io.h"
#include "kaldifst/csrc/text-normalizer.h"
#include "sherpa-onnx/csrc/file-utils.h"
#include "sherpa-onnx/csrc/jieba-lexicon.h"
#include "sherpa-onnx/csrc/lexicon.h"
#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/melo-tts-lexicon.h"
#include "sherpa-onnx/csrc/offline-tts-character-frontend.h"
#include "sherpa-onnx/csrc/offline-tts-frontend.h"
#include "sherpa-onnx/csrc/offline-tts-impl.h"
#include "sherpa-onnx/csrc/offline-tts-vits-model.h"
#include "sherpa-onnx/csrc/piper-phonemize-lexicon.h"
#include "sherpa-onnx/csrc/text-utils.h"

namespace sherpa_onnx {

class OfflineTtsVitsImpl : public OfflineTtsImpl {
 public:
  explicit OfflineTtsVitsImpl(const OfflineTtsConfig &config)
      : config_(config),
        model_(std::make_unique<OfflineTtsVitsModel>(config.model)) {
    InitFrontend();

    SHERPA_ONNX_LOGE("!!OfflineTtsVitsImpl//Create model:");

    if (!config.rule_fsts.empty()) {
      std::vector<std::string> files;
      SplitStringToVector(config.rule_fsts, ",", false, &files);
      tn_list_.reserve(files.size());
      for (const auto &f : files) {
        if (config.model.debug) {
#if __OHOS__
          SHERPA_ONNX_LOGE("rule fst: %{public}s", f.c_str());
#else
          SHERPA_ONNX_LOGE("rule fst: %s", f.c_str());
#endif
        }
        tn_list_.push_back(std::make_unique<kaldifst::TextNormalizer>(f));
      }
    }

    if (!config.rule_fars.empty()) {
      if (config.model.debug) {
        SHERPA_ONNX_LOGE("Loading FST archives");
      }
      std::vector<std::string> files;
      SplitStringToVector(config.rule_fars, ",", false, &files);

      tn_list_.reserve(files.size() + tn_list_.size());

      for (const auto &f : files) {
        if (config.model.debug) {
#if __OHOS__
          SHERPA_ONNX_LOGE("rule far: %{public}s", f.c_str());
#else
          SHERPA_ONNX_LOGE("rule far: %s", f.c_str());
#endif
        }
        std::unique_ptr<fst::FarReader<fst::StdArc>> reader(
            fst::FarReader<fst::StdArc>::Open(f));
        for (; !reader->Done(); reader->Next()) {
          std::unique_ptr<fst::StdConstFst> r(
              fst::CastOrConvertToConstFst(reader->GetFst()->Copy()));

          tn_list_.push_back(
              std::make_unique<kaldifst::TextNormalizer>(std::move(r)));
        }
      }

      if (config.model.debug) {
        SHERPA_ONNX_LOGE("FST archives loaded!");
      }
    }
  }

  template <typename Manager>
  OfflineTtsVitsImpl(Manager *mgr, const OfflineTtsConfig &config)
      : config_(config),
        model_(std::make_unique<OfflineTtsVitsModel>(mgr, config.model)) {
    SHERPA_ONNX_LOGE(">>>> OfflineTtsVitsImpl constructor");
    InitFrontend(mgr);

    if (!config.rule_fsts.empty()) {
      std::vector<std::string> files;
      SplitStringToVector(config.rule_fsts, ",", false, &files);
      tn_list_.reserve(files.size());
      for (const auto &f : files) {
        if (config.model.debug) {
#if __OHOS__
          SHERPA_ONNX_LOGE("rule fst: %{public}s", f.c_str());
#else
          SHERPA_ONNX_LOGE("rule fst: %s", f.c_str());
#endif
        }
        auto buf = ReadFile(mgr, f);
        std::istrstream is(buf.data(), buf.size());
        tn_list_.push_back(std::make_unique<kaldifst::TextNormalizer>(is));
      }
    }

    if (!config.rule_fars.empty()) {
      std::vector<std::string> files;
      SplitStringToVector(config.rule_fars, ",", false, &files);
      tn_list_.reserve(files.size() + tn_list_.size());

      for (const auto &f : files) {
        if (config.model.debug) {
#if __OHOS__
          SHERPA_ONNX_LOGE("rule far: %{public}s", f.c_str());
#else
          SHERPA_ONNX_LOGE("rule far: %s", f.c_str());
#endif
        }

        auto buf = ReadFile(mgr, f);

        std::unique_ptr<std::istream> s(
            new std::istrstream(buf.data(), buf.size()));

        std::unique_ptr<fst::FarReader<fst::StdArc>> reader(
            fst::FarReader<fst::StdArc>::Open(std::move(s)));

        for (; !reader->Done(); reader->Next()) {
          std::unique_ptr<fst::StdConstFst> r(
              fst::CastOrConvertToConstFst(reader->GetFst()->Copy()));

          tn_list_.push_back(
              std::make_unique<kaldifst::TextNormalizer>(std::move(r)));
        }  // for (; !reader->Done(); reader->Next())
      }  // for (const auto &f : files)
    }  // if (!config.rule_fars.empty())
  }

  int32_t SampleRate() const override {
    return model_->GetMetaData().sample_rate;
  }

  int32_t NumSpeakers() const override {
    return model_->GetMetaData().num_speakers;
  }

  GeneratedAudio Generate(
      const std::string &_text, int64_t sid = 0, float speed = 1.0,
      GeneratedAudioCallback callback = nullptr) const override {
    SHERPA_ONNX_LOGE(">>> Generate offline-tts-vits start");
    const auto &meta_data = model_->GetMetaData();
    int32_t num_speakers = meta_data.num_speakers;

    SHERPA_ONNX_LOGE("sid: %d", static_cast<int32_t>(sid));
    SHERPA_ONNX_LOGE("speed: %f", speed);
    if (num_speakers == 0 && sid != 0) {
#if __OHOS__
      SHERPA_ONNX_LOGE(
          "This is a single-speaker model and supports only sid 0. Given sid: "
          "%{public}d. sid is ignored",
          static_cast<int32_t>(sid));
#else
      SHERPA_ONNX_LOGE(
          "This is a single-speaker model and supports only sid 0. Given sid: "
          "%d. sid is ignored",
          static_cast<int32_t>(sid));
#endif
    }

    if (num_speakers != 0 && (sid >= num_speakers || sid < 0)) {
#if __OHOS__
      SHERPA_ONNX_LOGE(
          "This model contains only %{public}d speakers. sid should be in the "
          "range [%{public}d, %{public}d]. Given: %{public}d. Use sid=0",
          num_speakers, 0, num_speakers - 1, static_cast<int32_t>(sid));
#else
      SHERPA_ONNX_LOGE(
          "This model contains only %d speakers. sid should be in the range "
          "[%d, %d]. Given: %d. Use sid=0",
          num_speakers, 0, num_speakers - 1, static_cast<int32_t>(sid));
#endif
      sid = 0;
    }

    std::string text = _text;
    if (config_.model.debug) {
#if __OHOS__
      SHERPA_ONNX_LOGE("Raw text: %{public}s", text.c_str());
#else
      SHERPA_ONNX_LOGE("Raw text: %s", text.c_str());
#endif
    }

    if (!tn_list_.empty()) {
      for (const auto &tn : tn_list_) {
        text = tn->Normalize(text);
        if (config_.model.debug) {
#if __OHOS__
          SHERPA_ONNX_LOGE("After normalizing: %{public}s", text.c_str());
#else
          SHERPA_ONNX_LOGE("After normalizing: %s", text.c_str());
#endif
        }
      }
    }

    /// ConvertTextToTokenIdsKorean
    std::vector<TokenIDs> token_ids =
        frontend_->ConvertTextToTokenIds(text, meta_data.voice);

    std::vector<float> ja_bert_vec = token_ids[0].ja_bert_vec;

    if (token_ids.empty() ||
        (token_ids.size() == 1 && token_ids[0].tokens.empty())) {
      SHERPA_ONNX_LOGE("Failed to convert %s to token IDs", text.c_str());
      return {};
    }

    std::vector<std::vector<int64_t>> x;
    std::vector<std::vector<int64_t>> tones;

    x.reserve(token_ids.size());
    SHERPA_ONNX_LOGE("this from ConvertTextToTokenIdsKorean ; tokens.size(): %zu", token_ids[0].tokens.size());
    for (auto &i : token_ids) {
      x.push_back(std::move(i.tokens));
    }
    SHERPA_ONNX_LOGE(">>>>> 0 x.size(): %zu", x.size());

    if (!token_ids[0].tones.empty()) {
      tones.reserve(token_ids.size());
      for (auto &i : token_ids) {
        tones.push_back(std::move(i.tones));
      }
    }

    // TODO(fangjun): add blank inside the frontend, not here
    if (meta_data.add_blank && config_.model.vits.data_dir.empty() &&
        meta_data.frontend != "characters") {
      for (auto &k : x) {
        k = AddBlank(k);
      }

      for (auto &k : tones) {
        k = AddBlank(k);
      }
      // 여기서 블랭크 추가 해줘야함 word2ph 각 항목 * 2 해줘야함, 맨 앞에 +1
      // 이걸 기준으로 ja_bert_vec 또한 해줘야함 
      /// melo-tts-lexicon.cc ConvertTextToTokenIdsKorean 에서 미리 처리함
      
    }
    SHERPA_ONNX_LOGE(">>>>> 1 x.size(): %zu", x.size());

    int32_t x_size = static_cast<int32_t>(x.size());

    if (config_.max_num_sentences <= 0 || x_size <= config_.max_num_sentences) {
      SHERPA_ONNX_LOGE(">>> Process all sentences offline-tts-vits create 0");
      auto ans = Process(text, ja_bert_vec, x, tones, sid, speed);
      SHERPA_ONNX_LOGE(">>> Process all sentences offline-tts-vits created 0");
      if (callback) {
        SHERPA_ONNX_LOGE(
            ">>> Process all sentences offline-tts-vits callback 0");
        callback(ans.samples.data(), ans.samples.size(), 1.0);
        SHERPA_ONNX_LOGE(
            ">>> Process all sentences offline-tts-vits callback 0");
      }
      return ans;
    }

    // the input text is too long, we process sentences within it in batches
    // to avoid OOM. Batch size is config_.max_num_sentences
    std::vector<std::vector<int64_t>> batch_x;
    std::vector<std::vector<int64_t>> batch_tones;

    int32_t batch_size = config_.max_num_sentences;
    batch_x.reserve(config_.max_num_sentences);
    batch_tones.reserve(config_.max_num_sentences);
    int32_t num_batches = x_size / batch_size;

    if (config_.model.debug) {
#if __OHOS__
      SHERPA_ONNX_LOGE(
          "Text is too long. Split it into %{public}d batches. batch size: "
          "%{public}d. Number of sentences: %{public}d",
          num_batches, batch_size, x_size);
#else
      SHERPA_ONNX_LOGE(
          "Text is too long. Split it into %d batches. batch size: %d. Number "
          "of sentences: %d",
          num_batches, batch_size, x_size);
#endif
    }

    GeneratedAudio ans;

    int32_t should_continue = 1;

    int32_t k = 0;

    SHERPA_ONNX_LOGE("Start processing sentences");

    for (int32_t b = 0; b != num_batches && should_continue; ++b) {
      batch_x.clear();
      batch_tones.clear();
      for (int32_t i = 0; i != batch_size; ++i, ++k) {
        batch_x.push_back(std::move(x[k]));

        if (!tones.empty()) {
          batch_tones.push_back(std::move(tones[k]));
        }
      }

      auto audio = Process(batch_x, batch_tones, sid, speed);
      ans.sample_rate = audio.sample_rate;
      ans.samples.insert(ans.samples.end(), audio.samples.begin(),
                         audio.samples.end());
      if (callback) {
        should_continue = callback(audio.samples.data(), audio.samples.size(),
                                   (b + 1) * 1.0 / num_batches);
        // Caution(fangjun): audio is freed when the callback returns, so users
        // should copy the data if they want to access the data after
        // the callback returns to avoid segmentation fault.
      }
    }
    SHERPA_ONNX_LOGE("Finished processing sentences");

    SHERPA_ONNX_LOGE("Start processing the remaining sentences");
    batch_x.clear();
    batch_tones.clear();
    while (k < static_cast<int32_t>(x.size()) && should_continue) {
      batch_x.push_back(std::move(x[k]));
      if (!tones.empty()) {
        batch_tones.push_back(std::move(tones[k]));
      }

      ++k;
    }
    SHERPA_ONNX_LOGE("Finished processing the remaining sentences");

    SHERPA_ONNX_LOGE("Start processing the last batch");
    if (!batch_x.empty()) {
      auto audio = Process(batch_x, batch_tones, sid, speed);
      ans.sample_rate = audio.sample_rate;
      ans.samples.insert(ans.samples.end(), audio.samples.begin(),
                         audio.samples.end());
      if (callback) {
        callback(audio.samples.data(), audio.samples.size(), 1.0);
        // Caution(fangjun): audio is freed when the callback returns, so users
        // should copy the data if they want to access the data after
        // the callback returns to avoid segmentation fault.
      }
    }
    SHERPA_ONNX_LOGE("Finished processing the last batch");

    SHERPA_ONNX_LOGE("Finished processing all sentences");
    return ans;
  }

 private:
  template <typename Manager>
  void InitFrontend(Manager *mgr) {
    const auto &meta_data = model_->GetMetaData();

    // melo tts korean test
    // meta_data.frontend = "characters";
    // hack: melo-tts-korean용
    // prrint

    SHERPA_ONNX_LOGE(
        ">>>> InitFrontEnd csrc/offline-tts-vits-impl.h meta_data.frontend: "
        "%s ",
        meta_data.frontend.c_str());
    SHERPA_ONNX_LOGE(
        ">>>> InitFrontEnd csrc/offline-tts-vits-impl.h "
        "config_.model.vits.model: %s",
        config_.model.vits.model.c_str());
    SHERPA_ONNX_LOGE(
        ">>>> InitFrontEnd csrc/offline-tts-vits-impl.h "
        "config_.model.vits.lexicon: %s",
        config_.model.vits.lexicon.c_str());
    SHERPA_ONNX_LOGE(
        ">>>> InitFrontEnd csrc/offline-tts-vits-impl.h "
        "config_.model.vits.tokens: %s",
        config_.model.vits.tokens.c_str());

    // if (model_->GetMetaData().language == "Korean" &&
    //     config_.model.vits.dict_dir.empty() &&
    //     config_.model.vits.lexicon.empty()) {
    //     SHERPA_ONNX_LOGE("!!InitFrontEnd forcely create init
    //     OfflineTtsCharacterFrontend: ");
    //   frontend_ = std::make_unique<OfflineTtsCharacterFrontend>(
    //       mgr, config_.model.vits.tokens, model_->GetMetaData()
    //     );
    //     SHERPA_ONNX_LOGE("!!InitFrontEnd forcely create success
    //     OfflineTtsCharacterFrontend: ");
    //   return;
    // }
    // 1. 한국어 MeloTTS 모델에 대한 조건 추가
    // meta_data.is_melo_tts가 true이고, meta_data.language가 "Korean"일 때 이
    // 경로를 타도록 합니다. lexicon과 tokens 경로는 config.json에서 가져오므로
    // 그대로 사용합니다.
    if (meta_data.is_melo_tts && meta_data.language == "Korean") {
      SHERPA_ONNX_LOGE(
          "!!InitFrontEnd create init MeloTtsLexicon for Korean: New Path");
      frontend_ = std::make_unique<MeloTtsLexicon>(
          mgr, config_.model.vits.lexicon, config_.model.vits.tokens,
          config_.model.vits.ja_bert_model, config_.model.vits.vocab,
          model_->GetMetaData(), config_.model.debug);
    } else if (meta_data.frontend == "characters") {
      SHERPA_ONNX_LOGE(
          "!!InitFrontEnd create init OfflineTtsCharacterFrontend:1 ");
      frontend_ = std::make_unique<OfflineTtsCharacterFrontend>(
          mgr, config_.model.vits.tokens, meta_data);
    } else if (meta_data.jieba && !config_.model.vits.dict_dir.empty() &&
               meta_data.is_melo_tts) {
      SHERPA_ONNX_LOGE(
          "!!InitFrontEnd create init OfflineTtsCharacterFrontend:2 ");
      frontend_ = std::make_unique<MeloTtsLexicon>(
          mgr, config_.model.vits.lexicon, config_.model.vits.tokens,
          config_.model.vits.dict_dir, model_->GetMetaData(),
          config_.model.debug);
    } else if (meta_data.jieba && !config_.model.vits.dict_dir.empty()) {
      SHERPA_ONNX_LOGE("!!InitFrontEnd create init JiebaLexicon:3 ");
      frontend_ = std::make_unique<JiebaLexicon>(
          mgr, config_.model.vits.lexicon, config_.model.vits.tokens,
          config_.model.vits.dict_dir, config_.model.debug);
    } else if (meta_data.is_melo_tts && meta_data.language == "English") {
      SHERPA_ONNX_LOGE("!!InitFrontEnd create init MeloTtsLexicon:4 ");
      frontend_ = std::make_unique<MeloTtsLexicon>(
          mgr, config_.model.vits.lexicon, config_.model.vits.tokens,
          model_->GetMetaData(), config_.model.debug);
    } else if ((meta_data.is_piper || meta_data.is_coqui ||
                meta_data.is_icefall) &&
               !config_.model.vits.data_dir.empty()) {
      SHERPA_ONNX_LOGE("!!InitFrontEnd create init PiperPhonemizeLexicon:5 ");
      frontend_ = std::make_unique<PiperPhonemizeLexicon>(
          mgr, config_.model.vits.tokens, config_.model.vits.data_dir,
          meta_data);
    } else if (meta_data.is_melo_tts) {
      SHERPA_ONNX_LOGE("!!InitFrontEnd create init MeloTtsLexicon:6 ");
      frontend_ = std::make_unique<MeloTtsLexicon>(
          mgr, config_.model.vits.lexicon, config_.model.vits.tokens,
          model_->GetMetaData(), config_.model.debug);
    } else {
      SHERPA_ONNX_LOGE("!!InitFrontEnd create init Lexicon:6 ");
      if (config_.model.vits.lexicon.empty()) {
        SHERPA_ONNX_LOGE(
            "Not a model using characters as modeling unit. Please provide "
            "--vits-lexicon if you leave --vits-data-dir empty");
        exit(-1);
      }

      frontend_ = std::make_unique<Lexicon>(
          mgr, config_.model.vits.lexicon, config_.model.vits.tokens,
          meta_data.punctuations, meta_data.language, config_.model.debug);
    }
  }

  void InitFrontend() {
    const auto &meta_data = model_->GetMetaData();

    if (meta_data.jieba && config_.model.vits.dict_dir.empty()) {
      SHERPA_ONNX_LOGE(
          "Please provide --vits-dict-dir for Chinese TTS models using jieba");
      exit(-1);
    }

    if (!meta_data.jieba && !config_.model.vits.dict_dir.empty()) {
      SHERPA_ONNX_LOGE(
          "Current model is not using jieba but you provided --vits-dict-dir");
      exit(-1);
    }

    if (meta_data.frontend == "characters") {
      frontend_ = std::make_unique<OfflineTtsCharacterFrontend>(
          config_.model.vits.tokens, meta_data);
    } else if (meta_data.jieba && !config_.model.vits.dict_dir.empty() &&
               meta_data.is_melo_tts) {
      frontend_ = std::make_unique<MeloTtsLexicon>(
          config_.model.vits.lexicon, config_.model.vits.tokens,
          config_.model.vits.dict_dir, model_->GetMetaData(),
          config_.model.debug);
    } else if (meta_data.is_melo_tts && meta_data.language == "English") {
      frontend_ = std::make_unique<MeloTtsLexicon>(
          config_.model.vits.lexicon, config_.model.vits.tokens,
          model_->GetMetaData(), config_.model.debug);
    } else if (meta_data.jieba && !config_.model.vits.dict_dir.empty()) {
      frontend_ = std::make_unique<JiebaLexicon>(
          config_.model.vits.lexicon, config_.model.vits.tokens,
          config_.model.vits.dict_dir, config_.model.debug);
    } else if ((meta_data.is_piper || meta_data.is_coqui ||
                meta_data.is_icefall) &&
               !config_.model.vits.data_dir.empty()) {
      frontend_ = std::make_unique<PiperPhonemizeLexicon>(
          config_.model.vits.tokens, config_.model.vits.data_dir,
          model_->GetMetaData());
    } else {
      if (config_.model.vits.lexicon.empty()) {
        SHERPA_ONNX_LOGE(
            "Not a model using characters as modeling unit. Please provide "
            "--vits-lexicon if you leave --vits-data-dir empty");
        exit(-1);
      }
      frontend_ = std::make_unique<Lexicon>(
          config_.model.vits.lexicon, config_.model.vits.tokens,
          meta_data.punctuations, meta_data.language, config_.model.debug);
    }
  }

  GeneratedAudio Process(const std::string &text,
                          std::vector<float> &ja_bert_vec,
                         const std::vector<std::vector<int64_t>> &tokens,
                         const std::vector<std::vector<int64_t>> &tones,
                         int32_t sid, float speed) const {
    SHERPA_ONNX_LOGE(" >>> text Process offline-tts-vits-impl.h start");

    int32_t num_tokens = 0;
    for (const auto &k : tokens) {
      num_tokens += k.size();
    }
    SHERPA_ONNX_LOGE(" >>>text Process offline-tts-vits-impl.h num_tokens %d",
                     num_tokens);

    std::vector<int64_t> x;
    x.reserve(num_tokens);
    for (const auto &k : tokens) {
      x.insert(x.end(), k.begin(), k.end());
    }
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x.size() %d",
                     x.size());
    for (int i = 0; i < 10; i++) {
      SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x[%d] %d", i,
                       x[i]);
    }

    std::vector<int64_t> tone_list;
    if (!tones.empty()) {
      tone_list.reserve(num_tokens);
      for (const auto &k : tones) {
        tone_list.insert(tone_list.end(), k.begin(), k.end());
      }
    }
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h tone_list.size() %d",
                     tone_list.size());

    // for (int i = 0; i < 10; i++) {
    //   SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h tone_list[%d] %d",
    //                    i, tone_list[i]);
    // }
    // SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h sid %d", sid);
    // SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h speed %f", speed);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h memory_info  start");
    auto memory_info =
        Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h memory_info end ");

    std::array<int64_t, 2> x_shape = {1, static_cast<int32_t>(x.size())};
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x_shape[0] %d",
                     x_shape[0]);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x_shape[1] %d",
                     x_shape[1]);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x.size() %d",
                     x.size());
    Ort::Value x_tensor = Ort::Value::CreateTensor(
        memory_info, x.data(), x.size(), x_shape.data(), x_shape.size());
    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h "
        "x_tensor.GetTensorTypeAndShapeInfo().GetShape()[0] %d",
        x_tensor.GetTensorTypeAndShapeInfo().GetShape()[0]);

    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h "
        "x_tensor.GetTensorTypeAndShapeInfo().GetShape()[1] %d",
        x_tensor.GetTensorTypeAndShapeInfo().GetShape()[1]);
    Ort::Value tones_tensor{nullptr};
    if (!tones.empty()) {
      tones_tensor = Ort::Value::CreateTensor(memory_info, tone_list.data(),
                                              tone_list.size(), x_shape.data(),
                                              x_shape.size());
      SHERPA_ONNX_LOGE(
          " >>> Process offline-tts-vits-impl.h "
          "tones_tensor.GetTensorTypeAndShapeInfo().GetShape()[0] %d",
          tones_tensor.GetTensorTypeAndShapeInfo().GetShape()[0]);
      SHERPA_ONNX_LOGE(
          " >>> Process offline-tts-vits-impl.h "
          "tones_tensor.GetTensorTypeAndShapeInfo().GetShape()[1] %d",
          tones_tensor.GetTensorTypeAndShapeInfo().GetShape()[1]);
    }

    Ort::Value audio{nullptr};
    if (tones.empty()) {
      SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h no tones");
      audio = model_->Run(std::move(x_tensor), sid, speed);
    } else {
      SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h has tones");
      // audio =
      //     model_->Run(std::move(x_tensor), std::move(tones_tensor), sid,
      //     speed);
      SHERPA_ONNX_LOGE(">>>> Process offline-tts-vits-impl.h has tones ja_bert_vec.size() %d", ja_bert_vec.size());
      audio = model_->Run(text, ja_bert_vec, std::move(x_tensor),
                          std::move(tones_tensor), sid, speed);
    }

    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h "
        "audio.GetTensorTypeAndShapeInfo().GetShape()[0] %d",
        audio.GetTensorTypeAndShapeInfo().GetShape()[0]);

    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h "
        "audio.GetTensorTypeAndShapeInfo().GetShape()[1] %d",
        audio.GetTensorTypeAndShapeInfo().GetShape()[1]);
    std::vector<int64_t> audio_shape =
        audio.GetTensorTypeAndShapeInfo().GetShape();

    int64_t total = 1;
    // The output shape may be (1, 1, total) or (1, total) or (total,)
    for (auto i : audio_shape) {
      total *= i;
    }

    const float *p = audio.GetTensorData<float>();

    GeneratedAudio ans;
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h total %d", total);
    ans.sample_rate = model_->GetMetaData().sample_rate;
    ans.samples = std::vector<float>(p, p + total);
    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h ans.samples.size() %d",
        ans.samples.size());

    float silence_scale = config_.silence_scale;
    if (silence_scale != 1) {
      SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h silence_scale %f",
                       silence_scale);
      ans = ans.ScaleSilence(silence_scale);
      SHERPA_ONNX_LOGE(
          " >>> Process offline-tts-vits-impl.h ans.samples.size() %d",
          ans.samples.size());
    }

    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h end ");

    return ans;
  }
  GeneratedAudio Process(const std::vector<std::vector<int64_t>> &tokens,
                         const std::vector<std::vector<int64_t>> &tones,
                         int32_t sid, float speed) const {
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h start");

    int32_t num_tokens = 0;
    for (const auto &k : tokens) {
      num_tokens += k.size();
    }
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h num_tokens %d",
                     num_tokens);

    std::vector<int64_t> x;
    x.reserve(num_tokens);
    for (const auto &k : tokens) {
      x.insert(x.end(), k.begin(), k.end());
    }
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x.size() %d",
                     x.size());
    for (int i = 0; i < 10; i++) {
      SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x[%d] %d", i,
                       x[i]);
    }

    std::vector<int64_t> tone_list;
    if (!tones.empty()) {
      tone_list.reserve(num_tokens);
      for (const auto &k : tones) {
        tone_list.insert(tone_list.end(), k.begin(), k.end());
      }
    }
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h tone_list.size() %d",
                     tone_list.size());

    // for (int i = 0; i < 10; i++) {
    //   SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h tone_list[%d] %d",
    //                    i, tone_list[i]);
    // }
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h sid %d", sid);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h speed %f", speed);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h memory_info  start");
    auto memory_info =
        Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h memory_info end ");

    std::array<int64_t, 2> x_shape = {1, static_cast<int32_t>(x.size())};
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x_shape[0] %d",
                     x_shape[0]);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x_shape[1] %d",
                     x_shape[1]);
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h x.size() %d",
                     x.size());
    Ort::Value x_tensor = Ort::Value::CreateTensor(
        memory_info, x.data(), x.size(), x_shape.data(), x_shape.size());
    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h "
        "x_tensor.GetTensorTypeAndShapeInfo().GetShape()[0] %d",
        x_tensor.GetTensorTypeAndShapeInfo().GetShape()[0]);

    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h "
        "x_tensor.GetTensorTypeAndShapeInfo().GetShape()[1] %d",
        x_tensor.GetTensorTypeAndShapeInfo().GetShape()[1]);
    Ort::Value tones_tensor{nullptr};
    if (!tones.empty()) {
      tones_tensor = Ort::Value::CreateTensor(memory_info, tone_list.data(),
                                              tone_list.size(), x_shape.data(),
                                              x_shape.size());
      SHERPA_ONNX_LOGE(
          " >>> Process offline-tts-vits-impl.h "
          "tones_tensor.GetTensorTypeAndShapeInfo().GetShape()[0] %d",
          tones_tensor.GetTensorTypeAndShapeInfo().GetShape()[0]);
      SHERPA_ONNX_LOGE(
          " >>> Process offline-tts-vits-impl.h "
          "tones_tensor.GetTensorTypeAndShapeInfo().GetShape()[1] %d",
          tones_tensor.GetTensorTypeAndShapeInfo().GetShape()[1]);
    }

    Ort::Value audio{nullptr};
    if (tones.empty()) {
      SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h no tones");
      audio = model_->Run(std::move(x_tensor), sid, speed);
    } else {
      SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h has tones");
      audio =
          model_->Run(std::move(x_tensor), std::move(tones_tensor), sid, speed);
      // audio = model_->Run(text,std::move(x_tensor), std::move(tones_tensor),
      // sid, speed,frontend_.get());
    }

    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h "
        "audio.GetTensorTypeAndShapeInfo().GetShape()[0] %d",
        audio.GetTensorTypeAndShapeInfo().GetShape()[0]);

    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h "
        "audio.GetTensorTypeAndShapeInfo().GetShape()[1] %d",
        audio.GetTensorTypeAndShapeInfo().GetShape()[1]);
    std::vector<int64_t> audio_shape =
        audio.GetTensorTypeAndShapeInfo().GetShape();

    int64_t total = 1;
    // The output shape may be (1, 1, total) or (1, total) or (total,)
    for (auto i : audio_shape) {
      total *= i;
    }

    const float *p = audio.GetTensorData<float>();

    GeneratedAudio ans;
    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h total %d", total);
    ans.sample_rate = model_->GetMetaData().sample_rate;
    ans.samples = std::vector<float>(p, p + total);
    SHERPA_ONNX_LOGE(
        " >>> Process offline-tts-vits-impl.h ans.samples.size() %d",
        ans.samples.size());

    float silence_scale = config_.silence_scale;
    if (silence_scale != 1) {
      SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h silence_scale %f",
                       silence_scale);
      ans = ans.ScaleSilence(silence_scale);
      SHERPA_ONNX_LOGE(
          " >>> Process offline-tts-vits-impl.h ans.samples.size() %d",
          ans.samples.size());
    }

    SHERPA_ONNX_LOGE(" >>> Process offline-tts-vits-impl.h end ");

    return ans;
  }

  std::unique_ptr<OfflineTtsFrontend> frontend_;

 private:
  OfflineTtsConfig config_;
  std::unique_ptr<OfflineTtsVitsModel> model_;
  std::vector<std::unique_ptr<kaldifst::TextNormalizer>> tn_list_;
};

}  // namespace sherpa_onnx
#endif  // SHERPA_ONNX_CSRC_OFFLINE_TTS_VITS_IMPL_H_
