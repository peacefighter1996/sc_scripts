#include "timer_footer_renderer.h"

#include "timer_display.h"

#include <imgui.h>

void TimerFooterRenderer::render(const TimerDisplay& timer_display, int viewport_height) const {
    ImGui::SetNextWindowPos(ImVec2(10.0f, static_cast<float>(viewport_height - 70)));
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##footer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Sleep %%: %.1f%% | Loop: %.2f ms", timer_display.sleep_percent().average(), timer_display.loop_time_ms().average());
    ImGui::Text(
        "Work: %.2f ms | Render: %.2f ms | OCR Poll: %.2f ms",
        timer_display.work_time_ms().average(),
        timer_display.render_time_ms().average(),
        timer_display.ocr_poll_time_ms().average());
    ImGui::Text("OCR Task Last: %.2f ms | OCR Task Avg: %.2f ms", timer_display.ocr_task_time_ms().last(), timer_display.ocr_task_time_ms().average());
    ImGui::End();
}
