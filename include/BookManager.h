#pragma once

#include "Manager.h"

#include <string_view>
#include <unordered_map>
#include <vector>

class BookManager {
public:
    static BookManager* GetSingleton();

    void Initialize();
    void InstallHooks();
    void RebuildDynamicBooks();
    void Revert();

    [[nodiscard]] RE::TESObjectBOOK* GetBookForPerk(RE::BGSPerk* perk) const;
    [[nodiscard]] RE::BGSPerk* GetPerkForBook(RE::TESForm* book) const;
    [[nodiscard]] bool IsBookOfPerk(RE::TESForm* book) const;
    [[nodiscard]] bool AddBookToRef(RE::TESObjectREFR* target, RE::BGSPerk* perk, std::int32_t count) const;
    [[nodiscard]] const std::string* GetCachedDescription(RE::TESObjectBOOK* book) const;

    bool ApplyBookPerk(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const;
    bool ApplyBookPerkAndConsume(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const;
    bool HandleBookMenuOpen(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const;
    void ReleaseBookForPerkKey(std::string_view perkKey);
    void ReleaseBooksForPlugin(std::string_view pluginName);
    std::uint32_t GiveAllBooksToPlayer() const;
private:
    BookManager() = default;

    RE::TESObjectBOOK* CreateBookForPerk(RE::BGSPerk* perk, const InternalFormInfo& info, RE::TESObjectBOOK* baseBook);
    bool RegisterBookForPerk(RE::TESObjectBOOK* book, RE::BGSPerk* perk, const InternalFormInfo& info, RE::TESObjectBOOK* baseBook);
    void RemoveBookRuntimeMapping(RE::FormID perkID);
    void ConfigureBookForPerk(RE::TESObjectBOOK* book, RE::BGSPerk* perk, const InternalFormInfo& info, RE::TESObjectBOOK* baseBook) const;
    void ConsumeBook(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const;
    
    RE::TESObjectBOOK* FindBaseBook() const;
    void CopyBookAppearance(RE::TESObjectBOOK* target, RE::TESObjectBOOK* source) const;

    bool _initialized = false;
    std::vector<RE::TESObjectBOOK*> _books;
    std::unordered_map<RE::FormID, RE::FormID> _bookToPerk;
    std::unordered_map<RE::FormID, RE::FormID> _perkToBook;
    std::unordered_map<RE::FormID, std::string> _bookDescriptions;
};
