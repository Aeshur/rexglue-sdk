/**
 * @file        ui/overlay/mod_manager_overlay.cpp
 * @brief       Profile-local mod loadout manager overlay
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/ui/overlay/mod_manager_overlay.h>

#include <fstream>
#include <algorithm>
#include <vector>

#include <imgui.h>

#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>

namespace rex::ui {
namespace {

constexpr ImVec4 kHeaderText{0.60f, 0.85f, 1.00f, 1.00f};
constexpr ImVec4 kMutedText{0.60f, 0.62f, 0.66f, 1.00f};
constexpr ImVec4 kCodeBadge{1.00f, 0.82f, 0.30f, 1.00f};
constexpr float kIconSize = 40.0f;

const char* DiagnosticLabel(const rex::system::ModDiagnostic& diagnostic) {
  return diagnostic.severity == rex::system::ModDiagnosticSeverity::kError ? "error: "
                                                                           : "warning: ";
}

}  // namespace

ModManagerDialog::ModManagerDialog(ImGuiDrawer* imgui_drawer, ImmediateDrawer* immediate_drawer,
                                   rex::Runtime* runtime, std::function<void()> close_callback)
    : ImGuiDialog(imgui_drawer),
      immediate_drawer_(immediate_drawer),
      runtime_(runtime),
      close_callback_(std::move(close_callback)) {
  if (runtime_) {
    staged_ids_ = runtime_->mod_order_ids();
  }
}

ModManagerDialog::~ModManagerDialog() = default;

bool ModManagerDialog::IsDirty() const {
  return runtime_ && staged_ids_ != runtime_->mod_order_ids();
}

void ModManagerDialog::ClearStaleStatus() {
  status_message_.clear();
  replacement_popup_ = false;
}

bool ModManagerDialog::RequestClose() {
  if (!IsDirty()) {
    return true;
  }
  discard_popup_ = true;
  return false;
}

void ModManagerDialog::ApplyStaged(bool replace_invalid_current) {
  if (!runtime_) {
    return;
  }
  const auto result = runtime_->ApplyModLoadout(staged_ids_, replace_invalid_current);
  if (result.status == rex::system::ModLoadoutApplyStatus::kInvalidCurrent) {
    replacement_popup_ = true;
    return;
  }
  if (result.succeeded()) {
    status_message_ = "Loadout saved. Restart the game to apply native changes.";
    return;
  }
  status_message_.clear();
  if (!result.diagnostics.empty()) {
    status_message_ = result.diagnostics.front().message;
  } else {
    status_message_ = "Could not save the mod loadout.";
  }
}

void ModManagerDialog::DrawDiscardPopup() {
  if (discard_popup_) {
    ImGui::OpenPopup("Discard staged mod changes?");
  }
  if (!ImGui::BeginPopupModal("Discard staged mod changes?", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::TextWrapped("Discard the staged mod order and enabled changes?");
  if (ImGui::Button("Discard")) {
    staged_ids_ = runtime_ ? runtime_->mod_order_ids() : std::vector<std::string>{};
    discard_popup_ = false;
    ImGui::CloseCurrentPopup();
    if (close_callback_) {
      close_callback_();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Keep editing")) {
    discard_popup_ = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void ModManagerDialog::DrawReplacementPopup() {
  if (replacement_popup_) {
    ImGui::OpenPopup("Replace invalid mod_order.txt?");
  }
  if (!ImGui::BeginPopupModal("Replace invalid mod_order.txt?", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::TextWrapped("The current profile loadout has errors. Replace it with the staged order?");
  if (ImGui::Button("Replace and save")) {
    replacement_popup_ = false;
    ImGui::CloseCurrentPopup();
    ApplyStaged(true);
  }
  ImGui::SameLine();
  if (ImGui::Button("Keep current file")) {
    replacement_popup_ = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

ImmediateTexture* ModManagerDialog::GetIcon(const rex::system::ModPackage& mod) {
  if (mod.icon_path.empty()) {
    return nullptr;
  }
  const std::string key = mod.icon_path.string();
  auto cached = icon_cache_.find(key);
  if (cached != icon_cache_.end()) {
    return cached->second.get();
  }

  std::unique_ptr<ImmediateTexture> texture;
  if (immediate_drawer_) {
    std::ifstream file(mod.icon_path, std::ios::binary | std::ios::ate);
    if (file) {
      std::streamsize length = file.tellg();
      if (length > 0) {
        file.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        if (file.read(reinterpret_cast<char*>(bytes.data()), length)) {
          int width = 0;
          int height = 0;
          auto rgba = DecodeImageRGBA(bytes.data(), bytes.size(), width, height);
          if (!rgba.empty() && width > 0 && height > 0) {
            texture = immediate_drawer_->CreateTexture(
                static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                ImmediateTextureFilter::kLinear, false, rgba.data());
          }
        }
      }
    }
  }

  ImmediateTexture* result = texture.get();
  icon_cache_.emplace(key, std::move(texture));
  return result;
}

void ModManagerDialog::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 40.0f), ImGuiCond_FirstUseEver,
                          ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(560.0f, 480.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.92f);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

  if (ImGui::Begin("Mods##overlay", nullptr, ImGuiWindowFlags_NoCollapse)) {
    const auto* catalog = runtime_ ? &runtime_->mod_catalog() : nullptr;
    size_t count = catalog ? catalog->packages.size() : 0;

    ImGui::PushStyleColor(ImGuiCol_Text, kHeaderText);
    ImGui::Text("%zu mod package%s installed", count, count == 1 ? "" : "s");
    ImGui::PopStyleColor();
    ImGui::TextColored(kMutedText, "Changes apply after restart.");
    if (runtime_ && runtime_->restart_required()) {
      ImGui::TextColored(kCodeBadge, "Restart required to apply the saved loadout.");
    }
    if (runtime_ && ImGui::Button("Rescan installed mods")) {
      runtime_->RescanModCatalog();
      ClearStaleStatus();
    }
    ImGui::Separator();
    ImGui::Spacing();

    if (catalog) {
      bool has_nonblocking_catalog_diagnostics = false;
      for (const auto& diagnostic : catalog->diagnostics) {
        if (!diagnostic.blocking) {
          has_nonblocking_catalog_diagnostics = true;
          break;
        }
      }
      if (has_nonblocking_catalog_diagnostics) {
        for (const auto& diagnostic : catalog->diagnostics) {
          if (!diagnostic.blocking) {
            ImGui::TextWrapped("%s%s", DiagnosticLabel(diagnostic), diagnostic.message.c_str());
          }
        }
        ImGui::Separator();
      }
    }
    if (runtime_ && !runtime_->mod_loadout_diagnostics().empty()) {
      ImGui::TextColored(kCodeBadge, "Current profile loadout has blocking errors:");
      for (const auto& diagnostic : runtime_->mod_loadout_diagnostics()) {
        if (diagnostic.line != 0) {
          ImGui::TextWrapped("- line %zu: %s", diagnostic.line, diagnostic.message.c_str());
        } else {
          ImGui::TextWrapped("- %s", diagnostic.message.c_str());
        }
      }
      ImGui::Separator();
    }

    const auto staged =
        runtime_ ? runtime_->ValidateModLoadout(staged_ids_) : rex::system::ModLoadoutSelection{};
    if (!staged.IsValid()) {
      ImGui::TextColored(kCodeBadge, "Staged loadout cannot be applied:");
      for (const auto& diagnostic : staged.diagnostics) {
        if (diagnostic.line != 0) {
          ImGui::TextWrapped("- line %zu: %s", diagnostic.line, diagnostic.message.c_str());
        } else {
          ImGui::TextWrapped("- %s", diagnostic.message.c_str());
        }
      }
      ImGui::Separator();
    }

    std::vector<std::string> unavailable_staged_ids;
    if (catalog) {
      for (const auto& id : staged_ids_) {
        if (!catalog->Find(id)) {
          unavailable_staged_ids.push_back(id);
        }
      }
    }
    const bool has_unavailable_active =
        runtime_ &&
        std::any_of(runtime_->active_mod_states().begin(), runtime_->active_mod_states().end(),
                    [catalog](const auto& state) { return !catalog || !catalog->Find(state.id); });

    const float footer_height =
        ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 16.0f;
    if ((!catalog || catalog->packages.empty()) && unavailable_staged_ids.empty() &&
        !has_unavailable_active) {
      ImGui::TextDisabled("No installed mod packages.");
    } else {
      ImGui::BeginChild("##modlist", ImVec2(0.0f, -footer_height), false);
      for (const auto& id : unavailable_staged_ids) {
        ImGui::PushID(("unavailable:" + id).c_str());
        ImGui::TextColored(kCodeBadge, "Unavailable staged package: %s", id.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) {
          staged_ids_.erase(std::remove(staged_ids_.begin(), staged_ids_.end(), id),
                            staged_ids_.end());
          ClearStaleStatus();
        }
        ImGui::PopID();
      }
      if (runtime_) {
        for (const auto& active_mod : runtime_->active_mod_states()) {
          if (!catalog || !catalog->Find(active_mod.id)) {
            ImGui::TextColored(kHeaderText,
                               "Active until restart: %s | package unavailable | "
                               "load order #%zu",
                               active_mod.id.c_str(), active_mod.order + 1);
          }
        }
      }
      if (catalog) {
        for (const auto& mod : catalog->packages) {
          const std::string package_id = mod.id.empty() ? mod.folder_name : mod.id;
          const std::string package_key = mod.mod_root.generic_string();
          ImGui::PushID(package_key.c_str());
          const auto staged_it = std::find(staged_ids_.begin(), staged_ids_.end(), package_id);
          bool is_staged = staged_it != staged_ids_.end();
          std::optional<size_t> staged_order;
          if (ImGui::Checkbox("Enable", &is_staged)) {
            ClearStaleStatus();
            if (is_staged) {
              staged_ids_.push_back(package_id);
            } else {
              staged_ids_.erase(std::remove(staged_ids_.begin(), staged_ids_.end(), package_id),
                                staged_ids_.end());
            }
          }
          if (is_staged) {
            const size_t order =
                static_cast<size_t>(std::find(staged_ids_.begin(), staged_ids_.end(), package_id) -
                                    staged_ids_.begin());
            staged_order = order;
            ImGui::SameLine();
            ImGui::TextColored(kHeaderText, "staged order: #%zu", order + 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("up") && order > 0) {
              std::swap(staged_ids_[order], staged_ids_[order - 1]);
              ClearStaleStatus();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("down") && order + 1 < staged_ids_.size()) {
              std::swap(staged_ids_[order], staged_ids_[order + 1]);
              ClearStaleStatus();
            }
          }
          if (ImmediateTexture* icon = GetIcon(mod)) {
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(icon), ImVec2(kIconSize, kIconSize),
                               ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1));
          } else {
            ImGui::Dummy(ImVec2(kIconSize, kIconSize));
          }
          ImGui::SameLine();

          ImGui::BeginGroup();
          ImGui::Text("%s", mod.display_name.c_str());
          if (!mod.version.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kMutedText, "v%s", mod.version.c_str());
          }
          if (!mod.author.empty()) {
            ImGui::TextColored(kMutedText, "by %s", mod.author.c_str());
          }
          if (mod.status == rex::system::ModPackageStatus::kReady) {
            if (mod.active && !is_staged) {
              ImGui::TextColored(kHeaderText, "Active until restart");
            } else if (!mod.active && is_staged) {
              ImGui::TextColored(kHeaderText, "Starts after restart");
            } else if (mod.active && staged_order && mod.active_order != staged_order) {
              ImGui::TextColored(kHeaderText, "Currently active at load order #%zu",
                                 mod.active_order.value_or(0) + 1);
            }
          }
          for (const auto& diagnostic : mod.diagnostics) {
            ImGui::TextWrapped("%s%s", DiagnosticLabel(diagnostic), diagnostic.message.c_str());
          }
          if (!mod.description.empty()) {
            ImGui::TextWrapped("%s", mod.description.c_str());
          }
          ImGui::EndGroup();
          ImGui::Separator();
          ImGui::PopID();
        }
      }
      ImGui::EndChild();
    }
    ImGui::Separator();
    const bool has_persistable_change =
        runtime_ &&
        (IsDirty() || !runtime_->mod_order_file_exists() || runtime_->mod_order_file_invalid());
    const bool can_apply = has_persistable_change && staged.IsValid();
    if (!can_apply) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Apply")) {
      if (runtime_->mod_order_file_invalid()) {
        replacement_popup_ = true;
      } else {
        ApplyStaged(false);
      }
    }
    if (!can_apply) {
      ImGui::EndDisabled();
    }
    if (!status_message_.empty()) {
      ImGui::SameLine();
      ImGui::TextColored(kMutedText, "%s", status_message_.c_str());
    }
  }
  ImGui::End();

  ImGui::PopStyleVar(2);
  DrawDiscardPopup();
  DrawReplacementPopup();
}

}  // namespace rex::ui
