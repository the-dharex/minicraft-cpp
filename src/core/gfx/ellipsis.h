#ifndef MINICRAFT_CORE_GFX_ELLIPSIS_H_
#define MINICRAFT_CORE_GFX_ELLIPSIS_H_

#include <cstdint>
#include <memory>
#include <string>

#include "core/platform/time_clock.h"

namespace minicraft {
namespace gfx {

class Ellipsis;

class DotUpdater {
 public:
  explicit DotUpdater(int32_t count_per_cycle);
  virtual ~DotUpdater() = default;

  void SetEllipsis(Ellipsis* ellipsis) { ellipsis_ = ellipsis; }
  void SetIntervalCount(int32_t num_intervals);

  int32_t GetInterval() const { return cur_interval_; }
  int32_t GetIntervalCount() const { return interval_count_; }

  virtual void Update();

 protected:
  void IncCounter(int32_t amt);
  virtual void Start() { started_ = true; }

 private:
  void IncInterval(int32_t amt);

  int32_t count_per_cycle_;
  int32_t interval_count_ = 1;
  int32_t cur_interval_ = 0;
  int32_t count_per_interval_ = 1;
  int32_t counter_ = 0;
  Ellipsis* ellipsis_ = nullptr;
  bool started_ = false;
};

class TimeUpdater : public DotUpdater {
 public:
  explicit TimeUpdater(const platform::IClock* clock = nullptr,
                       int32_t millis_per_cycle = 750);
  void Update() override;

 protected:
  void Start() override;

 private:
  const platform::IClock* clock_;
  uint64_t last_time_ns_ = 0;
};

class CallUpdater : public DotUpdater {
 public:
  explicit CallUpdater(int32_t calls_per_cycle);
  void Update() override;
};

class TickUpdater : public DotUpdater {
 public:
  explicit TickUpdater(const int32_t* tick_source = nullptr,
                       int32_t ticks_per_cycle = 60);
  void Update() override;

 protected:
  void Start() override;

 private:
  const int32_t* tick_source_;
  int32_t last_tick_ = 0;
};

class Ellipsis {
 public:
  virtual ~Ellipsis() = default;

  std::string UpdateAndGet();
  virtual std::string Get() const = 0;
  virtual void NextInterval(int32_t interval) {}

 protected:
  Ellipsis(std::unique_ptr<DotUpdater> update_method, int32_t interval_count);

  int32_t GetInterval() const { return update_method_->GetInterval(); }
  int32_t GetIntervalCount() const {
    return update_method_->GetIntervalCount();
  }
  DotUpdater* Updater() { return update_method_.get(); }

 private:
  std::unique_ptr<DotUpdater> update_method_;
};

class SequentialEllipsis : public Ellipsis {
 public:
  SequentialEllipsis();
  explicit SequentialEllipsis(std::unique_ptr<DotUpdater> updater);

  std::string Get() const override;
};

class SmoothEllipsis : public Ellipsis {
 public:
  explicit SmoothEllipsis(const platform::IClock* clock = nullptr);
  explicit SmoothEllipsis(std::unique_ptr<DotUpdater> updater);

  std::string Get() const override;
  void NextInterval(int32_t interval) override;

 private:
  char dots_[3] = {' ', ' ', ' '};
};

}  // namespace gfx
}  // namespace minicraft

#endif  // MINICRAFT_CORE_GFX_ELLIPSIS_H_
