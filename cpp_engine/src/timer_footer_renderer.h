#pragma once

class TimerDisplay;

class TimerFooterRenderer {
public:
    void render(TimerDisplay& timer_display, int viewport_height) const;
};
