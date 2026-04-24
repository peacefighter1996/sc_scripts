#pragma once

class TimerDisplay;

class TimerFooterRenderer {
public:
    void render(const TimerDisplay& timer_display, int viewport_height) const;
};
