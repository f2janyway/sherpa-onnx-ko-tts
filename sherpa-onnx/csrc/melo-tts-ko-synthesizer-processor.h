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

// Assume GeneratedAudio is defined elsewhere in your project
// struct GeneratedAudio {
//     int32_t sample_rate;
//     std::vector<float> samples;
// };

// Assume Process function signature and its dependencies (e.g., Ort::Value,
// sid, speed) are defined elsewhere or passed into this class. For example:
// GeneratedAudio Process(const std::string& text,
//                        const std::vector<float>& ja_bert_vec,
//                        const std::vector<int64_t>& token_ids,
//                        const std::vector<int64_t>& tones_vec,
//                        int64_t sid, float speed);

// --- ThreadPool implementation (C++17 compatible) ---
// This is a minimal thread pool. For production, consider more robust
// libraries.
class ThreadPool {
 public:
  ThreadPool(size_t num_threads) : stop_(false) {
        SHERPA_ONNX_LOGE("ThreadPool: Creating %zu worker threads.", num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this, i] { // Capture i for logging
                SHERPA_ONNX_LOGE("ThreadPool: Worker thread %zu started.", i);
                for (;;) {
                     std::function<void()> task;
                     {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
            // Wait until there's a task or the pool is stopped
                        condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            // If stopped and no more tasks, exit
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                     }
                     task();
                }
                SHERPA_ONNX_LOGE("ThreadPool: Worker thread %zu stopped.", i);
            });
        }
        SHERPA_ONNX_LOGE("ThreadPool: All worker threads enqueued.");
    }

    ~ThreadPool() {
        SHERPA_ONNX_LOGE("ThreadPool: Destructor called. Stopping threads.");
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        int i = 0;
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                SHERPA_ONNX_LOGE("ThreadPool: Attempting to join worker thread %d...", i);
                try {
                    worker.join();
                    SHERPA_ONNX_LOGE("ThreadPool: Worker thread %d joined successfully.", i);
                } catch (const std::exception& e) {
                    SHERPA_ONNX_LOGE("ThreadPool: Exception while joining worker thread %d: %s", i, e.what());
                } catch (...) {
                    SHERPA_ONNX_LOGE("ThreadPool: Unknown exception while joining worker thread %d.", i);
                }
            } else {
                SHERPA_ONNX_LOGE("ThreadPool: Worker thread %d not joinable, skipping.", i);
            }
            i++;
        }
        SHERPA_ONNX_LOGE("ThreadPool: Destructor finished.");
    }

  // Add a new task to the pool
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
    condition_.notify_one();  // Notify one waiting worker
    return res;
  }

  // Stop all threads and join them
//   ~ThreadPool() {
//     SHERPA_ONNX_LOGE("ThreadPool destructor called.");
//     {
//       std::unique_lock<std::mutex> lock(queue_mutex_);
//       stop_ = true;
//     }
//     condition_.notify_all();  // Notify all waiting workers to stop
//     int i = 0;
//     for (std::thread &worker : workers_) {
//       if (worker.joinable()) {
//         SHERPA_ONNX_LOGE("Joining worker thread %d...", i);
//         try {
//           worker.join();
//           SHERPA_ONNX_LOGE("Worker thread %d joined successfully.", i);
//         } catch (const std::exception &e) {
//           SHERPA_ONNX_LOGE("Exception while joining worker thread %d: %s", i,
//                            e.what());
//         } catch (...) {
//           SHERPA_ONNX_LOGE("Unknown exception while joining worker thread %d.",
//                            i);
//         }
//       } else {
//         SHERPA_ONNX_LOGE("Worker thread %d is not joinable, skipping.", i);
//       }
//       i++;
//     }
//     SHERPA_ONNX_LOGE("ThreadPool destructor finished.");
//   }

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_;
};

// --- Thread-safe Queue implementation for GeneratedAudio ---
// This queue will hold the synthesized audio data
class ThreadSafeAudioQueue {
 public:
  void push(GeneratedAudio &&audio) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      queue_.push(std::move(audio));
    }
    cond_var_.notify_one();  // Notify consumer
  }

  bool pop(GeneratedAudio &audio) {
    std::unique_lock<std::mutex> lock(mutex_);
    // Wait until queue is not empty or shutdown is requested
    cond_var_.wait(lock, [this] { return !queue_.empty() || shutdown_; });

    if (queue_.empty() && shutdown_) {
      return false;  // Queue empty and shutting down
    }
    audio = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  bool empty() {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.empty();
  }

  void shutdown() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      shutdown_ = true;
    }
    cond_var_.notify_all();  // Wake up all waiting consumers
  }

 private:
  std::queue<GeneratedAudio> queue_;
  std::mutex mutex_;
  std::condition_variable cond_var_;
  bool shutdown_ = false;
};

// --- SynthesizerProcessor Class ---
class SynthesizerProcessor {
 public:
  // Callback type alias
  using ProcessCallback = std::function<int32_t(const float *, int32_t, float)>;

  // Constructor: Takes a reference to the Process function and number of
  // threads The ProcessFn should be a callable that matches the signature:
  // GeneratedAudio(const std::string&, const std::vector<float>&, const
  // std::vector<int64_t>&,
  //                const std::vector<int64_t>&, int64_t, float)
  SynthesizerProcessor(
      std::function<GeneratedAudio(const std::string &, std::vector<float> &,
                                   const std::vector<int64_t> &,
                                   const std::vector<int64_t> &, int32_t,
                                   float)>
          process_fn,
      size_t num_synthesis_threads = std::thread::hardware_concurrency())
      : process_fn_(process_fn),
        thread_pool_(num_synthesis_threads),
        consumer_thread_running_(false) {
    if (num_synthesis_threads == 0) {
      num_synthesis_threads = 1;  // At least one thread
    }
    SHERPA_ONNX_LOGE(
        "SynthesizerProcessor initialized with %zu synthesis threads.",
        num_synthesis_threads);
  }

  // Destructor: Ensures consumer thread is joined and pool is shut down
  ~SynthesizerProcessor() {
    StopProcessing();  // Ensure everything is cleanly shut down
  }

  // Main processing function
  GeneratedAudio ProcessAllSentences(
      const std::vector<std::string> &sentences,
      std::vector<std::vector<float>> &ja_berts,
      const std::vector<std::vector<int64_t>> &phone_ids,
      const std::vector<std::vector<int64_t>> &tones, int32_t sid, float speed,
      ProcessCallback callback = nullptr);

  // Stop ongoing processing
  void StopProcessing();

 private:
  // Function pointer/lambda for the actual TTS processing (e.g., calling ONNX
  // Runtime)
  std::function<GeneratedAudio(const std::string &, std::vector<float> &,
                               const std::vector<int64_t> &,
                               const std::vector<int64_t> &, int64_t, float)>
      process_fn_;

  ThreadPool thread_pool_;
  ThreadSafeAudioQueue audio_queue_;
  std::thread consumer_thread_;
  std::atomic<bool> consumer_thread_running_;
  std::atomic<bool> stop_requested_;

  // Consumer thread function
  void ConsumerLoop(ProcessCallback callback, int32_t total_sentences_count);
};

}  // namespace sherpa_onnx
#endif  // SHERPA_ONNX_TTS_SYNTHESIZER_PROCESSOR_H_