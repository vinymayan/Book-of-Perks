#pragma once

#include "DFGAPI.h"
#include "Manager.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class BookManager {
public:
    struct DynamicFormSlot {
        std::uint32_t pluginNumber{};
        std::uint32_t localID{};
        std::string pluginName;
    };

    static BookManager* GetSingleton();

    void Initialize();
    void InstallHooks();
    void RebuildDynamicBooks();
    void Revert();

    [[nodiscard]] bool IsInitializing() const;
    [[nodiscard]] RE::TESObjectBOOK* GetBookForPerk(RE::BGSPerk* perk) const;
    [[nodiscard]] RE::BGSPerk* GetPerkForBook(RE::TESForm* book) const;
    [[nodiscard]] bool IsBookOfPerk(RE::TESForm* book) const;
    [[nodiscard]] bool AddBookToRef(RE::TESObjectREFR* target, RE::BGSPerk* perk, std::int32_t count) const;
    [[nodiscard]] const std::string* GetCachedDescription(RE::TESObjectBOOK* book) const;
    [[nodiscard]] std::optional<DynamicFormSlot> GetDynamicFormSlot(RE::TESObjectBOOK* book) const;

    bool ApplyBookPerk(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const;
    bool ApplyBookPerkAndConsume(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const;
    bool HandleBookMenuOpen(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const;
    std::uint32_t GiveAllBooksToPlayer() const;

private:
    struct OperationContext;
    struct BatchContext;

    BookManager() = default;

    static void OnCreateBatchResult(const DFG::BatchOperationResult* result, void* userData);
    static void OnUpdateBatchResult(const DFG::BatchOperationResult* result, void* userData);
    static void OnDeleteBatchResult(const DFG::BatchOperationResult* result, void* userData);
    static void OnLookupBatchResult(const DFG::BatchLookupResult* result, void* userData);

    bool QueueCreateBatch(std::vector<OperationContext> operations);
    bool QueueUpdateBatch(std::vector<OperationContext> operations);
    bool QueueDeleteBatch(std::vector<OperationContext> operations);
    bool QueueLookupBatch(std::vector<OperationContext> operations);
    void HandleBookResult(const DFG::FormOperationResult& result, const OperationContext& context);
    void HandleBookLookupResult(const DFG::FormLookupResult& result, const OperationContext& context);
    void FinishBatchOperation();
    void FinishBatch();
    void ClearRuntimeMappings();

    bool RegisterBookForPerk(
        RE::TESObjectBOOK* book,
        RE::BGSPerk* perk,
        const InternalFormInfo& info,
        RE::TESObjectBOOK* baseBook,
        DynamicFormSlot slot,
        std::string_view resolution);
    void RemoveBookRuntimeMapping(RE::FormID perkID);
    void ConfigureBookForPerk(
        RE::TESObjectBOOK* book,
        RE::BGSPerk* perk) const;
    void ConsumeBook(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const;

    RE::TESObjectBOOK* FindBaseBook() const;
    bool _initialized = false;
    bool _initializing = false;
    bool _rebuildRequested = false;
    bool _legacyEditorIDsChecked = false;
    std::uint32_t _pendingBatches = 0;
    RE::FormID _baseBookID = 0;
    std::vector<RE::TESObjectBOOK*> _books;
    std::unordered_map<RE::FormID, RE::FormID> _bookToPerk;
    std::unordered_map<RE::FormID, RE::FormID> _perkToBook;
    std::unordered_map<RE::FormID, std::string> _bookDescriptions;
    std::unordered_map<RE::FormID, DynamicFormSlot> _bookSlots;
};
