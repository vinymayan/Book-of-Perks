#include "BookManager.h"

#include "BookSettings.h"
#include "DPFAPI.h"
#include "logger.h"

#include "RE/C/ConsoleLog.h"
#include "RE/F/FxDelegateArgs.h"
#include "RE/I/ItemRemoveReason.h"
#include "RE/S/SendUIMessage.h"
#include "RE/U/UIMessageQueue.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace {
    constexpr auto kDPFOwner = "BookOfPerks";
    constexpr std::string_view kGetAllSkillBooksCommand = "getallskillbooks";

    struct BookActivateHook {
        static bool thunk(RE::TESObjectBOOK* self, RE::TESObjectREFR* targetRef, RE::TESObjectREFR* activatorRef, std::uint8_t arg3, RE::TESBoundObject* object, std::int32_t targetCount) {
            const bool result = func(self, targetRef, activatorRef, arg3, object, targetCount);

            if (result && self && activatorRef) {
                BookManager::GetSingleton()->ApplyBookPerkAndConsume(self, activatorRef);
            }

            return result;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct BookReadHook {
        static bool thunk(RE::TESObjectBOOK* self, RE::TESObjectREFR* reader) {
            if (BookManager::GetSingleton()->IsBookOfPerk(self)) {
                BookManager::GetSingleton()->ApplyBookPerkAndConsume(self, reader);
                return true;
            }

            const bool result = func(self, reader);

            if (result && self && reader) {
                BookManager::GetSingleton()->ApplyBookPerkAndConsume(self, reader);
            }

            return result;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct BookMenuProcessHook {
        static RE::UI_MESSAGE_RESULTS thunk(RE::BookMenu* menu, RE::UIMessage& message) {
            if (message.type == RE::UI_MESSAGE_TYPE::kShow) {
                const auto book = RE::BookMenu::GetTargetForm();
                auto targetRef = RE::BookMenu::GetTargetReference();
                if (BookManager::GetSingleton()->HandleBookMenuOpen(book, targetRef.get())) {
                    if (auto queue = RE::UIMessageQueue::GetSingleton()) {
                        queue->AddMessage(RE::BookMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    }
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }
            }

            return func(menu, message);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    bool IsGetAllSkillBooksCommand(std::string_view command) {
        while (!command.empty() && std::isspace(static_cast<unsigned char>(command.front()))) {
            command.remove_prefix(1);
        }
        while (!command.empty() && std::isspace(static_cast<unsigned char>(command.back()))) {
            command.remove_suffix(1);
        }

        return command.size() == kGetAllSkillBooksCommand.size() &&
            std::equal(command.begin(), command.end(), kGetAllSkillBooksCommand.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            });
    }

    struct ConsoleCommandHook {
        static void thunk(RE::FxDelegateArgs* args) {
            if (args && args->GetArgCount() > 0 && (*args)[0].IsString()) {
                if (const auto command = (*args)[0].GetString(); command && IsGetAllSkillBooksCommand(command)) {
                    Manager::GetSingleton()->PopulateAllLists();
                    if (!DPF::GetAPI()) {
                        logger::warn("[BookManager] Console command getallskillbooks falhou: DPF API indisponivel.");
                        if (auto console = RE::ConsoleLog::GetSingleton()) {
                            console->Print("Book of Perks: DPF API indisponivel.");
                        }
                        return;
                    }

                    BookManager::GetSingleton()->Initialize();
                    const auto addedCount = BookManager::GetSingleton()->GiveAllBooksToPlayer();
                    if (auto console = RE::ConsoleLog::GetSingleton()) {
                        console->Print("Book of Perks: adicionados %u skill books ao player.", addedCount);
                    }
                    return;
                }
            }

            func(args);
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };

    std::string BuildEditorID(const InternalFormInfo& info) {
        std::string id = "BoP_Learn_";
        id += !info.editorID.empty() ? info.editorID : std::format("{:08X}", info.formID);

        for (auto& ch : id) {
            const bool valid = std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
            if (!valid) {
                ch = '_';
            }
        }

        return id;
    }

    std::string BuildDPFKey(RE::BGSPerk* perk, const InternalFormInfo& info) {
        auto key = std::format("perk:{}", BookSettings::MakePerkKey(perk));
        if (key == "perk:") {
            key = std::format("perk:{}", BookSettings::MakePerkKey(info));
        }
        return key;
    }

    std::string BuildDPFKey(std::string_view perkKey) {
        return std::format("perk:{}", perkKey);
    }

    void LogDPFFormID(std::string_view action, std::string_view key, RE::FormID formID, std::uint32_t localID) {
        logger::info("[BookManager] DPF {} owner '{}' key '{}' FormID {:08X} localID {:06X}.",
            action, kDPFOwner, key, formID, localID);
    }
}

BookManager* BookManager::GetSingleton() {
    static BookManager singleton;
    return &singleton;
}

void BookManager::Initialize() {
    if (_initialized) {
        return;
    }

    auto manager = Manager::GetSingleton();
    BookSettings::Load();
    for (const auto& perkKey : BookSettings::GetBlacklistedPerks()) {
        ReleaseBookForPerkKey(perkKey);
    }
    for (const auto& pluginName : BookSettings::GetBlacklistedPlugins()) {
        ReleaseBooksForPlugin(pluginName);
    }
    const auto& perks = manager->GetList("Perk");
    auto baseBook = FindBaseBook();

    if (!baseBook) {
        logger::error("[BookManager] Nenhum livro base encontrado. Livros de perks nao serao criados.");
        _initialized = true;
        return;
    }

    if (!DPF::GetAPI()) {
        logger::error("[BookManager] DPF API v{} nao encontrada. Livros de perks nao serao criados.",
            DPF::InterfaceVersion);
        return;
    }

    _books.reserve(perks.size());

    for (const auto& info : perks) {
        if (BookSettings::IsBlacklisted(info)) {
            continue;
        }

        auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(info.formID);
        if (!perk) {
            continue;
        }

        if (_perkToBook.contains(perk->GetFormID())) {
            continue;
        }

        if (auto book = CreateBookForPerk(perk, info, baseBook)) {
            RegisterBookForPerk(book, perk, info, baseBook);
        }
    }

    _initialized = true;
    logger::info("[BookManager] Registrados {} livros dinamicos para perks.", _books.size());
}

void BookManager::InstallHooks() {
    SKSE::AllocTrampoline(28);
    auto& trampoline = SKSE::GetTrampoline();

    REL::Relocation<std::uintptr_t> readFunc{ RELOCATION_ID(17439, 17842) };
    BookReadHook::func = trampoline.write_branch<5>(readFunc.address(), BookReadHook::thunk);

    REL::Relocation<std::uintptr_t> consoleExecuteFunc{ RELOCATION_ID(50157, 51084) };
    ConsoleCommandHook::func = trampoline.write_branch<5>(consoleExecuteFunc.address(), ConsoleCommandHook::thunk);

    REL::Relocation<std::uintptr_t> bookMenuVTable{ RE::VTABLE_BookMenu[0] };
    BookMenuProcessHook::func = bookMenuVTable.write_vfunc(0x4, BookMenuProcessHook::thunk);

    REL::Relocation<std::uintptr_t> vtbl{ RE::TESObjectBOOK::VTABLE[0] };
    BookActivateHook::func = vtbl.write_vfunc(0x37, BookActivateHook::thunk);
    logger::info("[BookManager] Hooks de leitura/menu/ativacao/console de livros instalados.");
}

void BookManager::RebuildDynamicBooks() {
    logger::info("[BookManager] Rebuild de livros dinamicos iniciado.");
    BookSettings::Load();
    _initialized = false;
    _books.clear();
    _bookToPerk.clear();
    _perkToBook.clear();
    _bookDescriptions.clear();
    Initialize();
}

void BookManager::Revert() {
    _initialized = false;
    _books.clear();
    _bookToPerk.clear();
    _perkToBook.clear();
    _bookDescriptions.clear();
}

RE::TESObjectBOOK* BookManager::GetBookForPerk(RE::BGSPerk* perk) const {
    if (!perk) {
        return nullptr;
    }

    auto it = _perkToBook.find(perk->GetFormID());
    if (it == _perkToBook.end()) {
        return nullptr;
    }

    return RE::TESForm::LookupByID<RE::TESObjectBOOK>(it->second);
}

RE::BGSPerk* BookManager::GetPerkForBook(RE::TESForm* book) const {
    if (!book) {
        return nullptr;
    }

    auto it = _bookToPerk.find(book->GetFormID());
    if (it == _bookToPerk.end()) {
        return nullptr;
    }

    return RE::TESForm::LookupByID<RE::BGSPerk>(it->second);
}

bool BookManager::IsBookOfPerk(RE::TESForm* book) const {
    return book && _bookToPerk.contains(book->GetFormID());
}

bool BookManager::AddBookToRef(RE::TESObjectREFR* target, RE::BGSPerk* perk, std::int32_t count) const {
    if (!target || count <= 0) {
        return false;
    }

    auto book = GetBookForPerk(perk);
    if (!book) {
        return false;
    }

    target->AddObjectToContainer(book, nullptr, count, nullptr);
    return true;
}

const std::string* BookManager::GetCachedDescription(RE::TESObjectBOOK* book) const {
    if (!book) {
        return nullptr;
    }

    auto it = _bookDescriptions.find(book->GetFormID());
    return it != _bookDescriptions.end() ? &it->second : nullptr;
}

bool BookManager::ApplyBookPerk(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const {
    auto perk = GetPerkForBook(book);
    auto actor = reader ? reader->As<RE::Actor>() : nullptr;

    if (!perk || !actor) {
        return false;
    }

    if (actor->HasPerk(perk)) {
        return false;
    }

    actor->AddPerk(perk, 0);
    logger::info("[BookManager] Perk {:08X} aplicado ao ator {:08X} via livro {:08X}.",
        perk->GetFormID(), actor->GetFormID(), book->GetFormID());
    return true;
}

bool BookManager::ApplyBookPerkAndConsume(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const {
    if (!ApplyBookPerk(book, reader)) {
        return false;
    }

    ConsumeBook(book, reader);
    return true;
}

bool BookManager::HandleBookMenuOpen(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const {
    if (!IsBookOfPerk(book)) {
        return false;
    }

    auto actorReader = reader && reader->As<RE::Actor>() ? reader : static_cast<RE::TESObjectREFR*>(RE::PlayerCharacter::GetSingleton());

    ApplyBookPerkAndConsume(book, actorReader);
    logger::info("[BookManager] BookMenu bloqueado para livro de perk {:08X}.", book->GetFormID());
    return true;
}

void BookManager::ReleaseBookForPerkKey(std::string_view perkKey) {
    if (perkKey.empty()) {
        return;
    }

    RE::FormID perkID = 0;
    try {
        perkID = FormUtil::FormIDFromString(std::string(perkKey));
    } catch (...) {
        logger::warn("[BookManager] Perk key '{}' invalida para release.", perkKey);
    }
    RemoveBookRuntimeMapping(perkID);

    if (auto dpf = DPF::GetAPI()) {
        const auto dpfKey = BuildDPFKey(perkKey);
        if (dpf->ReleaseByOwnerKey(kDPFOwner, dpfKey.c_str())) {
            logger::info("[BookManager] DPF release owner '{}' key '{}'.", kDPFOwner, dpfKey);
        }
    }
}

void BookManager::ReleaseBooksForPlugin(std::string_view pluginName) {
    for (const auto& info : Manager::GetSingleton()->GetList("Perk")) {
        if (info.pluginName == pluginName) {
            ReleaseBookForPerkKey(BookSettings::MakePerkKey(info));
        }
    }
}

RE::TESObjectBOOK* BookManager::CreateBookForPerk(RE::BGSPerk* perk, const InternalFormInfo& info, RE::TESObjectBOOK* baseBook) {
    if (!perk || !baseBook) {
        return nullptr;
    }

    if (BookSettings::IsBlacklisted(info)) {
        return nullptr;
    }

    auto dpf = DPF::GetAPI();
    if (!dpf) {
        logger::error("[BookManager] DPF API v{} nao encontrada. Livro para perk {:08X} nao sera criado.",
            DPF::InterfaceVersion, perk->GetFormID());
        return nullptr;
    }

    const auto key = BuildDPFKey(perk, info);
    std::uint32_t localID = 0;
    bool existed = false;
    auto createdForm = dpf->GetOrCreateByOwnerKey(
        kDPFOwner,
        key.c_str(),
        static_cast<std::uint32_t>(RE::FormType::Book),
        &localID,
        &existed);
    auto book = createdForm ? createdForm->As<RE::TESObjectBOOK>() : nullptr;
    if (!book) {
        logger::error("[BookManager] DPF falhou ao resolver/criar livro para perk {:08X} key '{}'.",
            perk->GetFormID(), key);
        return nullptr;
    }

    LogDPFFormID(existed ? "recovered" : "created", key, book->GetFormID(), localID);
    logger::info("[BookManager] Mapping perk {:08X} '{}' -> book {:08X}.",
        perk->GetFormID(), FormUtil::NormalizeFormID(perk), book->GetFormID());
    ConfigureBookForPerk(book, perk, info, baseBook);
    return book;
}

bool BookManager::RegisterBookForPerk(RE::TESObjectBOOK* book, RE::BGSPerk* perk, const InternalFormInfo& info, RE::TESObjectBOOK* baseBook) {
    if (!book || !perk) {
        return false;
    }

    const auto bookID = book->GetFormID();
    const auto perkID = perk->GetFormID();
    if (_bookToPerk.contains(bookID) || _perkToBook.contains(perkID)) {
        return false;
    }

    ConfigureBookForPerk(book, perk, info, baseBook);

    _books.push_back(book);
    _bookToPerk[bookID] = perkID;
    _perkToBook[perkID] = bookID;
    const auto perkKey = BookSettings::MakePerkKey(perk);
    const auto overrideData = BookSettings::GetOverride(perkKey);
    _bookDescriptions[bookID] = overrideData && !overrideData->description.empty() ? overrideData->description : info.description;
    return true;
}

void BookManager::RemoveBookRuntimeMapping(RE::FormID perkID) {
    if (!perkID) {
        return;
    }

    const auto perkIt = _perkToBook.find(perkID);
    if (perkIt == _perkToBook.end()) {
        return;
    }

    const auto bookID = perkIt->second;
    _perkToBook.erase(perkIt);
    _bookToPerk.erase(bookID);
    _bookDescriptions.erase(bookID);
    std::erase_if(_books, [bookID](RE::TESObjectBOOK* book) {
        return !book || book->GetFormID() == bookID;
    });
}

void BookManager::ConfigureBookForPerk(RE::TESObjectBOOK* book, RE::BGSPerk* perk, const InternalFormInfo& info, RE::TESObjectBOOK* baseBook) const {
    if (!book || !perk) {
        return;
    }

    CopyBookAppearance(book, baseBook);

    const auto perkKey = BookSettings::MakePerkKey(perk);
    const auto overrideData = BookSettings::GetOverride(perkKey);

    const auto bookName = overrideData && !overrideData->name.empty() ? overrideData->name : std::format("Learn {}", info.name);
    book->TESFullName::SetFullName(bookName.c_str());
    const auto editorID = BuildEditorID(info);
    book->SetFormEditorID(editorID.c_str());

    if (overrideData && !overrideData->modelPath.empty()) {
        book->SetModel(overrideData->modelPath.c_str());
    }
    if (overrideData && overrideData->weight >= 0.0f) {
        book->weight = overrideData->weight;
    }
    if (overrideData && overrideData->value >= 0) {
        book->value = overrideData->value;
    }

    book->data.flags.reset();
    book->data.type = RE::OBJ_BOOK::Type::kBookTome;
    book->data.teaches.spell = nullptr;

    book->TESDescription::fileOffset = perk->TESDescription::fileOffset;
    book->TESDescription::descriptionText = perk->TESDescription::descriptionText;
    book->itemCardDescription.fileOffset = perk->TESDescription::fileOffset;
    book->itemCardDescription.descriptionText = perk->TESDescription::descriptionText;
}

void BookManager::ConsumeBook(RE::TESObjectBOOK* book, RE::TESObjectREFR* reader) const {
    if (!book || !reader) {
        return;
    }

    auto actor = reader->As<RE::Actor>();
    if (!actor) {
        return;
    }

    actor->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
    RE::SendUIMessage::SendInventoryUpdateMessage(actor, nullptr);
    logger::info("[BookManager] Livro {:08X} consumido do ator {:08X}.", book->GetFormID(), actor->GetFormID());
}

std::uint32_t BookManager::GiveAllBooksToPlayer() const {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        logger::warn("[BookManager] Player indisponivel. Livros de perks nao foram adicionados ao inventario.");
        return 0;
    }

    std::uint32_t addedCount = 0;
    for (auto book : _books) {
        if (!book) {
            continue;
        }

        if (player->GetItemCount(book) > 0) {
            continue;
        }

        player->AddObjectToContainer(book, nullptr, 1, nullptr);
        ++addedCount;
    }

    logger::info("[BookManager] Adicionados {} livros de perks ao inventario do player.", addedCount);
    return addedCount;
}

RE::TESObjectBOOK* BookManager::FindBaseBook() const {
    auto dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        return nullptr;
    }

    auto& books = dataHandler->GetFormArray<RE::TESObjectBOOK>();
    const auto baseBookKey = BookSettings::GetBaseBookKey();
    if (!baseBookKey.empty()) {
        for (auto book : books) {
            if (!book || book->IsDeleted() || book->IsIgnored()) {
                continue;
            }

            if (BookSettings::MakeFormKey(book) == baseBookKey) {
                logger::info("[BookManager] Usando livro base configurado '{}' ({:08X}).", baseBookKey, book->GetFormID());
                return book;
            }
        }

        logger::warn("[BookManager] Livro base configurado '{}' nao encontrado. Usando fallback automatico.", baseBookKey);
    }

    for (auto book : books) {
        if (!book || book->IsDeleted() || book->IsIgnored()) {
            continue;
        }

        if (book->IsBookTome() && book->CanBeTaken() && book->inventoryModel) {
            return book;
        }
    }

    for (auto book : books) {
        if (book && !book->IsDeleted() && !book->IsIgnored()) {
            return book;
        }
    }

    return nullptr;
}

void BookManager::CopyBookAppearance(RE::TESObjectBOOK* target, RE::TESObjectBOOK* source) const {
    if (!target || !source) {
        return;
    }

    target->model = source->model;
    target->textures = source->textures;
    target->addons = source->addons;
    target->numTextures = source->numTextures;
    target->numAddons = source->numAddons;
    target->alternateTextures = source->alternateTextures;
    target->numAlternateTextures = source->numAlternateTextures;

    target->TESIcon::textureName = source->TESIcon::textureName;
    target->value = source->value;
    target->weight = source->weight;
    target->BGSDestructibleObjectForm::data = source->BGSDestructibleObjectForm::data;
    target->BGSMessageIcon::icon.textureName = source->BGSMessageIcon::icon.textureName;
    target->pickupSound = source->pickupSound;
    target->putdownSound = source->putdownSound;
    target->keywords = source->keywords;
    target->numKeywords = source->numKeywords;
    target->inventoryModel = source->inventoryModel;
    target->data = source->data;
}
