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
  
  SHERPA_ONNX_LOGE("Starting processing %d sentences in chunks of %zu...", 
                   total_sentences, chunk_size_);

  // 청크 단위로 처리
  for (size_t chunk_start = 0; chunk_start < sentences.size(); chunk_start += chunk_size_) {
    if (stop_requested_.load()) {
      SHERPA_ONNX_LOGE("Processing stopped by request.");
      break;
    }

    size_t chunk_end = std::min(chunk_start + chunk_size_, sentences.size());
    SHERPA_ONNX_LOGE("Processing chunk [%zu, %zu)", chunk_start, chunk_end);

    try {
      GeneratedAudio chunk_audio = ProcessChunk(
          sentences, ja_berts, phone_ids, tones,
          chunk_start, chunk_end, sid, speed, callback,
          processed_count, total_sentences);

      // 첫 번째 청크에서 sample_rate 설정
      if (final_audio_output.sample_rate == 0 && chunk_audio.sample_rate > 0) {
        final_audio_output.sample_rate = chunk_audio.sample_rate;
      }

      // 오디오 데이터 병합
      final_audio_output.samples.insert(
          final_audio_output.samples.end(),
          chunk_audio.samples.begin(),
          chunk_audio.samples.end());

      SHERPA_ONNX_LOGE("Completed chunk [%zu, %zu), total processed: %d/%d",
                       chunk_start, chunk_end, processed_count, total_sentences);

    } catch (const std::exception &e) {
      SHERPA_ONNX_LOGE("Error processing chunk [%zu, %zu): %s", 
                       chunk_start, chunk_end, e.what());
      stop_requested_ = true;
      break;
    }
  }

  SHERPA_ONNX_LOGE("Finished processing all sentences. Total: %d", processed_count);
  return final_audio_output;
}

GeneratedAudio SynthesizerProcessor::ProcessChunk(
    const std::vector<std::string> &sentences,
    std::vector<std::vector<float>> &ja_berts,
    const std::vector<std::vector<int64_t>> &phone_ids,
    const std::vector<std::vector<int64_t>> &tones,
    size_t start_idx, 
    size_t end_idx,
    int32_t sid, 
    float speed,
    ProcessCallback callback,
    int32_t &processed_count,
    int32_t total_sentences) {

  GeneratedAudio chunk_audio;
  chunk_audio.sample_rate = 0;

  std::vector<std::future<GeneratedAudio>> futures;
  futures.reserve(end_idx - start_idx);

  // 청크 내의 모든 문장을 ThreadPool에 제출
  for (size_t i = start_idx; i < end_idx; ++i) {
    if (stop_requested_.load()) {
      SHERPA_ONNX_LOGE("Chunk processing stopped by request at sentence %zu.", i);
      break;
    }

    // 데이터 복사 (lambda 캡처를 위해)
    std::string text_copy = sentences[i];
    std::vector<float> ja_bert_copy = ja_berts[i];
    std::vector<int64_t> phone_ids_copy = phone_ids[i];
    std::vector<int64_t> tones_copy = tones[i];

    futures.push_back(
        thread_pool_.enqueue(
            process_fn_,
            text_copy, ja_bert_copy, phone_ids_copy, tones_copy, sid, speed));
    
    SHERPA_ONNX_LOGE("Enqueued sentence %zu for processing.", i);
  }

  // 순서대로 결과 수집 및 콜백 호출
  for (size_t i = 0; i < futures.size(); ++i) {
    if (stop_requested_.load()) {
      SHERPA_ONNX_LOGE("Result collection stopped by request at future %zu.", i);
      break;
    }

    try {
      // 순서대로 대기 (이게 핵심 - 순차적 처리 보장)
      auto audio = futures[i].get();
      processed_count++;

      // 첫 번째 오디오에서 sample_rate 설정
      if (chunk_audio.sample_rate == 0 && audio.sample_rate > 0) {
        chunk_audio.sample_rate = audio.sample_rate;
      }

      // 오디오 데이터 병합
      chunk_audio.samples.insert(
          chunk_audio.samples.end(),
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
          SHERPA_ONNX_LOGE("Callback requested stop at sentence %zu.", start_idx + i);
          stop_requested_ = true;
          break;
        }
      }

      SHERPA_ONNX_LOGE("Processed sentence %zu/%d (%.1f%%)", 
                       start_idx + i + 1, total_sentences, 
                       (float)processed_count * 100.0f / total_sentences);

    } catch (const std::exception &e) {
      SHERPA_ONNX_LOGE("Error getting result for sentence %zu: %s", 
                       start_idx + i, e.what());
      // 에러 발생 시 해당 청크 처리 중단
      stop_requested_ = true;
      break;
    }
  }

  return chunk_audio;
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