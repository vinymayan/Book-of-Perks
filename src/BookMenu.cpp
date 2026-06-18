#include "BookMenu.h"

#include "BookManager.h"
#include "BookSettings.h"
#include "SKSEMenuFramework.h"
#include "logger.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <rapidjson/document.h>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {
    constexpr auto kLangPath = "Data/Viny Mods/Book of Perks/Language.json";

    std::string selectedPerkKey;
    BookSettings::BookOverride editBuffer;
    char filterBuffer[128]{};
    char pluginInput[128]{};
    char perkInput[128]{};
    char baseBookInput[128]{};
    std::string status;
    std::unordered_map<std::string, std::string> language;

    struct BookRow {
        const InternalFormInfo* info{};
        std::string perkKey;
    };

    std::string ReadTextFile(const char* path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void FlattenLanguageObject(const rapidjson::Value& value, const std::string& prefix) {
        if (!value.IsObject()) {
            return;
        }

        for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
            if (!member->name.IsString()) {
                continue;
            }

            const std::string key = prefix.empty() ? member->name.GetString() : prefix + "." + member->name.GetString();
            if (member->value.IsString()) {
                language[key] = member->value.GetString();
            } else if (member->value.IsObject()) {
                FlattenLanguageObject(member->value, key);
            }
        }
    }

    void LoadLanguage() {
        language.clear();

        const auto json = ReadTextFile(kLangPath);
        if (json.empty()) {
            return;
        }

        rapidjson::Document doc;
        doc.Parse(json.c_str(), json.size());
        if (!doc.HasParseError() && doc.IsObject()) {
            FlattenLanguageObject(doc, {});
        }
    }

    const char* GetLoc(const char* key, const char* fallback) {
        const auto it = language.find(key);
        return it != language.end() ? it->second.c_str() : fallback;
    }

    std::string ToLower(std::string_view value) {
        std::string lowered(value);
        std::ranges::transform(lowered, lowered.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return lowered;
    }

    bool ContainsInsensitive(std::string_view haystack, std::string_view needle) {
        if (needle.empty()) {
            return true;
        }
        return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
    }

    void CopyToBuffer(char* target, std::size_t size, const std::string& value) {
        if (!target || size == 0) {
            return;
        }
        target[0] = '\0';
        strcpy_s(target, size, value.c_str());
    }

    bool StringInput(const char* label, std::string& value, const std::size_t size = 256) {
        std::vector<char> buffer(size);
        CopyToBuffer(buffer.data(), buffer.size(), value);
        if (ImGuiMCP::InputText(label, buffer.data(), buffer.size())) {
            value = buffer.data();
            return true;
        }
        return false;
    }

    bool DescriptionInput(const char* label, std::string& value) {
        std::array<char, 2048> buffer{};
        CopyToBuffer(buffer.data(), buffer.size(), value);
        if (ImGuiMCP::InputTextMultiline(label, buffer.data(), buffer.size(), { 520.0f, 140.0f }, 0, nullptr, nullptr)) {
            value = buffer.data();
            return true;
        }
        return false;
    }

    std::string DefaultBookName(const InternalFormInfo& info) {
        return std::format("Learn {}", info.name);
    }

    std::string DefaultEditorID(const InternalFormInfo& info) {
        std::string id = "BoP_Learn_";
        id += !info.editorID.empty() ? info.editorID : std::format("{:08X}", info.formID);
        for (auto& ch : id) {
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                ch = '_';
            }
        }
        return id;
    }

    const InternalFormInfo* FindPerkInfo(std::string_view perkKey) {
        for (const auto& info : Manager::GetSingleton()->GetList("Perk")) {
            if (BookSettings::MakePerkKey(info) == perkKey) {
                return &info;
            }
        }
        return nullptr;
    }

    RE::BGSPerk* LookupPerk(const InternalFormInfo& info) {
        return RE::TESForm::LookupByID<RE::BGSPerk>(info.formID);
    }

    std::string GetBookDisplayName(RE::TESObjectBOOK* book) {
        if (!book) {
            return {};
        }

        if (const auto name = book->GetName(); name && name[0] != '\0') {
            return name;
        }

        if (const auto editorID = book->GetFormEditorID(); editorID && editorID[0] != '\0') {
            return editorID;
        }

        return std::format("{:08X}", book->GetFormID());
    }

    std::string GetBaseBookPreview() {
        const auto key = BookSettings::GetBaseBookKey();
        return key.empty() ? GetLoc("common.automatic", "Automatic") : key;
    }

    std::uint32_t GetLocalFormID(RE::TESForm* form) {
        return form ? (form->GetFormID() & 0x00FFFFFF) : 0;
    }

    std::vector<BookRow> BuildBookRows(std::string_view filter) {
        std::vector<BookRow> rows;
        for (const auto& info : Manager::GetSingleton()->GetList("Perk")) {
            if (BookSettings::IsBlacklisted(info)) {
                continue;
            }

            auto perkKey = BookSettings::MakePerkKey(info);
            if (!ContainsInsensitive(info.name, filter) && !ContainsInsensitive(info.editorID, filter) &&
                !ContainsInsensitive(info.pluginName, filter) && !ContainsInsensitive(perkKey, filter)) {
                continue;
            }

            rows.push_back({ &info, std::move(perkKey) });
        }
        return rows;
    }

    std::string ResolvePerkInput(std::string_view input) {
        if (input.empty()) {
            return {};
        }

        for (const auto& info : Manager::GetSingleton()->GetList("Perk")) {
            const auto perkKey = BookSettings::MakePerkKey(info);
            if (perkKey == input || info.name == input || info.editorID == input || info.GetDisplayName() == input) {
                return perkKey;
            }
        }

        return std::string(input);
    }

    void LoadEditor(std::string_view perkKey) {
        selectedPerkKey = perkKey;
        editBuffer = {};

        if (const auto overrideData = BookSettings::GetOverride(perkKey)) {
            editBuffer = *overrideData;
        }

        if (const auto info = FindPerkInfo(perkKey)) {
            if (editBuffer.name.empty()) {
                editBuffer.name = DefaultBookName(*info);
            }
            if (editBuffer.description.empty()) {
                editBuffer.description = info->description;
            }
        }
    }

    void SaveSelectedOverride() {
        const auto info = FindPerkInfo(selectedPerkKey);
        if (!info) {
            status = GetLoc("status.invalid_perk", "Invalid perk.");
            return;
        }

        auto& overrideData = BookSettings::GetOrCreateOverride(selectedPerkKey);
        overrideData = editBuffer;
        BookSettings::SaveBooks();
        BookManager::GetSingleton()->RebuildDynamicBooks();
        status = GetLoc("status.book_saved", "Book saved.");
    }

    void ResetSelectedOverride() {
        if (selectedPerkKey.empty()) {
            return;
        }
        BookSettings::RemoveOverride(selectedPerkKey);
        BookSettings::SaveBooks();
        BookManager::GetSingleton()->RebuildDynamicBooks();
        LoadEditor(selectedPerkKey);
        status = GetLoc("status.override_removed", "Override removed.");
    }

    void BlacklistSelectedPerk() {
        if (selectedPerkKey.empty()) {
            return;
        }
        BookSettings::AddBlacklistedPerk(selectedPerkKey);
        BookSettings::SaveBlacklist();
        BookManager::GetSingleton()->ReleaseBookForPerkKey(selectedPerkKey);
        BookManager::GetSingleton()->RebuildDynamicBooks();
        selectedPerkKey.clear();
        status = GetLoc("status.perk_blacklisted", "Perk moved to blacklist.");
    }

    void DrawBaseBookSelector() {
        if (baseBookInput[0] == '\0') {
            const auto key = BookSettings::GetBaseBookKey();
            if (!key.empty()) {
                CopyToBuffer(baseBookInput, sizeof(baseBookInput), key);
            }
        }

        const auto preview = GetBaseBookPreview();
        ImGuiMCP::PushID("BaseBookCombo");
        if (ImGuiMCP::BeginCombo(GetLoc("field.base_book", "Base book"), preview.c_str())) {
            ImGuiMCP::SetNextItemWidth(-1.0f);
            ImGuiMCP::InputText("##filter", baseBookInput, sizeof(baseBookInput));
            ImGuiMCP::Separator();

            auto dataHandler = RE::TESDataHandler::GetSingleton();
            if (dataHandler) {
                const std::string filter = baseBookInput;
                ImGuiMCP::BeginChild("##scroll", { 0.0f, 220.0f }, false);
                for (auto book : dataHandler->GetFormArray<RE::TESObjectBOOK>()) {
                    if (book && !book->IsDeleted() && !book->IsIgnored()) {
                        const auto key = BookSettings::MakeFormKey(book);
                        const auto displayName = GetBookDisplayName(book);
                        if (!ContainsInsensitive(displayName, filter) && !ContainsInsensitive(key, filter)) {
                            continue;
                        }

                        const auto label = std::format("{} ({})", displayName, key);
                        const bool selected = BookSettings::GetBaseBookKey() == key;
                        if (ImGuiMCP::Selectable(label.c_str(), selected)) {
                            BookSettings::SetBaseBookKey(key);
                            BookSettings::SaveBooks();
                            BookManager::GetSingleton()->RebuildDynamicBooks();
                            CopyToBuffer(baseBookInput, sizeof(baseBookInput), key);
                            status = GetLoc("status.base_book_saved", "Base book saved.");
                        }
                        if (selected) {
                            ImGuiMCP::SetItemDefaultFocus();
                        }
                    }
                }
                ImGuiMCP::EndChild();
            }
            ImGuiMCP::EndCombo();
        }
        ImGuiMCP::PopID();
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("button.clear_base_book", "Clear Base"))) {
            BookSettings::SetBaseBookKey({});
            BookSettings::SaveBooks();
            BookManager::GetSingleton()->RebuildDynamicBooks();
            baseBookInput[0] = '\0';
            status = GetLoc("status.base_book_cleared", "Base book cleared.");
        }
    }

    void DrawBooksTable() {
        const auto rows = BuildBookRows(filterBuffer);

        if (ImGuiMCP::BeginTable("BookOfPerksBooks", 7,
            ImGuiMCP::ImGuiTableFlags_Borders | ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_Resizable |
                ImGuiMCP::ImGuiTableFlags_Reorderable | ImGuiMCP::ImGuiTableFlags_ScrollY,
            { 0.0f, 360.0f })) {
            ImGuiMCP::TableSetupScrollFreeze(0, 1);
            ImGuiMCP::TableSetupColumn(GetLoc("table.perk", "Perk"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 190.0f);
            ImGuiMCP::TableSetupColumn(GetLoc("table.plugin", "Plugin"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGuiMCP::TableSetupColumn(GetLoc("table.dpf_formid", "DPF FormID"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGuiMCP::TableSetupColumn(GetLoc("table.name", "Name"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 180.0f);
            ImGuiMCP::TableSetupColumn(GetLoc("table.editorid", "EditorID"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGuiMCP::TableSetupColumn(GetLoc("table.weight", "Weight"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGuiMCP::TableSetupColumn(GetLoc("table.value", "Value"), ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGuiMCP::TableHeadersRow();

            auto clipper = std::unique_ptr<ImGuiMCP::ImGuiListClipper, decltype(&ImGuiMCP::ImGuiListClipperManager::Destroy)>(
                ImGuiMCP::ImGuiListClipperManager::Create(), &ImGuiMCP::ImGuiListClipperManager::Destroy);
            ImGuiMCP::ImGuiListClipperManager::Begin(clipper.get(), static_cast<int>(rows.size()), 0.0f);
            while (ImGuiMCP::ImGuiListClipperManager::Step(clipper.get())) {
                for (int rowIndex = clipper->DisplayStart; rowIndex < clipper->DisplayEnd; ++rowIndex) {
                    const auto& row = rows[static_cast<std::size_t>(rowIndex)];
                    const auto& info = *row.info;
                    const auto& perkKey = row.perkKey;
                    const auto overrideData = BookSettings::GetOverride(perkKey);
                    auto perk = LookupPerk(info);
                    auto book = BookManager::GetSingleton()->GetBookForPerk(perk);
                    const auto displayName = overrideData && !overrideData->name.empty() ? overrideData->name : DefaultBookName(info);
                    const auto displayEditorID = DefaultEditorID(info);
                    const auto displayWeight = overrideData && overrideData->weight >= 0.0f ? overrideData->weight : (book ? book->weight : 0.0f);
                    const auto displayValue = overrideData && overrideData->value >= 0 ? overrideData->value : (book ? book->value : 0);

                    ImGuiMCP::TableNextRow();
                    ImGuiMCP::TableNextColumn();
                    const bool selected = selectedPerkKey == perkKey;
                    if (ImGuiMCP::Selectable(info.GetDisplayName().c_str(), selected, ImGuiMCP::ImGuiSelectableFlags_SpanAllColumns)) {
                        LoadEditor(perkKey);
                    }
                    ImGuiMCP::TableNextColumn();
                    ImGuiMCP::Text("%s", info.pluginName.c_str());
                    ImGuiMCP::TableNextColumn();
                    ImGuiMCP::Text("%06X", GetLocalFormID(book));
                    if (ImGuiMCP::IsItemHovered()) {
                        ImGuiMCP::SetTooltip("%s: Dynamic Persistent Forms.esp\n%s: %08X\n%s: %s",
                            GetLoc("tooltip.dpf_plugin", "DPF plugin"), GetLoc("tooltip.full_formid", "Full FormID"),
                            book ? book->GetFormID() : 0, GetLoc("tooltip.perk_plugin", "Perk plugin"), info.pluginName.c_str());
                    }
                    ImGuiMCP::TableNextColumn();
                    ImGuiMCP::Text("%s", displayName.c_str());
                    ImGuiMCP::TableNextColumn();
                    ImGuiMCP::Text("%s", displayEditorID.c_str());
                    ImGuiMCP::TableNextColumn();
                    ImGuiMCP::Text("%.2f", displayWeight);
                    ImGuiMCP::TableNextColumn();
                    ImGuiMCP::Text("%d", displayValue);
                }
            }
            ImGuiMCP::ImGuiListClipperManager::End(clipper.get());

            ImGuiMCP::EndTable();
        }
    }

    void DrawEditor() {
        if (selectedPerkKey.empty()) {
            ImGuiMCP::Text("%s", GetLoc("menu.select_book", "Select a book."));
            return;
        }

        ImGuiMCP::Separator();
        ImGuiMCP::Text("%s: %s", GetLoc("menu.editing", "Editing"), selectedPerkKey.c_str());

        StringInput(GetLoc("field.name", "Name"), editBuffer.name);
        DescriptionInput(GetLoc("field.description", "Description"), editBuffer.description);
        StringInput(GetLoc("field.model_path", "NIF model path"), editBuffer.modelPath, 512);
        if (const auto info = FindPerkInfo(selectedPerkKey)) {
            ImGuiMCP::Text("%s: %s", GetLoc("field.editorid_readonly", "EditorID"), DefaultEditorID(*info).c_str());
        }
        ImGuiMCP::InputFloat(GetLoc("field.weight", "Weight"), &editBuffer.weight, 0.1f, 1.0f, "%.2f");
        int value = editBuffer.value;
        if (ImGuiMCP::InputInt(GetLoc("field.value", "Value"), &value)) {
            editBuffer.value = std::max(-1, value);
        }

        if (ImGuiMCP::Button(GetLoc("button.save", "Save"))) {
            SaveSelectedOverride();
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("button.reset_overrides", "Reset Overrides"))) {
            ResetSelectedOverride();
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("button.blacklist_delete", "Blacklist / Delete"))) {
            BlacklistSelectedPerk();
        }
    }

    void __stdcall BooksRender() {
        BookSettings::Load();
        Manager::GetSingleton()->PopulateAllLists();

        if (ImGuiMCP::Button(GetLoc("button.refresh_rebuild", "Refresh / Rebuild"))) {
            BookSettings::SaveBooks();
            BookSettings::SaveBlacklist();
            BookManager::GetSingleton()->RebuildDynamicBooks();
            status = GetLoc("status.rebuild_complete", "Rebuild complete.");
        }

        ImGuiMCP::SameLine();
        ImGuiMCP::Text("%s", status.c_str());

        DrawBaseBookSelector();
        ImGuiMCP::InputText(GetLoc("field.filter", "Filter"), filterBuffer, sizeof(filterBuffer));
        DrawEditor();
        ImGuiMCP::Separator();
        DrawBooksTable();
    }

    void DrawBlacklistedPerks() {
        ImGuiMCP::PushID("BlacklistPerkCombo");
        if (ImGuiMCP::BeginCombo(GetLoc("field.perk_key", "Perk key"), perkInput[0] ? perkInput : GetLoc("common.select", "Select"))) {
            ImGuiMCP::SetNextItemWidth(-1.0f);
            ImGuiMCP::InputText("##filter", perkInput, sizeof(perkInput));
            ImGuiMCP::Separator();

            const std::string filter = perkInput;
            ImGuiMCP::BeginChild("##scroll", { 0.0f, 220.0f }, false);
            for (const auto& info : Manager::GetSingleton()->GetList("Perk")) {
                const auto perkKey = BookSettings::MakePerkKey(info);
                const auto displayName = info.GetDisplayName();
                if (!ContainsInsensitive(displayName, filter) && !ContainsInsensitive(perkKey, filter) &&
                    !ContainsInsensitive(info.pluginName, filter) && !ContainsInsensitive(info.editorID, filter)) {
                    continue;
                }
                const auto label = std::format("{} ({})", displayName, perkKey);
                if (ImGuiMCP::Selectable(label.c_str(), false)) {
                    CopyToBuffer(perkInput, sizeof(perkInput), perkKey);
                }
            }
            ImGuiMCP::EndChild();
            ImGuiMCP::EndCombo();
        }
        ImGuiMCP::PopID();
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("button.add_perk", "Add Perk"))) {
            const auto key = ResolvePerkInput(perkInput);
            if (!key.empty()) {
                BookSettings::AddBlacklistedPerk(key);
                BookSettings::SaveBlacklist();
                BookManager::GetSingleton()->ReleaseBookForPerkKey(key);
                BookManager::GetSingleton()->RebuildDynamicBooks();
                perkInput[0] = '\0';
            }
        }

        for (const auto& key : BookSettings::GetBlacklistedPerks()) {
            ImGuiMCP::PushID(key.c_str());
            ImGuiMCP::Text("%s", key.c_str());
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button(GetLoc("button.remove", "Remove"))) {
                BookSettings::RemoveBlacklistedPerk(key);
                BookSettings::SaveBlacklist();
                BookManager::GetSingleton()->RebuildDynamicBooks();
                ImGuiMCP::PopID();
                break;
            }
            ImGuiMCP::PopID();
        }
    }

    void DrawBlacklistedPlugins() {
        ImGuiMCP::PushID("BlacklistPluginCombo");
        if (ImGuiMCP::BeginCombo(GetLoc("field.plugin_name", "Plugin name"), pluginInput[0] ? pluginInput : GetLoc("common.select", "Select"))) {
            ImGuiMCP::SetNextItemWidth(-1.0f);
            ImGuiMCP::InputText("##filter", pluginInput, sizeof(pluginInput));
            ImGuiMCP::Separator();

            const std::string filter = pluginInput;
            ImGuiMCP::BeginChild("##scroll", { 0.0f, 180.0f }, false);
            for (const auto& plugin : BookSettings::GetKnownPlugins()) {
                if (!ContainsInsensitive(plugin, filter)) {
                    continue;
                }
                if (ImGuiMCP::Selectable(plugin.c_str(), false)) {
                    CopyToBuffer(pluginInput, sizeof(pluginInput), plugin);
                }
            }
            ImGuiMCP::EndChild();
            ImGuiMCP::EndCombo();
        }
        ImGuiMCP::PopID();
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(GetLoc("button.add_plugin", "Add Plugin"))) {
            const std::string plugin = pluginInput;
            if (!plugin.empty()) {
                BookSettings::AddBlacklistedPlugin(plugin);
                BookSettings::SaveBlacklist();
                BookManager::GetSingleton()->ReleaseBooksForPlugin(plugin);
                BookManager::GetSingleton()->RebuildDynamicBooks();
                pluginInput[0] = '\0';
            }
        }

        for (const auto& plugin : BookSettings::GetBlacklistedPlugins()) {
            ImGuiMCP::PushID(plugin.c_str());
            ImGuiMCP::Text("%s", plugin.c_str());
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button(GetLoc("button.remove", "Remove"))) {
                BookSettings::RemoveBlacklistedPlugin(plugin);
                BookSettings::SaveBlacklist();
                BookManager::GetSingleton()->RebuildDynamicBooks();
                ImGuiMCP::PopID();
                break;
            }
            ImGuiMCP::PopID();
        }
    }

    void __stdcall BlacklistRender() {
        BookSettings::Load();
        Manager::GetSingleton()->PopulateAllLists();

        ImGuiMCP::Text("%s", GetLoc("section.plugins", "Plugins"));
        DrawBlacklistedPlugins();
        ImGuiMCP::Separator();
        ImGuiMCP::Text("%s", GetLoc("section.perks", "Perks"));
        DrawBlacklistedPerks();
    }
}

namespace BookMenu {
    void Register() {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::info("[BookMenu] SKSEMenuFramework nao instalado.");
            return;
        }

        LoadLanguage();
        BookSettings::Load();
        SKSEMenuFramework::SetSection(GetLoc("menu.section", "Book of Perks"));
        SKSEMenuFramework::AddSectionItem(GetLoc("tab.books", "Books"), BooksRender);
        SKSEMenuFramework::AddSectionItem(GetLoc("tab.blacklist", "Blacklist"), BlacklistRender);
        logger::info("[BookMenu] Menu registrado.");
    }
}
