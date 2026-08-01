#pragma once

#include "Manager.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BookSettings {
    struct BookOverride {
        std::string name;
        std::string description;
        std::string modelPath;
        float weight = -1.0f;
        std::int32_t value = -1;
    };

    void Load();
    void SaveBooks();
    void SaveBlacklist();

    [[nodiscard]] std::string MakePerkKey(RE::BGSPerk* perk);
    [[nodiscard]] std::string MakePerkKey(const InternalFormInfo& info);
    [[nodiscard]] std::string MakeBookEditorID(std::string_view perkKey);
    [[nodiscard]] std::string MakeBookEditorID(const InternalFormInfo& info);
    [[nodiscard]] std::string MakeFormKey(RE::TESForm* form);
    [[nodiscard]] std::string GetBaseBookKey();
    void SetBaseBookKey(std::string_view bookKey);
    [[nodiscard]] bool IsBlacklisted(const InternalFormInfo& info);
    [[nodiscard]] bool IsPerkBlacklisted(std::string_view perkKey);
    [[nodiscard]] bool IsPluginBlacklisted(std::string_view pluginName);
    [[nodiscard]] const BookOverride* GetOverride(std::string_view perkKey);
    [[nodiscard]] BookOverride& GetOrCreateOverride(std::string_view perkKey);
    void RemoveOverride(std::string_view perkKey);

    void AddBlacklistedPerk(std::string_view perkKey);
    void RemoveBlacklistedPerk(std::string_view perkKey);
    void AddBlacklistedPlugin(std::string_view pluginName);
    void RemoveBlacklistedPlugin(std::string_view pluginName);

    [[nodiscard]] const std::unordered_map<std::string, BookOverride>& GetOverrides();
    [[nodiscard]] const std::unordered_set<std::string>& GetBlacklistedPerks();
    [[nodiscard]] const std::unordered_set<std::string>& GetBlacklistedPlugins();
    [[nodiscard]] std::vector<std::string> GetKnownPlugins();
}
