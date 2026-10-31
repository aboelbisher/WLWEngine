#if WLW_DEBUG_UI
#include "wlw/debug/cvar_panel.h"

#include <cstdio>
#include <string>

#include "wlw/app/application.h"
#include "wlw/core/console.h"
#include "wlw/core/cvar.h"

namespace wlw::debug {
namespace {

void Set(CVarBase& cvar, const std::string& text) {
  const CVarSetResult result = cvar.SetFromText(text, CVarSource::Console);
  if (result != CVarSetResult::Ok) {
    WLW_LOG(LogConsole, Warning, "%s: %s", cvar.Name().c_str(), CVarSetResultText(result));
  }
}

// The widget for one cvar, labelled with the part of its name after the group's prefix.
void DrawCVar(CVarBase& cvar, const char* label) {
  switch (cvar.Type()) {
    case CVarType::Bool: {
      bool value = static_cast<CVar<bool>&>(cvar).Get();
      if (ImGui::Checkbox(label, &value)) Set(cvar, value ? "1" : "0");
      break;
    }
    case CVarType::Int: {
      auto& typed = static_cast<CVar<int>&>(cvar);
      int value = typed.Get();
      const bool changed = typed.GetRange()
                               ? ImGui::SliderInt(label, &value, typed.GetRange()->min, typed.GetRange()->max)
                               : ImGui::InputInt(label, &value);
      if (changed) Set(cvar, std::to_string(value));
      break;
    }
    case CVarType::Float: {
      auto& typed = static_cast<CVar<float>&>(cvar);
      float value = typed.Get();
      const bool changed = typed.GetRange()
                               ? ImGui::SliderFloat(label, &value, typed.GetRange()->min, typed.GetRange()->max)
                               : ImGui::DragFloat(label, &value, 0.01f);
      if (changed) Set(cvar, FormatCVarFloat(value));
      break;
    }
    case CVarType::String: {
      // ImGui edits its own copy while the field is active; Enter applies it.
      char buffer[512];
      std::snprintf(buffer, sizeof(buffer), "%s", cvar.ValueText().c_str());
      if (ImGui::InputText(label, buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) Set(cvar, buffer);
      break;
    }
  }
}

}  // namespace

void CVarPanel::OnDebugUI() {
  if (App().Internal().HostedSessions() > 0) return;  // the Studio while a game plays: the game's panel draws it
  ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(8.0f, 40.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("CVars")) {
    ImGui::End();
    return;
  }
  filter_.Draw("Filter");
  std::string group;
  bool group_open = false;
  for (CVarBase* cvar : AllCVars()) {
    const std::string& name = cvar->Name();
    if (!filter_.PassFilter(name.c_str()) && !filter_.PassFilter(cvar->Help().c_str())) continue;
    const size_t dot = name.find('.');
    const std::string prefix = dot == std::string::npos ? std::string("(other)") : name.substr(0, dot);
    if (prefix != group) {  // sorted by name: each prefix comes in one run
      group = prefix;
      group_open = ImGui::CollapsingHeader(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    }
    if (!group_open) continue;
    ImGui::PushID(name.c_str());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    DrawCVar(*cvar, dot == std::string::npos ? name.c_str() : name.c_str() + dot + 1);
    if (ImGui::IsItemHovered() && !cvar->Help().empty()) {
      ImGui::SetTooltip("%s\n%s (default %s)", cvar->Help().c_str(), name.c_str(), cvar->DefaultText().c_str());
    }
    if (!cvar->IsDefault()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("reset")) Set(*cvar, cvar->DefaultText());  // through the flags too
    }
    ImGui::PopID();
  }
  ImGui::End();
}

}  // namespace wlw::debug
#endif  // WLW_DEBUG_UI
