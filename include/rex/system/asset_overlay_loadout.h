/**
 * @file        system/asset_overlay_loadout.h
 * @brief       Profile-local ordered asset-overlay loadout
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <rex/system/asset_overlay_catalog.h>

namespace rex::system {

inline constexpr std::string_view kAssetOverlayOrderFileName = "asset_order.txt";

struct AssetOverlayLoadoutEntry {
  std::string id;
  size_t line = 0;
};

struct AssetOverlayLoadoutDiagnostic {
  size_t line = 0;
  std::string message;
  std::filesystem::path path;
};

struct AssetOverlayLoadoutFile {
  std::filesystem::path path;
  bool exists = false;
  std::vector<AssetOverlayLoadoutEntry> entries;
  std::vector<AssetOverlayLoadoutDiagnostic> diagnostics;
};

struct AssetOverlayLoadoutSelection {
  std::vector<std::string> requested_ids;
  std::vector<AssetOverlayPackage> packages;
  std::vector<AssetOverlayLoadoutDiagnostic> diagnostics;

  bool IsValid() const { return diagnostics.empty(); }
};

enum class AssetOverlayLoadoutApplyStatus {
  kSuccess,
  kInvalidDesired,
  kInvalidCurrent,
  kInvalidProfile,
  kIoError,
};

struct AssetOverlayLoadoutApplyResult {
  AssetOverlayLoadoutApplyStatus status = AssetOverlayLoadoutApplyStatus::kIoError;
  std::vector<AssetOverlayLoadoutDiagnostic> diagnostics;

  bool succeeded() const { return status == AssetOverlayLoadoutApplyStatus::kSuccess; }
};

AssetOverlayLoadoutFile ReadAssetOverlayLoadout(const std::filesystem::path& profile_root);
AssetOverlayLoadoutSelection SelectAssetOverlayLoadout(const AssetOverlayCatalog& catalog,
                                                       const AssetOverlayLoadoutFile& loadout);
AssetOverlayLoadoutSelection ValidateAssetOverlayLoadout(const AssetOverlayCatalog& catalog,
                                                         std::span<const std::string> ids,
                                                         const std::filesystem::path& path = {});
AssetOverlayLoadoutApplyResult ApplyAssetOverlayLoadout(const std::filesystem::path& profile_root,
                                                        const AssetOverlayCatalog& catalog,
                                                        std::span<const std::string> ids,
                                                        bool replace_invalid_current = false);

}  // namespace rex::system
