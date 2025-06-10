#include "sherpa-onnx/csrc/melo-tts-ko-synthesizer-processor.h"
#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/offline-tts.h"

namespace sherpa_onnx {

GeneratedAudio SynthesizerProcessor::ProcessAllSentences(
    const std::vector<std::string> &sentences,
    std::vector<std::vector<float>> &ja_berts,
    const std::vector<std::vector<int64_t>> &phone_ids,
    const std::vector<std::vector<int64_t>> &tones, 
    int32_t sid, 
    float speed,
    ProcessCallback callback) {
  
  if (sentences.empty()) {
    SHERPA_ONNX_LOGE("No sentences to process.");
    return {};
  }

  stop_requested_ = false;
  GeneratedAudio final_audio_output;
  final_audio_output.sample_rate = 0;

  int32_t total_sentences = static_cast<int32_t>(sentences.size());
  int32_t processed_count = 0;
  
  SHERPA_ONNX_LOGE("Starting pipelined processing of %d sentences with chunk size %zu...", 
                   total_sentences, chunk_size_);

  // 파이프라인 방식: 모든 futures를 관리하되, 청크 단위로 제출
  std::vector<std::future<GeneratedAudio>> all_futures;
  all_futures.reserve(total_sentences);
  
  size_t next_to_submit = 0;  // 다음에 제출할 문장 인덱스
  size_t next_to_collect = 0; // 다음에 수집할 문장 인덱스
  
  // 첫 번째 청크를 미리 제출
  size_t initial_chunk_end = std::min(chunk_size_, sentences.size());
  for (size_t i = 0; i < initial_chunk_end; ++i) {
    SubmitSentence(sentences, ja_berts, phone_ids, tones, i, sid, speed, all_futures);
  }
  next_to_submit = initial_chunk_end;
  
  SHERPA_ONNX_LOGE("Initial chunk [0, %zu) submitted to ThreadPool", initial_chunk_end);

  // 파이프라인 처리: 결과 수집과 동시에 새로운 작업 제출
  while (next_to_collect < sentences.size()) {
    if (stop_requested_.load()) {
      SHERPA_ONNX_LOGE("Processing stopped by request at sentence %zu.", next_to_collect);
      break;
    }

    try {
      // 순서대로 결과 수집 (이 부분이 순차성 보장)
      auto audio = all_futures[next_to_collect].get();
      processed_count++;

      // 첫 번째 오디오에서 sample_rate 설정
      if (final_audio_output.sample_rate == 0 && audio.sample_rate > 0) {
        final_audio_output.sample_rate = audio.sample_rate;
      }

      // 오디오 데이터 병합
      final_audio_output.samples.insert(
          final_audio_output.samples.end(),
          audio.samples.begin(),
          audio.samples.end());

      // 콜백 호출 (순서 보장됨)
      if (callback) {
        float progress = static_cast<float>(processed_count) / total_sentences;
        int32_t continue_processing = callback(
            audio.samples.data(), 
            static_cast<int32_t>(audio.samples.size()),
            progress);
        
        if (continue_processing == 0) {
          SHERPA_ONNX_LOGE("Callback requested stop at sentence %zu.", next_to_collect);
          stop_requested_ = true;
          break;
        }
      }

      SHERPA_ONNX_LOGE("Processed sentence %zu/%d (%.1f%%)", 
                       next_to_collect + 1, total_sentences, 
                       (float)processed_count * 100.0f / total_sentences);

      next_to_collect++;

      // 결과를 하나 수집할 때마다 새로운 문장을 제출 (파이프라인 유지)
      if (next_to_submit < sentences.size()) {
        SubmitSentence(sentences, ja_berts, phone_ids, tones, next_to_submit, sid, speed, all_futures);
        SHERPA_ONNX_LOGE("Submitted sentence %zu to maintain pipeline", next_to_submit);
        next_to_submit++;
      }

    } catch (const std::exception &e) {
      SHERPA_ONNX_LOGE("Error getting result for sentence %zu: %s", 
                       next_to_collect, e.what());
      stop_requested_ = true;
      break;
    }
  }

  SHERPA_ONNX_LOGE("Finished pipelined processing. Total processed: %d/%d", 
                   processed_count, total_sentences);
  return final_audio_output;
}

void SynthesizerProcessor::SubmitSentence(
    const std::vector<std::string> &sentences,
    std::vector<std::vector<float>> &ja_berts,
    const std::vector<std::vector<int64_t>> &phone_ids,
    const std::vector<std::vector<int64_t>> &tones,
    size_t sentence_idx,
    int32_t sid, 
    float speed,
    std::vector<std::future<GeneratedAudio>> &futures) {

  // 데이터 복사 (lambda 캡처를 위해)
  std::string text_copy = sentences[sentence_idx];
  std::vector<float> ja_bert_copy = ja_berts[sentence_idx];
  std::vector<int64_t> phone_ids_copy = phone_ids[sentence_idx];
  std::vector<int64_t> tones_copy = tones[sentence_idx];

  futures.push_back(
      thread_pool_.enqueue(
          process_fn_,
          text_copy, ja_bert_copy, phone_ids_copy, tones_copy, sid, speed));
}

void SynthesizerProcessor::StopProcessing() {
  SHERPA_ONNX_LOGE("StopProcessing called.");
  stop_requested_ = true;
  
  // ThreadPool 안전하게 종료
  try {
    thread_pool_.stop();
    SHERPA_ONNX_LOGE("ThreadPool stopped successfully.");
  } catch (const std::exception &e) {
    SHERPA_ONNX_LOGE("Error stopping ThreadPool: %s", e.what());
  } catch (...) {
    SHERPA_ONNX_LOGE("Unknown error stopping ThreadPool.");
  }
}

}  // namespace sherpa_onnx