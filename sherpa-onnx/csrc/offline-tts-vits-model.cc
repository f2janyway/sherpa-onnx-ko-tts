// sherpa-onnx/csrc/offline-tts-vits-model.cc
//
// Copyright (c)  2023  Xiaomi Corporation

#include "sherpa-onnx/csrc/offline-tts-vits-model.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#if __ANDROID_API__ >= 9
#include "android/asset_manager.h"
#include "android/asset_manager_jni.h"
#endif

#if __OHOS__
#include "rawfile/raw_file_manager.h"
#endif

#include "melo-tts-lexicon.h"
#include "offline-tts-vits-model.h"
#include "sherpa-onnx/csrc/file-utils.h"
#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/melo-tts-ko-const.h"
#include "sherpa-onnx/csrc/melo-tts-ko.h"
#include "sherpa-onnx/csrc/onnx-utils.h"
#include "sherpa-onnx/csrc/session.h"

namespace sherpa_onnx {

class OfflineTtsVitsModel::Impl {
 public:
  explicit Impl(const OfflineTtsModelConfig &config)
      : config_(config),
        env_(ORT_LOGGING_LEVEL_ERROR),
        sess_opts_(GetSessionOptions(config)),
        allocator_{} {
    auto buf = ReadFile(config.vits.model);
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl Impl offline-tts-vits-model.cc 0");
    Init(buf.data(), buf.size());
  }

  template <typename Manager>
  Impl(Manager *mgr, const OfflineTtsModelConfig &config)
      : config_(config),
        env_(ORT_LOGGING_LEVEL_ERROR),
        sess_opts_(GetSessionOptions(config)),
        allocator_{}

  {
    auto buf = ReadFile(mgr, config.vits.model);
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl Impl offline-tts-vits-model.cc 1");
    Init(buf.data(), buf.size());
  }

  Ort::Value Run(Ort::Value x, int64_t sid, float speed) {
    if (meta_data_.is_piper || meta_data_.is_coqui) {
      return RunVitsPiperOrCoqui(std::move(x), sid, speed);
    }

    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl Ort::Value Run "
        "offline-tts-vits-model.cc ");
    return RunVits(std::move(x), sid, speed);
  }
  Ort::Value Run(const std::string &text, std::vector<float> &ja_bert_vec,
                 Ort::Value x, Ort::Value tones, int64_t sid, float speed) {
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::Run(ja_bert) "
        "csrc/offline-tts-vits-model.cc start");

    // tokenzie -> ja_bert

