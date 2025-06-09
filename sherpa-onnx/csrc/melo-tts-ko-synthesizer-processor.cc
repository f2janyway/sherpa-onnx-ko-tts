#include "sherpa-onnx/csrc/melo-tts-ko-synthesizer-processor.h"  // Include the header file

#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/offline-tts.h"

// Assume GeneratedAudio is defined globally or in a common header
// struct GeneratedAudio {
//     int32_t sample_rate;
//     std::vector<float> samples;
// };

namespace sherpa_onnx {
// Implementation of ProcessAllSentences
GeneratedAudio SynthesizerProcessor::ProcessAllSentences(
    const std::vector<std::string> &sentences,
    std::vector<std::vector<float>> &ja_berts,
    const std::vector<std::vector<int64_t>> &phone_ids,
    const std::vector<std::vector<int64_t>> &tones, int32_t sid, float speed,
    ProcessCallback callback) {
  if (sentences.empty()) {
    SHERPA_ONNX_LOGE("No sentences to process.");
    return {};
  }

  stop_requested_ = false;  // Reset stop request

  GeneratedAudio final_audio_output;   // To store the concatenated final audio
  final_audio_output.sample_rate = 0;  // Will be set by the first audio chunk

  int32_t total_sentences = static_cast<int32_t>(sentences.size());
  SHERPA_ONNX_LOGE("Starting processing %d sentences concurrently...",
                   total_sentences);

  // --- Start Consumer Thread ---
  if (consumer_thread_running_.load()) {
    StopProcessing();  // Ensure previous run is stopped before starting a new
                       // one
  }
  consumer_thread_running_ = true;
  consumer_thread_ = std::thread(&SynthesizerProcessor::ConsumerLoop, this,
                                 callback, total_sentences);
  // ConsumerLoop will now push to final_audio_output via a local queue
  // instead of calling callback directly. Let's rethink this.
  // The consumer loop should call the callback *and* accumulate the
  // final_audio_output. Or, more cleanly: the consumer loop just calls the
  // callback, and the caller accumulates.

  // Let's modify the ConsumerLoop signature and behavior slightly for
  // accumulation For now, let's keep the `ans` accumulation in the main thread
  // for simplicity and just use the consumer thread for the callback. Or, we
  // can make `final_audio_output` a member and have the consumer accumulate
  // there. For clarity, let's have `ProcessAllSentences` accumulate its own
  // result, and the `callback` is handled by the consumer.

  // The consumer thread will manage calling the user-provided callback
  // and stopping early if requested.
  // We need a way for the consumer to indicate that it stopped.
  // For simplicity, let's have consumer thread accumulate into a member
  // `final_audio_output_` which this function will then return. This simplifies
  // callback handling.

  // New approach: the consumer thread will manage accumulation AND callback.
  // This `ProcessAllSentences` function will only manage enqueueing tasks.
  // This is more complex, but better for real-time.

  // Let's go back to the idea: this function enqueues tasks, then waits for all
  // futures to complete, accumulates, and calls callbacks *in this thread*.
  // This is simpler for C++17 and `std::future`.
  // If real-time playback (i.e. callback before all audio is done) is crucial,
  // the previous idea of Producer-Consumer with a separate audio playback
  // thread is needed.

  // Given the prompt: "가장 늦게 생성되는 시간 기준으로 callback을 실행하면
  // 4개의 뭉치의 소리가 나오고 그 동안에 나머지 문장들을 위와 같은 방식으로
  // 돌리면서 실행하면 끊기지 않고 실행할수 있을거 같은데 어떤가?" This implies
  // a "batch" of futures, then collect and callback. This means the
  // `ProcessAllSentences` itself will accumulate.

  std::vector<std::future<GeneratedAudio>> futures;
  futures.reserve(total_sentences);

  for (int32_t b = 0; b != total_sentences; ++b) {
    if (stop_requested_.load()) {  // Check for external stop request
      SHERPA_ONNX_LOGE("Processing stopped by request.");
      break;
    }

    // Capture by value for lambda to ensure data lives until future is ready
    std::string text_copy = sentences[b];
    std::vector<float> ja_bert_vec_copy = ja_berts[b];
    std::vector<int64_t> token_ids_copy = phone_ids[b];
    std::vector<int64_t> tones_vec_copy = tones[b];

    futures.push_back(
        thread_pool_.enqueue(process_fn_,  // The actual Process function
                             text_copy, ja_bert_vec_copy, token_ids_copy,
                             tones_vec_copy, sid, speed));
    SHERPA_ONNX_LOGE("Enqueued sentence %d for processing.", b);
  }

  // Now, collect the results in order and call the callback
  // This is the "순서대로 쌓고" 부분
  int32_t processed_count = 0;
  for (int32_t b = 0; b < futures.size(); ++b) {
    if (stop_requested_
            .load()) {  // Check for external stop request while collecting
      SHERPA_ONNX_LOGE("Collection stopped by request.");
      break;
    }

    // Get the result. This will block until the future is ready.
    // This is the "가장 늦게 생성되는 시간 기준으로"의 한계점.
    // 개별 문장들이 병렬로 생성되더라도, 여기서 순서대로 `get()`을 호출하면
    // 순서에 맞게 대기하게 됩니다. 하지만 전체 시간은 단축될 수 있습니다.
    try {
      auto audio = futures[b].get();
      processed_count++;

      if (final_audio_output.sample_rate == 0) {
        final_audio_output.sample_rate = audio.sample_rate;
      }
      final_audio_output.samples.insert(final_audio_output.samples.end(),
                                        audio.samples.begin(),
                                        audio.samples.end());

      // Callback after each sentence is processed and accumulated
      if (callback) {
        // If the callback returns 0, stop further processing
        int32_t continue_processing = callback(
            audio.samples.data(), static_cast<int32_t>(audio.samples.size()),
            static_cast<float>(processed_count) / total_sentences);
        if (continue_processing == 0) {
          SHERPA_ONNX_LOGE("Callback requested stop.");
          stop_requested_ = true;  // Signal to stop any pending tasks
          break;
        }
      }
      SHERPA_ONNX_LOGE("Processed and accumulated sentence %d/%d.", b + 1,
                       total_sentences);

    } catch (const std::exception &e) {
      SHERPA_ONNX_LOGE("Error processing sentence %d: %s", b, e.what());
      // Decide whether to continue or stop on error
      stop_requested_ = true;  // 오류 발생 시 다른 작업도 중단
      break;
    }
  }

  SHERPA_ONNX_LOGE("Finished enqueuing and collecting all sentences.");
  return final_audio_output;
}

void SynthesizerProcessor::StopProcessing() {
  stop_requested_ = true;  // Signal to stop
  // Note: If tasks are already enqueued in the thread pool, they might still
  // run unless the process_fn_ itself checks `stop_requested_`. The
  // `future.get()` loop above also checks `stop_requested_`.
}

// ConsumerLoop is not used in this specific implementation for real-time
// playback as the `ProcessAllSentences` waits for futures and accumulates in
// its own thread. This is a simpler "batch of futures" approach. For true
// streaming (Producer-Consumer), `ConsumerLoop` would be crucial.
void SynthesizerProcessor::ConsumerLoop(ProcessCallback callback,
                                        int32_t total_sentences_count) {
  // This loop would typically fetch from audio_queue_ and call callback.
  // However, the current `ProcessAllSentences` design gets futures and calls
  // callback directly. So this ConsumerLoop is commented out/not used in the
  // current structure. If you want true async playback, we need to revert to a
  // Producer-Consumer model.
  SHERPA_ONNX_LOGE(
      "ConsumerLoop started (though not actively used in this sync-collection "
      "approach).");
  consumer_thread_running_ = false;  // Mark as stopped if it ever runs
}
}  // namespace sherpa_onnx