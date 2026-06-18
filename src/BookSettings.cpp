#include "BookSettings.h"

#include "logger.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <sstream>

namespace {
    constexpr auto kModDir = "Data/Viny Mods/Book of Perks";
    constexpr auto kBooksPath = "Data/Viny Mods/Book of Perks/Books.json";
    constexpr auto kBlacklistPath = "Data/Viny Mods/Book of Perks/Blacklist.json";

    std::unordered_map<std::string, BookSettings::BookOverride> g_overrides;
    std::unordered_set<std::string> g_blacklistedPerks;
    std::unordered_set<std::string> g_blacklistedPlugins;
    std::string g_baseBookKey;

    std::string ReadFile(const char* path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    template <class Writer>
    void WriteStringMember(Writer& writer, const char* key, const std::string& value) {
        writer.Key(key);
        writer.String(value.c_str(), static_cast<rapidjson::SizeType>(value.size()));
    }

    void WriteJson(const char* path, const rapidjson::StringBuffer& buffer) {
        std::filesystem::create_directories(kModDir);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            logger::error("[BookSettings] Falha ao salvar '{}'.", path);
            return;
        }
        file.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    }

    void LoadBooks() {
        g_overrides.clear();
        g_baseBookKey.clear();

        const auto json = ReadFile(kBooksPath);
        if (json.empty()) {
            return;
        }

        rapidjson::Document doc;
        doc.Parse(json.c_str(), json.size());
        if (doc.HasParseError() || !doc.IsObject()) {
            logger::warn("[BookSettings] Books.json invalido. Ignorando overrides.");
            return;
        }

        if (doc.HasMember("baseBookKey") && doc["baseBookKey"].IsString()) {
            g_baseBookKey = doc["baseBookKey"].GetString();
        }

        const auto it = doc.FindMember("bookOverrides");
        if (it == doc.MemberEnd() || !it->value.IsObject()) {
            return;
        }

        for (auto member = it->value.MemberBegin(); member != it->value.MemberEnd(); ++member) {
            if (!member->name.IsString() || !member->value.IsObject()) {
                continue;
            }

            BookSettings::BookOverride overrideData;
            const auto& value = member->value;
            if (value.HasMember("name") && value["name"].IsString()) {
                overrideData.name = value["name"].GetString();
            }
            if (value.HasMember("description") && value["description"].IsString()) {
                overrideData.description = value["description"].GetString();
            }
            if (value.HasMember("modelPath") && value["modelPath"].IsString()) {
                overrideData.modelPath = value["modelPath"].GetString();
            }
            if (value.HasMember("weight") && value["weight"].IsNumber()) {
                overrideData.weight = value["weight"].GetFloat();
            }
            if (value.HasMember("value") && value["value"].IsInt()) {
                overrideData.value = value["value"].GetInt();
            }

            g_overrides[member->name.GetString()] = std::move(overrideData);
        }
    }

    void LoadBlacklist() {
        g_blacklistedPerks.clear();
        g_blacklistedPlugins.clear();

        const auto json = ReadFile(kBlacklistPath);
        if (json.empty()) {
            return;
        }

        rapidjson::Document doc;
        doc.Parse(json.c_str(), json.size());
        if (doc.HasParseError() || !doc.IsObject()) {
            logger::warn("[BookSettings] Blacklist.json invalido. Ignorando blacklist.");
            return;
        }

        if (doc.HasMember("blacklistedPerks") && doc["blacklistedPerks"].IsArray()) {
            for (const auto& item : doc["blacklistedPerks"].GetArray()) {
                if (item.IsString()) {
                    g_blacklistedPerks.insert(item.GetString());
                }
            }
        }

        if (doc.HasMember("blacklistedPlugins") && doc["blacklistedPlugins"].IsArray()) {
            for (const auto& item : doc["blacklistedPlugins"].GetArray()) {
                if (item.IsString()) {
                    g_blacklistedPlugins.insert(item.GetString());
                }
            }
        }
    }
}

namespace BookSettings {
    void Load() {
        LoadBooks();
        LoadBlacklist();
    }