    if (meta_data_.num_speakers == 1) {
      // For MeloTTS, we hardcode sid to the one contained in the meta data
      sid = meta_data_.speaker_id;
    }

    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::Run(ja_bert) "
        "csrc/offline-tts-vits-model.cc create cpu");
    auto memory_info =
        Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::Run(ja_bert) "
        "csrc/offline-tts-vits-model.cc xshape");

    std::vector<int64_t> x_shape = x.GetTensorTypeAndShapeInfo().GetShape();
    if (x_shape[0] != 1) {
      SHERPA_ONNX_LOGE("Support only batch_size == 1. Given: %d",
                       static_cast<int32_t>(x_shape[0]));
      exit(-1);
    }

    int64_t len = x_shape[1];
    int64_t len_shape = 1;
    // --- Start of added code for ja_bert ---
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::Run() csrc/offline-tts-vits-model.cc "
        "ja_bert tensor len %d",
        len);

    // Define the shape for ja_bert
    std::vector<int64_t> ja_bert_shape = {1, 768, len};

    SHERPA_ONNX_LOGE(
        "DEBUG: ja_bert_vec actual size: %zu, 768 * %zu = %zu, this should "
        "same",
        ja_bert_vec.size(), len, 768 * len);

    // for (int i = 0; i < 5; i++) {
    //   SHERPA_ONNX_LOGE("ja_bert_vec[%d] = %f", i, ja_bert_vec[i]);
    // }
    SHERPA_ONNX_LOGE("Dumping first 10 elements of ja_bert_vec:");
    for (int i = 0; i < std::min((size_t)10, ja_bert_vec.size()); ++i) {
      SHERPA_ONNX_LOGE("ja_bert_vec[%d]: %f", i, ja_bert_vec[i]);
    }
    // You might also want to check for NaNs or Infs
    for (float val : ja_bert_vec) {
      if (std::isnan(val) || std::isinf(val)) {
        SHERPA_ONNX_LOGE("ERROR: ja_bert_vec contains NaN or Inf!");
        break;
      }
    }
    Ort::Value ja_bert_tensor = Ort::Value::CreateTensor<float>(
        memory_info, ja_bert_vec.data(), ja_bert_vec.size(),
        ja_bert_shape.data(), ja_bert_shape.size());
    SHERPA_ONNX_LOGE(
        ">>>>> OfflineTtsVitsModel::Impl::Run() csrc/offline-tts-vits-model.cc "
        "ja_bert tensor created");

    // --- End of added code for ja_bert ---
    Ort::Value x_length =
        Ort::Value::CreateTensor(memory_info, &len, 1, &len_shape, 1);

    int64_t scale_shape = 1;
    float noise_scale = config_.vits.noise_scale;
    float length_scale = config_.vits.length_scale;
    float noise_scale_w = config_.vits.noise_scale_w;

    if (speed != 1 && speed > 0) {
      length_scale = 1. / speed;
    }

    Ort::Value noise_scale_tensor =
        Ort::Value::CreateTensor(memory_info, &noise_scale, 1, &scale_shape, 1);

    Ort::Value length_scale_tensor = Ort::Value::CreateTensor(
        memory_info, &length_scale, 1, &scale_shape, 1);

    Ort::Value noise_scale_w_tensor = Ort::Value::CreateTensor(
        memory_info, &noise_scale_w, 1, &scale_shape, 1);

    Ort::Value sid_tensor =
        Ort::Value::CreateTensor(memory_info, &sid, 1, &scale_shape, 1);

    std::vector<Ort::Value> inputs;
    // inputs.reserve(7);
    inputs.reserve(8);
    inputs.push_back(std::move(x));
    inputs.push_back(std::move(x_length));
    inputs.push_back(std::move(tones));
    inputs.push_back(std::move(sid_tensor));
    inputs.push_back(std::move(noise_scale_tensor));    // Reordered
    inputs.push_back(std::move(length_scale_tensor));   // Reordered
    inputs.push_back(std::move(noise_scale_w_tensor));  // Reordered
    inputs.push_back(std::move(ja_bert_tensor));
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::Run() csrc/offline-tts-vits-model.cc "
        "sess_->Run() start");
    auto out =
        sess_->Run({}, input_names_ptr_.data(), inputs.data(), inputs.size(),
                   output_names_ptr_.data(), output_names_ptr_.size());
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::Run() csrc/offline-tts-vits-model.cc "
        "sess_->Run() end");

    return std::move(out[0]);
  }

  Ort::Value Run(Ort::Value x, Ort::Value tones, int64_t sid, float speed) {
    SHERPA_ONNX_LOGE(
        ">>>>origin OfflineTtsVitsModel::Impl::Run() "
        "csrc/offline-tts-vits-model.cc start");

    // tokenzie -> ja_bert

    if (meta_data_.num_speakers == 1) {
      // For MeloTTS, we hardcode sid to the one contained in the meta data
      sid = meta_data_.speaker_id;
    }

    SHERPA_ONNX_LOGE(
        ">>>>origin OfflineTtsVitsModel::Impl::Run() "
        "csrc/offline-tts-vits-model.cc "
        "create cpu");
    auto memory_info =
        Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

    SHERPA_ONNX_LOGE(
        ">>>>origin OfflineTtsVitsModel::Impl::Run() "
        "csrc/offline-tts-vits-model.cc "
        "xshape");

    std::vector<int64_t> x_shape = x.GetTensorTypeAndShapeInfo().GetShape();
    if (x_shape[0] != 1) {
      SHERPA_ONNX_LOGE("Support only batch_size == 1. Given: %d",
                       static_cast<int32_t>(x_shape[0]));
      exit(-1);
    }

    std::vector<float> ja_bert_vec = {};
    int64_t len = x_shape[1];
    int64_t len_shape = 1;
    // --- Start of added code for ja_bert ---
    SHERPA_ONNX_LOGE(
        ">>>>origin OfflineTtsVitsModel::Impl::Run() "
        "csrc/offline-tts-vits-model.cc  "
        "ja_bert tensor len %d",
        len);

    // Define the shape for ja_bert
    std::vector<int64_t> ja_bert_shape = {1, 768, len};

    // Calculate the total number of elements
    // size_t ja_bert_num_elements = 1 * 768 * len;

    // Create a dummy float array for the tensor data
    // You might want to initialize this with meaningful data if needed
    // std::vector<float> dummy_ja_bert_data = ja_bert_vec;
    // ReadJaBert(dummy_ja_bert_data);
    for (int i = 0; i < 5; i++) {
      SHERPA_ONNX_LOGE(
          ">>>>origin OfflineTtsVitsModel::Impl::Run() "
          "csrc/offline-tts-vits-model.cc dummy ja_bert data %f",
          ja_bert_vec[i]);
    }
    SHERPA_ONNX_LOGE("111DEBUG: ja_bert_vec actual size: %zu",
                     ja_bert_vec.size());
    assert(ja_bert_vec.size() == 768 * len);
    // Create the Ort::Value for ja_bert
    Ort::Value ja_bert_tensor = Ort::Value::CreateTensor<float>(
        memory_info, ja_bert_vec.data(), ja_bert_vec.size(),
        ja_bert_shape.data(), ja_bert_shape.size());

    SHERPA_ONNX_LOGE(
        ">>>>origin OfflineTtsVitsModel::Impl::Run() "
        "csrc/offline-tts-vits-model.cc "
        "dummy ja_bert tensor created");
    // --- End of added code for ja_bert ---
    Ort::Value x_length =
        Ort::Value::CreateTensor(memory_info, &len, 1, &len_shape, 1);

    int64_t scale_shape = 1;
    float noise_scale = config_.vits.noise_scale;
    float length_scale = config_.vits.length_scale;
    float noise_scale_w = config_.vits.noise_scale_w;

    if (speed != 1 && speed > 0) {
      length_scale = 1. / speed;
    }

    Ort::Value noise_scale_tensor =
        Ort::Value::CreateTensor(memory_info, &noise_scale, 1, &scale_shape, 1);

    Ort::Value length_scale_tensor = Ort::Value::CreateTensor(
        memory_info, &length_scale, 1, &scale_shape, 1);

    Ort::Value noise_scale_w_tensor = Ort::Value::CreateTensor(
        memory_info, &noise_scale_w, 1, &scale_shape, 1);

    Ort::Value sid_tensor =
        Ort::Value::CreateTensor(memory_info, &sid, 1, &scale_shape, 1);

    std::vector<Ort::Value> inputs;
    // inputs.reserve(7);
    inputs.reserve(8);
    inputs.push_back(std::move(x));
    inputs.push_back(std::move(x_length));
    inputs.push_back(std::move(tones));
    inputs.push_back(std::move(sid_tensor));
    inputs.push_back(std::move(noise_scale_tensor));    // Reordered
    inputs.push_back(std::move(length_scale_tensor));   // Reordered
    inputs.push_back(std::move(noise_scale_w_tensor));  // Reordered
    inputs.push_back(std::move(ja_bert_tensor));
    SHERPA_ONNX_LOGE(
        ">>>>origin OfflineTtsVitsModel::Impl::Run() "
        "csrc/offline-tts-vits-model.cc "
        "sess_->Run() start");
    auto out =
        sess_->Run({}, input_names_ptr_.data(), inputs.data(), inputs.size(),
                   output_names_ptr_.data(), output_names_ptr_.size());
    SHERPA_ONNX_LOGE(
        ">>>>origin no used OfflineTtsVitsModel::Impl::Run() "
        "csrc/offline-tts-vits-model.cc "
        "sess_->Run() end");

    return std::move(out[0]);
  }

  const OfflineTtsVitsModelMetaData &GetMetaData() const { return meta_data_; }

 private:
  void Init(void *model_data, size_t model_data_length) {
    sess_ = std::make_unique<Ort::Session>(env_, model_data, model_data_length,
                                           sess_opts_);

    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::Init() csrc/offline-tts-vits-model.cc "
        "start");
    GetInputNames(sess_.get(), &input_names_, &input_names_ptr_);

    GetOutputNames(sess_.get(), &output_names_, &output_names_ptr_);

    // get meta data
    Ort::ModelMetadata meta_data = sess_->GetModelMetadata();
    if (config_.debug) {
      std::ostringstream os;
      os << "---vits model---\n";
      PrintModelMetadata(os, meta_data);

      os << "----------input names----------\n";
      int32_t i = 0;
      for (const auto &s : input_names_) {
        os << i << " " << s << "\n";
        ++i;
      }
      os << "----------output names----------\n";
      i = 0;
      for (const auto &s : output_names_) {
        os << i << " " << s << "\n";
        ++i;
      }

#if __OHOS__
      SHERPA_ONNX_LOGE("%{public}s\n", os.str().c_str());
#else
      SHERPA_ONNX_LOGE("%s\n", os.str().c_str());
#endif
    }

    Ort::AllocatorWithDefaultOptions allocator;  // used in the macro below
    SHERPA_ONNX_READ_META_DATA(meta_data_.sample_rate, "sample_rate");
    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.add_blank, "add_blank",
                                            0);

    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.speaker_id, "speaker_id",
                                            0);
    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.version, "version", 0);
    SHERPA_ONNX_READ_META_DATA(meta_data_.num_speakers, "n_speakers");
    SHERPA_ONNX_READ_META_DATA_STR_WITH_DEFAULT(meta_data_.punctuations,
                                                "punctuation", "");
    SHERPA_ONNX_READ_META_DATA_STR(meta_data_.language, "language");

    SHERPA_ONNX_READ_META_DATA_STR_WITH_DEFAULT(meta_data_.voice, "voice", "");

    SHERPA_ONNX_READ_META_DATA_STR_WITH_DEFAULT(meta_data_.frontend, "frontend",
                                                "");

    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.jieba, "jieba", 0);
    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.blank_id, "blank_id", 0);
    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.bos_id, "bos_id", 0);
    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.eos_id, "eos_id", 0);
    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.use_eos_bos,
                                            "use_eos_bos", 1);
    SHERPA_ONNX_READ_META_DATA_WITH_DEFAULT(meta_data_.pad_id, "pad_id", 0);

    std::string comment;
    SHERPA_ONNX_READ_META_DATA_STR(comment, "comment");

    if (comment.find("piper") != std::string::npos) {
      meta_data_.is_piper = true;
    }

    if (comment.find("coqui") != std::string::npos) {
      meta_data_.is_coqui = true;
    }

    if (comment.find("icefall") != std::string::npos) {
      meta_data_.is_icefall = true;
    }

    if (comment.find("melo") != std::string::npos) {
      meta_data_.is_melo_tts = true;
      int32_t expected_version = 2;
      if (meta_data_.version < expected_version) {
        SHERPA_ONNX_LOGE(
            "Please download the latest MeloTTS model and retry. Current "
            "version: %d. Expected version: %d",
            meta_data_.version, expected_version);
        exit(-1);
      }

      // NOTE(fangjun):
      // version 0 is the first version
      // version 2: add jieba=1 to the metadata
    }
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::Init() csrc/offline-tts-vits-model.cc "
        " end");
  }

  Ort::Value RunVitsPiperOrCoqui(Ort::Value x, int64_t sid, float speed) {
    auto memory_info =
        Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

    std::vector<int64_t> x_shape = x.GetTensorTypeAndShapeInfo().GetShape();
    if (x_shape[0] != 1) {
      SHERPA_ONNX_LOGE("Support only batch_size == 1. Given: %d",
                       static_cast<int32_t>(x_shape[0]));
      exit(-1);
    }

    int64_t len = x_shape[1];
    int64_t len_shape = 1;

    Ort::Value x_length =
        Ort::Value::CreateTensor(memory_info, &len, 1, &len_shape, 1);

    float noise_scale = config_.vits.noise_scale;
    float length_scale = config_.vits.length_scale;
    float noise_scale_w = config_.vits.noise_scale_w;

    if (speed != 1 && speed > 0) {
      length_scale = 1. / speed;
    }
    std::array<float, 3> scales = {noise_scale, length_scale, noise_scale_w};

    int64_t scale_shape = 3;

    Ort::Value scales_tensor = Ort::Value::CreateTensor(
        memory_info, scales.data(), scales.size(), &scale_shape, 1);

    int64_t sid_shape = 1;
    Ort::Value sid_tensor =
        Ort::Value::CreateTensor(memory_info, &sid, 1, &sid_shape, 1);

    int64_t lang_id_shape = 1;
    int64_t lang_id = 0;
    Ort::Value lang_id_tensor =
        Ort::Value::CreateTensor(memory_info, &lang_id, 1, &lang_id_shape, 1);

    std::vector<Ort::Value> inputs;
    inputs.reserve(5);
    inputs.push_back(std::move(x));
    inputs.push_back(std::move(x_length));
    inputs.push_back(std::move(scales_tensor));

    if (input_names_.size() >= 4 && input_names_[3] == "sid") {
      inputs.push_back(std::move(sid_tensor));
    }

    if (input_names_.size() >= 5 && input_names_[4] == "langid") {
      inputs.push_back(std::move(lang_id_tensor));
    }

    auto out =
        sess_->Run({}, input_names_ptr_.data(), inputs.data(), inputs.size(),
                   output_names_ptr_.data(), output_names_ptr_.size());

    return std::move(out[0]);
  }

  Ort::Value RunVits(Ort::Value x, int64_t sid, float speed) {
    SHERPA_ONNX_LOGE(
        ">>>> OfflineTtsVitsModel::Impl::RunVits() "
        "csrc/offline-tts-vits-model");
    auto memory_info =
        Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);

    std::vector<int64_t> x_shape = x.GetTensorTypeAndShapeInfo().GetShape();
    if (x_shape[0] != 1) {
      SHERPA_ONNX_LOGE("Support only batch_size == 1. Given: %d",
                       static_cast<int32_t>(x_shape[0]));
      exit(-1);
    }

    int64_t len = x_shape[1];
    int64_t len_shape = 1;

    Ort::Value x_length =
        Ort::Value::CreateTensor(memory_info, &len, 1, &len_shape, 1);

    int64_t scale_shape = 1;
    float noise_scale = config_.vits.noise_scale;
    float length_scale = config_.vits.length_scale;
    float noise_scale_w = config_.vits.noise_scale_w;

    if (speed != 1 && speed > 0) {
      length_scale = 1. / speed;
    }

    Ort::Value noise_scale_tensor =
        Ort::Value::CreateTensor(memory_info, &noise_scale, 1, &scale_shape, 1);

    Ort::Value length_scale_tensor = Ort::Value::CreateTensor(
        memory_info, &length_scale, 1, &scale_shape, 1);

    Ort::Value noise_scale_w_tensor = Ort::Value::CreateTensor(
        memory_info, &noise_scale_w, 1, &scale_shape, 1);

    Ort::Value sid_tensor =
        Ort::Value::CreateTensor(memory_info, &sid, 1, &scale_shape, 1);

    std::vector<Ort::Value> inputs;
    inputs.reserve(6);
    inputs.push_back(std::move(x));
    inputs.push_back(std::move(x_length));
    inputs.push_back(std::move(noise_scale_tensor));
    inputs.push_back(std::move(length_scale_tensor));
    inputs.push_back(std::move(noise_scale_w_tensor));

    if (input_names_.size() == 6 &&
        (input_names_.back() == "sid" || input_names_.back() == "speaker")) {
      inputs.push_back(std::move(sid_tensor));
    }

    auto out =
        sess_->Run({}, input_names_ptr_.data(), inputs.data(), inputs.size(),
                   output_names_ptr_.data(), output_names_ptr_.size());

    return std::move(out[0]);
  }

 private:
  OfflineTtsModelConfig config_;
  Ort::Env env_;
  Ort::SessionOptions sess_opts_;
  Ort::AllocatorWithDefaultOptions allocator_;

  std::unique_ptr<Ort::Session> sess_;

  std::vector<std::string> input_names_;
  std::vector<const char *> input_names_ptr_;

  std::vector<std::string> output_names_;
  std::vector<const char *> output_names_ptr_;

  OfflineTtsVitsModelMetaData meta_data_;
};

