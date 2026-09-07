/**
 * @file        asset_overlay_test.cpp
 * @brief       Unit tests for separate asset-overlay packages and loadouts
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/system/asset_overlay.h>
#include <rex/system/asset_overlay_catalog.h>
#include <rex/system/asset_overlay_loadout.h>

namespace {

class TempDirectory {
 public:
  explicit TempDirectory(std::string_view name) {
    static std::atomic<uint64_t> next_id{0};
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            (std::string(name) + "_" + std::to_string(suffix) + "_" + std::to_string(next_id++));
    std::filesystem::create_directories(path_);
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

void WritePack(const std::filesystem::path& root, std::string_view id,
               std::string_view name = "Asset Pack") {
  std::filesystem::create_directories(root / "assets" / "ui");
  std::ofstream manifest(root / "asset-pack.toml", std::ios::binary);
  manifest << "manifest_version = 1\n[asset_pack]\nid = \"" << id << "\"\nname = \"" << name
           << "\"\nversion = \"1.0\"\n";
  std::ofstream(root / "assets" / "ui" / "logo.dds", std::ios::binary) << id;
}

}  // namespace

TEST_CASE("asset overlay catalog is separate and validates safe nonempty assets",
          "[asset_overlay]") {
  TempDirectory temp("rex_asset_overlay_catalog");
  WritePack(temp.path() / "high", "high");
  WritePack(temp.path() / "low", "low");
  const auto empty = temp.path() / "empty";
  std::filesystem::create_directories(empty / "assets");
  std::ofstream(empty / "asset-pack.toml", std::ios::binary)
      << "manifest_version = 1\n[asset_pack]\nid = \"empty\"\nname = \"Empty\"\n"
         "version = \"1.0\"\n";

  const auto catalog = rex::system::DiscoverAssetOverlayCatalog(temp.path());
  REQUIRE(catalog.packages.size() == 3);
  CHECK(catalog.Find("high")->assets_root.filename() == "assets");
  CHECK(catalog.Find("empty")->assets_root.filename() == "assets");
  CHECK(std::any_of(catalog.Find("empty")->diagnostics.begin(),
                    catalog.Find("empty")->diagnostics.end(),
                    [](const auto& diagnostic) { return diagnostic.blocking; }));
  CHECK_FALSE(std::any_of(catalog.Find("high")->diagnostics.begin(),
                          catalog.Find("high")->diagnostics.end(),
                          [](const auto& diagnostic) { return diagnostic.blocking; }));
  CHECK_FALSE(rex::system::IsValidAssetOverlayPackageId("Upper"));
}

TEST_CASE("asset overlay resolver uses top priority and reports shadowed packs",
          "[asset_overlay]") {
  TempDirectory temp("rex_asset_overlay_resolve");
  WritePack(temp.path() / "high", "high");
  WritePack(temp.path() / "low", "low");
  const auto catalog = rex::system::DiscoverAssetOverlayCatalog(temp.path());
  const std::vector<rex::system::AssetOverlayPackage> packages = {*catalog.Find("high"),
                                                                  *catalog.Find("low")};
  const auto result = rex::system::ResolveAssetOverlay(packages, "ui/logo.dds", 64);
  REQUIRE(result.has_value());
  CHECK(result->package_id == "high");
  CHECK(std::string(result->bytes.begin(), result->bytes.end()) == "high");
  REQUIRE(result->shadowed_package_ids.size() == 1);
  CHECK(result->shadowed_package_ids.front() == "low");
}

TEST_CASE("asset overlay resolver rejects unsafe and oversized winners", "[asset_overlay]") {
  TempDirectory temp("rex_asset_overlay_safety");
  WritePack(temp.path() / "high", "high");
  WritePack(temp.path() / "low", "low");
  const auto catalog = rex::system::DiscoverAssetOverlayCatalog(temp.path());
  const std::vector<rex::system::AssetOverlayPackage> packages = {*catalog.Find("high"),
                                                                  *catalog.Find("low")};
  const auto oversized = rex::system::ResolveAssetOverlay(packages, "ui/logo.dds", 2);
  REQUIRE_FALSE(oversized.has_value());
  CHECK(oversized.error().category == rex::ErrorCategory::Validation);
  const auto missing = rex::system::ResolveAssetOverlay(packages, "ui/missing.dds", 64);
  CHECK(missing.error().category == rex::ErrorCategory::NotFound);
  for (const auto key : {"", "/ui/logo.dds", "../ui/logo.dds", "ui//logo.dds", "ui\\logo.dds",
                         "C:/ui/logo.dds", "ui/./logo.dds"}) {
    const auto invalid = rex::system::ResolveAssetOverlay(packages, key, 64);
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().category == rex::ErrorCategory::Validation);
  }
}

TEST_CASE("asset overlay loadout is profile-local and atomic", "[asset_overlay]") {
  TempDirectory temp("rex_asset_overlay_loadout");
  WritePack(temp.path() / "high", "high");
  WritePack(temp.path() / "low", "low");
  const auto catalog = rex::system::DiscoverAssetOverlayCatalog(temp.path());
  const auto first = std::vector<std::string>{"high", "low"};
  REQUIRE(
      rex::system::ApplyAssetOverlayLoadout(temp.path() / "profile", catalog, first).succeeded());
  const auto read = rex::system::ReadAssetOverlayLoadout(temp.path() / "profile");
  REQUIRE(read.entries.size() == 2);
  CHECK(read.entries[0].id == "high");
  CHECK(read.entries[1].id == "low");
  CHECK(rex::system::SelectAssetOverlayLoadout(catalog, read).IsValid());
  CHECK(std::filesystem::exists(temp.path() / "profile" / "asset_order.txt"));

  const auto invalid_profile = temp.path() / "invalid-profile";
  std::filesystem::create_directories(invalid_profile);
  std::ofstream(invalid_profile / "asset_order.txt", std::ios::binary) << "missing\n";
  const auto blocked = rex::system::ApplyAssetOverlayLoadout(invalid_profile, catalog, first);
  CHECK(blocked.status == rex::system::AssetOverlayLoadoutApplyStatus::kInvalidCurrent);
  const auto replaced =
      rex::system::ApplyAssetOverlayLoadout(invalid_profile, catalog, first, true);
  REQUIRE(replaced.succeeded());
}
