#include "Papyrus.h"

#include "BookManager.h"
#include "logger.h"

namespace {
    constexpr std::string_view SCRIPT_NAME = "BookOfPerks";

    RE::TESForm* GetBookForPerk(RE::StaticFunctionTag*, RE::BGSPerk* perk) {
        return BookManager::GetSingleton()->GetBookForPerk(perk);
    }

    RE::BGSPerk* GetPerkForBook(RE::StaticFunctionTag*, RE::TESForm* book) {
        return BookManager::GetSingleton()->GetPerkForBook(book);
    }

    bool IsBookOfPerk(RE::StaticFunctionTag*, RE::TESForm* book) {
        return BookManager::GetSingleton()->IsBookOfPerk(book);
    }

    bool AddBookToRef(RE::StaticFunctionTag*, RE::TESObjectREFR* target, RE::BGSPerk* perk, std::int32_t count) {
        return BookManager::GetSingleton()->AddBookToRef(target, perk, count);
    }
}

namespace Papyrus {
    bool Register(RE::BSScript::IVirtualMachine* vm) {
        if (!vm) {
            return false;
        }

        vm->RegisterFunction("GetBookForPerk"sv, SCRIPT_NAME, GetBookForPerk);
        vm->RegisterFunction("GetPerkForBook"sv, SCRIPT_NAME, GetPerkForBook);
        vm->RegisterFunction("IsBookOfPerk"sv, SCRIPT_NAME, IsBookOfPerk);
        vm->RegisterFunction("AddBookToRef"sv, SCRIPT_NAME, AddBookToRef);

        logger::info("Papyrus API registrada.");
        return true;
    }
}
