#include "timer_metric.h"

#include <algorithm>

TimerMetric::TimerMetric(int sample_window)
    : samples_(std::max(1, sample_window), 0.0) {
}

void TimerMetric::add_sample(double value) {
    last_value_ = value;
    samples_[static_cast<size_t>(index_)] = value;
    index_ = (index_ + 1) % static_cast<int>(samples_.size());
    if (count_ < static_cast<int>(samples_.size())) {
        ++count_;
    }
}

double TimerMetric::last() const {
    return last_value_;
}

double TimerMetric::average() const {
    if (count_ <= 0) {
        return 0.0;
    }

    double total = 0.0;
    for (int i = 0; i < count_; ++i) {
        total += samples_[static_cast<size_t>(i)];
    }
    return total / static_cast<double>(count_);
}

int TimerMetric::sample_count() const {
    return count_;
}
