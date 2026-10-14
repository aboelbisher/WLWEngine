#if WLW_DEBUG_UI
#include "wlw/debug/console_window.h"

#include <string>

namespace wlw::debug {
namespace {

ImVec4 LineColor(const LogRecord& record) {
  if (record.level >= LogLevel::Error) return ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
  if (record.level == LogLevel::Warning) return ImVec4(1.0f, 0.85f, 0.4f, 1.0f);
  if (record.level <= LogLevel::Debug) return ImVec4(0.6f, 0.6f, 0.65f, 1.0f);
  if (record.message.rfind("> ", 0) == 0) return ImVec4(0.5f, 0.85f, 1.0f, 1.0f);  // what was typed
  return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

}  // namespace

void ConsoleWindow::Draw(bool keyboard) {
  ImGui::SetNextWindowSize(ImVec2(760.0f, 420.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(60.0f, 60.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Console", &open)) {
    ImGui::End();
    return;
  }

  // Toolbar: clear, the lowest level shown, a text filter, follow the newest line.
  if (ImGui::SmallButton("Clear")) model_.Clear();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.0f);
  ImGui::Combo("##level", &min_level_, "verbose\0debug\0info\0warning\0error\0");
  ImGui::SameLine();
  filter_.Draw("Filter", 200.0f);
  ImGui::SameLine();
  ImGui::Checkbox("Follow", &follow_);
  ImGui::Separator();

  // The lines, leaving room for the input line.
  const float footer = keyboard ? ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing()
                                : ImGui::GetTextLineHeightWithSpacing() * 1.5f;
  if (ImGui::BeginChild("##lines", ImVec2(0.0f, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
    const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
    model_.ForEachLine([this](const LogRecord& record) {
      if (static_cast<int>(record.level) < min_level_) return;
      const std::string line = FormatLogRecord(record);
      if (!filter_.PassFilter(line.c_str())) return;
      ImGui::PushStyleColor(ImGuiCol_Text, LineColor(record));
      ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
      ImGui::PopStyleColor();
    });
    if (follow_ && at_bottom) ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();

  if (!keyboard) {
    ImGui::TextDisabled("No keyboard: change cvars in the CVars panel, or with adb: --es wlw \"name=value;...\"");
    ImGui::End();
    return;
  }

  // The input line: Enter runs it, Tab completes, Up / Down walk the history.
  constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
                                         ImGuiInputTextFlags_CallbackHistory;
  if (focus_input_) {
    ImGui::SetKeyboardFocusHere();
    focus_input_ = false;
  }
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputTextWithHint("##input", "command or cvar (Tab completes, help lists commands)", input_,
                               sizeof(input_), kFlags, &ConsoleWindow::InputCallback, this)) {
    model_.Submit(input_);
    input_[0] = '\0';
    follow_ = true;
    focus_input_ = true;  // keep typing
  }
  ImGui::End();
}

int ConsoleWindow::InputCallback(ImGuiInputTextCallbackData* data) {
  auto* self = static_cast<ConsoleWindow*>(data->UserData);
  auto replace = [data](const std::string& text) {
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, text.c_str());
  };
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
    const ConsoleModel::Completion completion =
        self->model_.Complete({data->Buf, static_cast<size_t>(data->BufTextLen)});
    if (completion.text != std::string_view(data->Buf, static_cast<size_t>(data->BufTextLen))) replace(completion.text);
    if (!completion.candidates.empty()) {
      std::string list;
      for (const std::string& name : completion.candidates) list += "  " + name;
      WLW_LOG(LogConsole, Info, "%s", list.c_str());
    }
  } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    const std::optional<std::string> text =
        data->EventKey == ImGuiKey_UpArrow ? self->model_.HistoryOlder() : self->model_.HistoryNewer();
    if (text) replace(*text);
  }
  return 0;
}

}  // namespace wlw::debug
#endif  // WLW_DEBUG_UI
