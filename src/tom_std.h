#pragma once

#include <semaphore>
#include <chrono>

namespace tom {
  struct Semaphore {
    std::binary_semaphore value{0};

    void wait() {
      value.acquire();
    }
    void signal() {
      value.release();
    }
  };

  struct Clock {
    uint64_t start_time_nanoseconds;
    uint64_t end_time_nanoseconds;
    uint64_t elapsed_time_nanoseconds;

    void start() {
      using namespace std::chrono;
      this->start_time_nanoseconds = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    }

    void restart() {
      start();
    };

    void stop() {
      using namespace std::chrono;
      this->end_time_nanoseconds     = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
      this->elapsed_time_nanoseconds = end_time_nanoseconds - start_time_nanoseconds;
    }

    uint64_t get_elapsed_time_nanoseconds() const {
      return elapsed_time_nanoseconds;
    }

    uint64_t get_elapsed_time_milliseconds() const {
      return elapsed_time_nanoseconds / 1000000;
    }

    float get_elapsed_time_seconds() const {
      return static_cast<float>(elapsed_time_nanoseconds) / 1000000000.0f;
    }
  };
}
