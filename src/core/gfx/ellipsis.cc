#include "core/gfx/ellipsis.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace minicraft {
namespace gfx {

DotUpdater::DotUpdater(int32_t count_per_cycle)
    : count_per_cycle_(count_per_cycle) {}

void DotUpdater::SetIntervalCount(int32_t num_intervals) {
  interval_count_ = std::max(1, num_intervals);
  count_per_interval_ = std::max(
      1, static_cast<int32_t>(std::lround(
             static_cast<double>(count_per_cycle_) / interval_count_)));
}

void DotUpdater::IncInterval(int32_t amt) {
  if (ellipsis_) {
    for (int32_t i = cur_interval_ + 1; i <= cur_interval_ + amt; ++i) {
      ellipsis_->NextInterval(i % interval_count_);
    }
  }
  cur_interval_ = (cur_interval_ + amt) % interval_count_;
}

void DotUpdater::IncCounter(int32_t amt) {
  if (amt <= 0) return;
  counter_ += amt;
  int32_t intervals = counter_ / count_per_interval_;
  if (intervals > 0) {
    IncInterval(intervals);
    counter_ -= intervals * count_per_interval_;
  }
}

void DotUpdater::Update() {
  if (!started_) {
    Start();
  }
}

TimeUpdater::TimeUpdater(const platform::IClock* clock,
                         int32_t millis_per_cycle)
    : DotUpdater(millis_per_cycle), clock_(clock) {}

void TimeUpdater::Start() {
  DotUpdater::Start();
  last_time_ns_ = clock_ ? static_cast<uint64_t>(clock_->NowNanoseconds()) : 0;
}

void TimeUpdater::Update() {
  DotUpdater::Update();
  if (!clock_) {
    IncCounter(16);
    return;
  }
  uint64_t now = static_cast<uint64_t>(clock_->NowNanoseconds());
  int32_t diff_millis = static_cast<int32_t>((now - last_time_ns_) / 1000000ULL);
  last_time_ns_ = now;
  IncCounter(diff_millis);
}

CallUpdater::CallUpdater(int32_t calls_per_cycle)
    : DotUpdater(calls_per_cycle) {}

void CallUpdater::Update() {
  DotUpdater::Update();
  IncCounter(1);
}

TickUpdater::TickUpdater(const int32_t* tick_source, int32_t ticks_per_cycle)
    : DotUpdater(ticks_per_cycle), tick_source_(tick_source) {}

void TickUpdater::Start() {
  DotUpdater::Start();
  last_tick_ = tick_source_ ? *tick_source_ : 0;
}

void TickUpdater::Update() {
  DotUpdater::Update();
  if (!tick_source_) {
    IncCounter(1);
    return;
  }
  int32_t new_tick = *tick_source_;
  int32_t ticks_passed = new_tick - last_tick_;
  last_tick_ = new_tick;
  IncCounter(ticks_passed);
}

Ellipsis::Ellipsis(std::unique_ptr<DotUpdater> update_method,
                   int32_t interval_count)
    : update_method_(std::move(update_method)) {
  update_method_->SetIntervalCount(interval_count);
}

std::string Ellipsis::UpdateAndGet() {
  update_method_->Update();
  return Get();
}

SequentialEllipsis::SequentialEllipsis()
    : SequentialEllipsis(std::make_unique<CallUpdater>(60 * 2 / 3)) {}

SequentialEllipsis::SequentialEllipsis(std::unique_ptr<DotUpdater> updater)
    : Ellipsis(std::move(updater), 3) {}

std::string SequentialEllipsis::Get() const {
  std::string dots;
  int32_t e_pos = GetInterval();
  for (int32_t i = 0; i < GetIntervalCount(); ++i) {
    dots.push_back(e_pos == i ? '.' : ' ');
  }
  return dots;
}

SmoothEllipsis::SmoothEllipsis(const platform::IClock* clock)
    : SmoothEllipsis(std::make_unique<TimeUpdater>(clock)) {}

SmoothEllipsis::SmoothEllipsis(std::unique_ptr<DotUpdater> updater)
    : Ellipsis(std::move(updater), 6) {
  Updater()->SetEllipsis(this);
}

std::string SmoothEllipsis::Get() const {
  return std::string(dots_, 3);
}

void SmoothEllipsis::NextInterval(int32_t interval) {
  int32_t epos = interval % 3;
  dots_[epos] = (interval < GetIntervalCount() / 2) ? '.' : ' ';
}

}  // namespace gfx
}  // namespace minicraft
