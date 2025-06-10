#ifndef SHERPA_ONNX_TTS_SYNTHESIZER_PROCESSOR_H_
#define SHERPA_ONNX_TTS_SYNTHESIZER_PROCESSOR_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "sherpa-onnx/csrc/macros.h"
#include "sherpa-onnx/csrc/offline-tts.h"

namespace sherpa_onnx {

class ThreadPool {
 public:
  ThreadPool(size_t num_threads) : stop_(false) {
    SHERPA_ONNX_LOGE("ThreadPool: Creating %zu worker threads.", num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
      workers_.emplace_back([this, i] {
        SHERPA_ONNX_LOGE("ThreadPool: Worker thread %zu started.", i);
        for (;;) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) {
              SHERPA_ONNX_LOGE("ThreadPool: Worker thread %zu exiting.", i);
              return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
          }
          try {
            task();
          } catch (const std::exception& e) {
            SHERPA_ONNX_LOGE("ThreadPool: Task exception in thread %zu: %s", i, e.what());
          } catch (...) {
            SHERPA_ONNX_LOGE("ThreadPool: Unknown task exception in thread %zu", i);
          }
        }
      });
    }
    SHERPA_ONNX_LOGE("ThreadPool: All worker threads created.");
  }

  ~ThreadPool() {
    SHERPA_ONNX_LOGE("ThreadPool: Destructor called. Stopping threads.");
    stop();
    SHERPA_ONNX_LOGE("ThreadPool: Destructor finished.");
  }

  void stop() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (stop_) return; // Already stopped
      stop_ = true;
    }
    condition_.notify_all();
    
    for (size_t i = 0; i < workers_.size(); ++i) {
      if (workers_[i].joinable()) {
        SHERPA_ONNX_LOGE("ThreadPool: Joining worker thread %zu...", i);
        try {
          workers_[i].join();
          SHERPA_ONNX_LOGE("ThreadPool: Worker thread %zu joined successfully.", i);
        } catch (const std::exception& e) {
          SHERPA_ONNX_LOGE("ThreadPool: Exception joining thread %zu: %s", i, e.what());
        }
      }
    }
  }

  template <class F, class... Args>
  auto enqueue(F &&f, Args &&...args)
      -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (stop_) {
        throw std::runtime_error("enqueue on stopped ThreadPool");
      }
      tasks_.emplace([task]() { (*task)(); });
    }
    condition_.notify_one();
    return res;
  }

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_;
};

class SynthesizerProcessor {
 public:
  using ProcessCallback = std::function<int32_t(const float *, int32_t, float)>;

  SynthesizerProcessor(
      std::function<GeneratedAudio(const std::string &, std::vector<float> &,
                                   const std::vector<int64_t> &,
                                   const std::vector<int64_t> &, int32_t,
                                   float)> process_fn,
      size_t num_synthesis_threads = std::thread::hardware_concurrency(),
      size_t chunk_size = 4)
      : process_fn_(process_fn),
        thread_pool_(num_synthesis_threads > 0 ? num_synthesis_threads : 1),
        chunk_size_(chunk_size),
        stop_requested_(false) {
    SHERPA_ONNX_LOGE(
        "SynthesizerProcessor initialized with %zu synthesis threads, chunk size %zu.",
        num_synthesis_threads > 0 ? num_synthesis_threads : 1, chunk_size);
  }

  ~SynthesizerProcessor() {
    StopProcessing();
  }

  GeneratedAudio ProcessAllSentences(
      const std::vector<std::string> &sentences,
      std::vector<std::vector<float>> &ja_berts,
      const std::vector<std::vector<int64_t>> &phone_ids,
      const std::vector<std::vector<int64_t>> &tones, 
      int32_t sid, 
      float speed,
      ProcessCallback callback = nullptr);

  void StopProcessing();

 private:
  std::function<GeneratedAudio(const std::string &, std::vector<float> &,
                               const std::vector<int64_t> &,
                               const std::vector<int64_t> &, int32_t, float)> process_fn_;

  ThreadPool thread_pool_;
  size_t chunk_size_;
  std::atomic<bool> stop_requested_;

  // 청크 단위로 처리하는 내부 함수
  void SubmitSentence(
      const std::vector<std::string> &sentences,
      std::vector<std::vector<float>> &ja_berts,
      const std::vector<std::vector<int64_t>> &phone_ids,
      const std::vector<std::vector<int64_t>> &tones,
      size_t sentence_idx,
      int32_t sid, 
      float speed,
      std::vector<std::future<GeneratedAudio>> &futures);
};

}  // namespace sherpa_onnx
#endif  // SHERPA_ONNX_TTS_SYNTHESIZER_PROCESSOR_H_