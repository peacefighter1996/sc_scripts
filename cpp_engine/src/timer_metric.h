#pragma once

#include <vector>

class TimerMetric {
public:
    explicit TimerMetric(int sample_window = 30);

    void add_sample(double value);
    [[nodiscard]] double last() const;
    [[nodiscard]] double average() const;
    [[nodiscard]] int sample_count() const;

private:
    std::vector<double> samples_;
    int index_{0};
    int count_{0};
    double last_value_{0.0};
};