OfflineTtsVitsModel::OfflineTtsVitsModel(const OfflineTtsModelConfig &config)
    : impl_(std::make_unique<Impl>(config)) {}

template <typename Manager>
OfflineTtsVitsModel::OfflineTtsVitsModel(Manager *mgr,
                                         const OfflineTtsModelConfig &config)
    : impl_(std::make_unique<Impl>(mgr, config)) {}

OfflineTtsVitsModel::~OfflineTtsVitsModel() = default;

Ort::Value OfflineTtsVitsModel::Run(Ort::Value x, int64_t sid /*=0*/,
                                    float speed /*= 1.0*/) {
  return impl_->Run(std::move(x), sid, speed);
}

Ort::Value OfflineTtsVitsModel::Run(Ort::Value x, Ort::Value tones,
                                    int64_t sid /*= 0*/,
                                    float speed /*= 1.0*/) const {
  SHERPA_ONNX_LOGE(
      ">>>> OfflineTtsVitsModel::Run() csrc/offline-tts-vits-model.cc");
  return impl_->Run(std::move(x), std::move(tones), sid, speed);
}

Ort::Value OfflineTtsVitsModel::Run(const std::string &text,
                                    std::vector<float> &ja_bert_vec,
                                    Ort::Value x, Ort::Value tones, int64_t sid,
                                    float speed) {
  SHERPA_ONNX_LOGE(
      ">>>> OfflineTtsVitsModel::Run() csrc/offline-tts-vits-model.cc passed "
      "frontend");
  return impl_->Run(text, ja_bert_vec, std::move(x), std::move(tones), sid,
                    speed);
}
const OfflineTtsVitsModelMetaData &OfflineTtsVitsModel::GetMetaData() const {
  return impl_->GetMetaData();
}

#if __ANDROID_API__ >= 9
template OfflineTtsVitsModel::OfflineTtsVitsModel(
    AAssetManager *mgr, const OfflineTtsModelConfig &config);
#endif

#if __OHOS__
template OfflineTtsVitsModel::OfflineTtsVitsModel(
    NativeResourceManager *mgr, const OfflineTtsModelConfig &config);
#endif

}  // namespace sherpa_onnx
