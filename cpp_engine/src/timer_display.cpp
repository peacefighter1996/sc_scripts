#include "timer_display.h"

void TimerDisplay::record_sleep(double sleep_percent) {
    sleep_percent_.add_sample(sleep_percent);
}

void TimerDisplay::record_frame(double loop_time_ms, double work_time_ms, double render_time_ms, double ocr_poll_time_ms) {
    loop_time_ms_.add_sample(loop_time_ms);
    work_time_ms_.add_sample(work_time_ms);
    render_time_ms_.add_sample(render_time_ms);
    ocr_poll_time_ms_.add_sample(ocr_poll_time_ms);
}

void TimerDisplay::record_ocr_task(double task_time_ms) {
    ocr_task_time_ms_.add_sample(task_time_ms);
}

TimerMetric& TimerDisplay::sleep_percent() {
    return sleep_percent_;
}

TimerMetric& TimerDisplay::loop_time_ms() {
    return loop_time_ms_;
}

TimerMetric& TimerDisplay::work_time_ms() {
    return work_time_ms_;
}

TimerMetric& TimerDisplay::render_time_ms() {
    return render_time_ms_;
}

TimerMetric& TimerDisplay::ocr_poll_time_ms() {
    return ocr_poll_time_ms_;
}

TimerMetric& TimerDisplay::ocr_task_time_ms() {
    return ocr_task_time_ms_;
}
