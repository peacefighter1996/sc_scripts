#pragma once

#include "timer_metric.h"

class TimerDisplay {
public:
    void record_sleep(double sleep_percent);
    void record_frame(double loop_time_ms, double work_time_ms, double render_time_ms, double ocr_poll_time_ms);
    void record_ocr_task(double task_time_ms);

    TimerMetric& sleep_percent();
    TimerMetric& loop_time_ms();
    TimerMetric& work_time_ms();
    TimerMetric& render_time_ms();
    TimerMetric& ocr_poll_time_ms();
    TimerMetric& ocr_task_time_ms();

private:
    TimerMetric sleep_percent_;
    TimerMetric loop_time_ms_;
    TimerMetric work_time_ms_;
    TimerMetric render_time_ms_;
    TimerMetric ocr_poll_time_ms_;
    TimerMetric ocr_task_time_ms_;
};