    void SaveBooks() {
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("version");
        writer.Uint(1);
        WriteStringMember(writer, "baseBookKey", g_baseBookKey);
        writer.Key("bookOverrides");
        writer.StartObject();
        for (const auto& [perkKey, overrideData] : g_overrides) {
            writer.Key(perkKey.c_str(), static_cast<rapidjson::SizeType>(perkKey.size()));
            writer.StartObject();
            WriteStringMember(writer, "name", overrideData.name);
            WriteStringMember(writer, "description", overrideData.description);
            WriteStringMember(writer, "modelPath", overrideData.modelPath);
            writer.Key("weight");
            writer.Double(overrideData.weight);
            writer.Key("value");
            writer.Int(overrideData.value);
            writer.EndObject();
        }
        writer.EndObject();
        writer.EndObject();
        WriteJson(kBooksPath, buffer);
    }

    void SaveBlacklist() {
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        writer.Key("version");
        writer.Uint(1);
        writer.Key("blacklistedPlugins");
        writer.StartArray();
        for (const auto& plugin : g_blacklistedPlugins) {
            writer.String(plugin.c_str(), static_cast<rapidjson::SizeType>(plugin.size()));
        }
        writer.EndArray();
        writer.Key("blacklistedPerks");
        writer.StartArray();
        for (const auto& perk : g_blacklistedPerks) {
            writer.String(perk.c_str(), static_cast<rapidjson::SizeType>(perk.size()));
        }
        writer.EndArray();
        writer.EndObject();
        WriteJson(kBlacklistPath, buffer);
    }

    std::string MakePerkKey(RE::BGSPerk* perk) {
        return FormUtil::NormalizeFormID(perk);
    }

    std::string MakePerkKey(const InternalFormInfo& info) {
        const auto localID = (info.formID & 0xFF000000) == 0xFE000000 ? (info.formID & 0xFFF) : (info.formID & 0x00FFFFFF);
        return std::format("{}|{:X}", info.pluginName, localID);
    }

    std::string MakeFormKey(RE::TESForm* form) {
        return FormUtil::NormalizeFormID(form);
    }

    std::string GetBaseBookKey() {
        return g_baseBookKey;
    }

    void SetBaseBookKey(std::string_view bookKey) {
        g_baseBookKey = bookKey;
    }

    bool IsBlacklisted(const InternalFormInfo& info) {
        return IsPluginBlacklisted(info.pluginName) || IsPerkBlacklisted(MakePerkKey(info));
    }

    bool IsPerkBlacklisted(std::string_view perkKey) {
        return g_blacklistedPerks.contains(std::string(perkKey));
    }

    bool IsPluginBlacklisted(std::string_view pluginName) {
        return g_blacklistedPlugins.contains(std::string(pluginName));
    }

    const BookOverride* GetOverride(std::string_view perkKey) {
        const auto it = g_overrides.find(std::string(perkKey));
        return it != g_overrides.end() ? &it->second : nullptr;
    }

    BookOverride& GetOrCreateOverride(std::string_view perkKey) {
        return g_overrides[std::string(perkKey)];
    }

    void RemoveOverride(std::string_view perkKey) {
        g_overrides.erase(std::string(perkKey));
    }

    void AddBlacklistedPerk(std::string_view perkKey) {
        g_blacklistedPerks.insert(std::string(perkKey));
    }

    void RemoveBlacklistedPerk(std::string_view perkKey) {
        g_blacklistedPerks.erase(std::string(perkKey));
    }

    void AddBlacklistedPlugin(std::string_view pluginName) {
        g_blacklistedPlugins.insert(std::string(pluginName));
    }

    void RemoveBlacklistedPlugin(std::string_view pluginName) {
        g_blacklistedPlugins.erase(std::string(pluginName));
    }

    const std::unordered_map<std::string, BookOverride>& GetOverrides() {
        return g_overrides;
    }

    const std::unordered_set<std::string>& GetBlacklistedPerks() {
        return g_blacklistedPerks;
    }

    const std::unordered_set<std::string>& GetBlacklistedPlugins() {
        return g_blacklistedPlugins;
    }

    std::vector<std::string> GetKnownPlugins() {
        std::unordered_set<std::string> unique;
        for (const auto& info : Manager::GetSingleton()->GetList("Perk")) {
            unique.insert(info.pluginName);
        }

        std::vector<std::string> result(unique.begin(), unique.end());
        std::ranges::sort(result);
        return result;
    }

}
