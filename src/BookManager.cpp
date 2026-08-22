#include "BookManager.h"

#include "BookSettings.h"
#include "logger.h"

#include "RE/I/ItemRemoveReason.h"
#include "RE/S/SendUIMessage.h"
#include "RE/U/UIMessageQueue.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <string>
#include <string_view>
#include <unordered_set>

namespace {
    constexpr auto kDFGRequester = "BookOfPerks";
    constexpr auto kDFGPackage = "Book of Perks";

    struct BookActivateHook {
        static bool thunk(
            RE::TESObjectBOOK* self,
            RE::TESObjectREFR* targetRef,
            RE::TESObjectREFR* activatorRef,
            std::uint8_t arg3,
            RE::TESBoundObject* object,
            std::int32_t targetCount) {
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

    void WriteFormRef(
        rapidjson::Writer<rapidjson::StringBuffer>& writer,
        const char* key,
        RE::TESForm* form) {
        writer.Key(key);
        if (!form) {
            writer.Null();
            return;
        }

        const auto formID = FormUtil::NormalizeFormID(form);
        if (formID.empty()) {
            writer.Null();
            return;
        }

        writer.StartObject();
        writer.Key("formID");
        writer.String(formID.c_str(), static_cast<rapidjson::SizeType>(formID.size()));
        writer.EndObject();
    }

    void WriteBookKeywords(
        rapidjson::Writer<rapidjson::StringBuffer>& writer,
        RE::TESObjectBOOK* book) {
        writer.Key("keywords");
        writer.StartArray();
        if (book) {
            book->ForEachKeyword([&writer](RE::BGSKeyword* keyword) {
                const auto formID = FormUtil::NormalizeFormID(keyword);
                if (!formID.empty()) {
                    writer.StartObject();
                    writer.Key("formID");
                    writer.String(formID.c_str(), static_cast<rapidjson::SizeType>(formID.size()));
                    writer.EndObject();
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }
        writer.EndArray();
    }

    std::string BuildBookJson(
        const InternalFormInfo& info,
        RE::TESObjectBOOK* baseBook,
        const bool create) {
        const auto perkKey = BookSettings::MakePerkKey(info);
        const auto overrideData = BookSettings::GetOverride(perkKey);
        const auto name = overrideData && !overrideData->name.empty() ?
            overrideData->name :
            std::format("Learn {}", info.name);
        const auto description = overrideData && !overrideData->description.empty() ?
            overrideData->description :
            info.description;
        const auto modelPath = overrideData && !overrideData->modelPath.empty() ?
            overrideData->modelPath :
            std::string(baseBook && baseBook->GetModel() ? baseBook->GetModel() : "");
        const auto weight = overrideData && overrideData->weight >= 0.0F ?
            overrideData->weight :
            (baseBook ? baseBook->weight : 0.0F);
        const auto value = overrideData && overrideData->value >= 0 ?
            overrideData->value :
            (baseBook ? baseBook->value : 0);

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject();
        if (create) {
            writer.Key("formKind");
            writer.String("Book");
            writer.Key("sourceSignature");
            writer.String("BOOK");
            writer.Key("editorId");
            const auto editorID = BookSettings::MakeBookEditorID(info);
            writer.String(editorID.c_str(), static_cast<rapidjson::SizeType>(editorID.size()));
        }
        writer.Key("fullName");
        writer.String(name.c_str(), static_cast<rapidjson::SizeType>(name.size()));
        writer.Key("description");
        writer.String(description.c_str(), static_cast<rapidjson::SizeType>(description.size()));
        writer.Key("modelPath");
        writer.String(modelPath.c_str(), static_cast<rapidjson::SizeType>(modelPath.size()));
        writer.Key("itemValue");
        writer.Int(value);
        writer.Key("itemWeight");
        writer.Double(weight);
        const auto inventoryIcon = baseBook ? baseBook->TESIcon::textureName.c_str() : "";
        writer.Key("inventoryIcon");
        writer.String(inventoryIcon ? inventoryIcon : "");
        const auto messageIcon = baseBook ? baseBook->BGSMessageIcon::icon.textureName.c_str() : "";
        writer.Key("messageIcon");
        writer.String(messageIcon ? messageIcon : "");
        WriteFormRef(writer, "pickupSound", baseBook ? baseBook->pickupSound : nullptr);
        WriteFormRef(writer, "putdownSound", baseBook ? baseBook->putdownSound : nullptr);
        WriteBookKeywords(writer, baseBook);
        writer.Key("bookFlags");
        writer.Uint(0);
        writer.Key("bookType");
        writer.Uint(static_cast<std::uint32_t>(RE::OBJ_BOOK::Type::kBookTome));
        writer.Key("teachesActorValue");
        writer.Int(-1);
        writer.EndObject();
        return { buffer.GetString(), buffer.GetSize() };
    }

    bool BookJsonNeedsUpdate(
        const char* currentJson,
        std::uint32_t currentJsonLength,
        std::string_view expectedPatch) {
        if (!currentJson || currentJsonLength == 0 || expectedPatch.empty()) {
            return true;
        }

        rapidjson::Document current;
        current.Parse(currentJson, currentJsonLength);
        rapidjson::Document expected;
        expected.Parse(expectedPatch.data(), expectedPatch.size());
        if (current.HasParseError() || expected.HasParseError() ||
            !current.IsObject() || !expected.IsObject()) {
            return true;
        }

        for (auto member = expected.MemberBegin(); member != expected.MemberEnd(); ++member) {
            const auto currentIt = current.FindMember(member->name.GetString());
            if (currentIt == current.MemberEnd()) {
                if (member->value.IsNull()) {
                    continue;
                }
                return true;
            }

            const auto& currentValue = currentIt->value;
            const auto& expectedValue = member->value;
            if (expectedValue.IsString()) {
                if (!currentValue.IsString() ||
                    std::string_view(currentValue.GetString(), currentValue.GetStringLength()) !=
                        std::string_view(expectedValue.GetString(), expectedValue.GetStringLength())) {
                    return true;
                }
            } else if (expectedValue.IsNumber()) {
                if (!currentValue.IsNumber()) {
                    return true;
                }
                const auto tolerance =
                    std::string_view(member->name.GetString(), member->name.GetStringLength()) == "itemWeight" ?
                    0.0001 :
                    0.0;
                if (std::abs(currentValue.GetDouble() - expectedValue.GetDouble()) > tolerance) {
                    return true;
                }
            } else if (currentValue != expectedValue) {
                return true;
            }
        }
        return false;
    }

    std::string MakeLegacyBookEditorID(std::string_view perkKey) {
        const auto separator = perkKey.rfind('|');
        const auto pluginName =
            separator == std::string_view::npos ? perkKey : perkKey.substr(0, separator);
        const auto localID =
            separator == std::string_view::npos ? std::string_view{ "0" } : perkKey.substr(separator + 1);

        std::string pluginPart;
        pluginPart.reserve((std::min)(pluginName.size(), std::size_t{ 48 }));
        for (const auto ch : pluginName) {
            if (pluginPart.size() == 48) {
                break;
            }
            pluginPart.push_back(std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_');
        }

        std::uint32_t pluginHash = 2166136261u;
        for (const auto ch : pluginName) {
            pluginHash ^= static_cast<unsigned char>(ch);
            pluginHash *= 16777619u;
        }

        std::string normalizedLocalID(localID);
        std::ranges::transform(normalizedLocalID, normalizedLocalID.begin(), [](const unsigned char ch) {
            return std::isalnum(ch) ? static_cast<char>(std::toupper(ch)) : '_';
        });
        return std::format("BoP_Learn_{}_{:08X}_{}", pluginPart, pluginHash, normalizedLocalID);
    }

    bool IsGeneratedBook(RE::TESObjectBOOK* book) {
        if (!book) {
            return false;
        }
        const auto editorID = book->GetFormEditorID();
        return editorID && std::string_view(editorID).starts_with("BoP_Learn_");
    }
}

struct BookManager::OperationContext {
    RE::FormID perkID{};
    bool deleteIfFound{ false };
    std::string perkKey;
    std::string editorID;
    std::string createJson;
    std::string updateJson;
};

struct BookManager::BatchContext {
    std::vector<OperationContext> operations;
};

BookManager* BookManager::GetSingleton() {
    static BookManager singleton;
    return &singleton;
}

void BookManager::Initialize() {
    if (_initialized || _initializing) {
        return;
    }

    auto dfg = DFG::GetAPI();
    if (!dfg || !dfg->IsReady()) {
        logger::warn("[BookManager] DFG API v{} ainda nao esta pronta. Initialize adiado.",
            DFG::InterfaceVersion);
        return;
    }

    BookSettings::Load();
    const auto& perks = Manager::GetSingleton()->GetList("Perk");
    auto baseBook = FindBaseBook();

    ClearRuntimeMappings();
    _baseBookID = baseBook ? baseBook->GetFormID() : 0;
    _initializing = true;
    _pendingBatches = 0;
    _books.reserve(perks.size());

    std::unordered_set<std::string> deletes;
    for (const auto& perkKey : BookSettings::GetBlacklistedPerks()) {
        deletes.insert(perkKey);
    }
    for (const auto& info : perks) {
        if (BookSettings::IsPluginBlacklisted(info.pluginName)) {
            deletes.insert(BookSettings::MakePerkKey(info));
        }
    }

    const bool checkLegacyEditorIDs = !_legacyEditorIDsChecked;
    std::vector<OperationContext> lookupOperations;
    lookupOperations.reserve(
        deletes.size() + perks.size() * (checkLegacyEditorIDs ? 2 : 1));
    for (const auto& perkKey : deletes) {
        OperationContext context;
        context.deleteIfFound = true;
        context.perkKey = perkKey;
        try {
            context.perkID = FormUtil::FormIDFromString(perkKey);
        } catch (...) {
            logger::debug("[BookManager] Perk key '{}' nao possui FormID runtime resolvivel.", perkKey);
        }
        if (const auto info = Manager::GetSingleton()->GetInfoByID("Perk", context.perkID)) {
            context.editorID = BookSettings::MakeBookEditorID(*info);
        } else {
            context.editorID = BookSettings::MakeBookEditorID(perkKey);
        }
        RemoveBookRuntimeMapping(context.perkID);

        if (checkLegacyEditorIDs) {
            OperationContext legacyContext;
            legacyContext.deleteIfFound = true;
            legacyContext.perkID = context.perkID;
            legacyContext.perkKey = perkKey;
            legacyContext.editorID = MakeLegacyBookEditorID(perkKey);
            lookupOperations.push_back(std::move(legacyContext));
        }
        lookupOperations.push_back(std::move(context));
    }

    if (!baseBook) {
        logger::error("[BookManager] Nenhum livro base encontrado. Livros de perks nao serao criados.");
    } else {
        for (const auto& info : perks) {
            if (BookSettings::IsBlacklisted(info)) {
                continue;
            }

            auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(info.formID);
            if (perk) {
                const auto perkKey = BookSettings::MakePerkKey(info);
                if (checkLegacyEditorIDs) {
                    OperationContext legacyContext;
                    legacyContext.deleteIfFound = true;
                    legacyContext.perkID = perk->GetFormID();
                    legacyContext.perkKey = perkKey;
                    legacyContext.editorID = MakeLegacyBookEditorID(perkKey);
                    lookupOperations.push_back(std::move(legacyContext));
                }

                OperationContext context;
                context.perkID = perk->GetFormID();
                context.perkKey = perkKey;
                context.editorID = BookSettings::MakeBookEditorID(info);
                context.createJson = BuildBookJson(info, baseBook, true);
                context.updateJson = BuildBookJson(info, baseBook, false);
                lookupOperations.push_back(std::move(context));
            }
        }
    }
    if (QueueLookupBatch(std::move(lookupOperations))) {
        _legacyEditorIDsChecked = true;
    }

    logger::info("[BookManager] Inicializacao DFG iniciada com {} batches.", _pendingBatches);
    if (_pendingBatches == 0) {
        FinishBatch();
    }
}

void BookManager::InstallHooks() {
    SKSE::AllocTrampoline(14);
    auto& trampoline = SKSE::GetTrampoline();

    REL::Relocation<std::uintptr_t> readFunc{ RELOCATION_ID(17439, 17842) };
    BookReadHook::func = trampoline.write_branch<5>(readFunc.address(), BookReadHook::thunk);

    REL::Relocation<std::uintptr_t> bookMenuVTable{ RE::VTABLE_BookMenu[0] };
    BookMenuProcessHook::func = bookMenuVTable.write_vfunc(0x4, BookMenuProcessHook::thunk);

    REL::Relocation<std::uintptr_t> vtbl{ RE::TESObjectBOOK::VTABLE[0] };
    BookActivateHook::func = vtbl.write_vfunc(0x37, BookActivateHook::thunk);
    logger::info("[BookManager] Hooks de leitura/menu/ativacao de livros instalados.");
}

void BookManager::RebuildDynamicBooks() {
    if (_initializing) {
        _rebuildRequested = true;
        logger::info("[BookManager] Rebuild solicitado durante batch DFG; execucao adiada.");
        return;
    }

    logger::info("[BookManager] Rebuild de livros dinamicos iniciado.");
    _initialized = false;
    ClearRuntimeMappings();
    Initialize();
}

void BookManager::Revert() {
    _initialized = false;
    _initializing = false;
    _rebuildRequested = false;
    _pendingBatches = 0;
    _baseBookID = 0;
    ClearRuntimeMappings();
}

bool BookManager::IsInitializing() const {
    return _initializing;
}

bool BookManager::QueueCreateBatch(std::vector<OperationContext> operations) {
    auto dfg = DFG::GetAPI();
    if (!dfg || operations.empty()) {
        return false;
    }

    auto context = std::make_unique<BatchContext>();
    context->operations = std::move(operations);

    std::vector<DFG::CreateFormRequest> requests;
    requests.reserve(context->operations.size());
    for (const auto& operation : context->operations) {
        DFG::CreateFormRequest request;
        request.requester = kDFGRequester;
        request.packageName = kDFGPackage;
        request.formJson = operation.createJson.c_str();
        requests.push_back(request);
    }

    DFG::CreateFormsRequest request;
    request.requests = requests.data();
    request.requestCount = static_cast<std::uint32_t>(requests.size());
    if (!dfg->QueueCreateForms(&request, OnCreateBatchResult, context.get())) {
        logger::error("[BookManager] DFG recusou batch create com {} forms.", requests.size());
        return false;
    }

    ++_pendingBatches;
    context.release();
    return true;
}

bool BookManager::QueueUpdateBatch(std::vector<OperationContext> operations) {
    auto dfg = DFG::GetAPI();
    if (!dfg || operations.empty()) {
        return false;
    }

    auto context = std::make_unique<BatchContext>();
    context->operations = std::move(operations);

    std::vector<DFG::UpdateFormRequest> requests;
    requests.reserve(context->operations.size());
    for (const auto& operation : context->operations) {
        DFG::UpdateFormRequest request;
        request.requester = kDFGRequester;
        request.editorId = operation.editorID.c_str();
        request.patchJson = operation.updateJson.c_str();
        requests.push_back(request);
    }

    DFG::UpdateFormsRequest request;
    request.requests = requests.data();
    request.requestCount = static_cast<std::uint32_t>(requests.size());
    if (!dfg->QueueUpdateForms(&request, OnUpdateBatchResult, context.get())) {
        logger::error("[BookManager] DFG recusou batch update com {} forms.", requests.size());
        return false;
    }

    ++_pendingBatches;
    context.release();
    return true;
}

bool BookManager::QueueDeleteBatch(std::vector<OperationContext> operations) {
    auto dfg = DFG::GetAPI();
    if (!dfg || operations.empty()) {
        return false;
    }

    auto context = std::make_unique<BatchContext>();
    context->operations = std::move(operations);

    std::vector<DFG::DeleteFormRequest> requests;
    requests.reserve(context->operations.size());
    for (const auto& operation : context->operations) {
        DFG::DeleteFormRequest request;
        request.requester = kDFGRequester;
        request.editorId = operation.editorID.c_str();
        requests.push_back(request);
    }

    DFG::DeleteFormsRequest request;
    request.requests = requests.data();
    request.requestCount = static_cast<std::uint32_t>(requests.size());
    if (!dfg->QueueDeleteForms(&request, OnDeleteBatchResult, context.get())) {
        logger::error("[BookManager] DFG recusou batch delete com {} forms.", requests.size());
        return false;
    }

    ++_pendingBatches;
    context.release();
    return true;
}

bool BookManager::QueueLookupBatch(std::vector<OperationContext> operations) {
    auto dfg = DFG::GetAPI();
    if (!dfg || operations.empty()) {
        return false;
    }

    auto context = std::make_unique<BatchContext>();
    context->operations = std::move(operations);

    std::vector<DFG::LookupFormRequest> requests;
    requests.reserve(context->operations.size());
    for (const auto& operation : context->operations) {
        DFG::LookupFormRequest request;
        request.requester = kDFGRequester;
        request.editorId = operation.editorID.c_str();
        requests.push_back(request);
    }

    DFG::LookupFormsRequest request;
    request.requests = requests.data();
    request.requestCount = static_cast<std::uint32_t>(requests.size());
    if (!dfg->QueueLookupForms(&request, OnLookupBatchResult, context.get())) {
        logger::error("[BookManager] DFG recusou batch lookup com {} forms.", requests.size());
        return false;
    }

    ++_pendingBatches;
    context.release();
    return true;
}

void BookManager::OnLookupBatchResult(const DFG::BatchLookupResult* result, void* userData) {
    std::unique_ptr<BatchContext> context(static_cast<BatchContext*>(userData));
    auto manager = GetSingleton();
    std::vector<OperationContext> creates;
    std::vector<OperationContext> updates;
    std::vector<OperationContext> deletes;
    std::size_t reused = 0;

    if (result && context && (result->resultCount == 0 || result->results)) {
        const auto count = (std::min)(
            static_cast<std::size_t>(result->resultCount),
            context->operations.size());
        creates.reserve(result->missingCount);
        updates.reserve(result->foundCount);
        deletes.reserve(result->foundCount);

        for (std::size_t i = 0; i < count; ++i) {
            const auto& item = result->results[i];
            auto& operation = context->operations[i];
            if (item.status != DFG::Status::Success) {
                logger::error("[BookManager] DFG lookup '{}' falhou: status {} '{}'.",
                    operation.editorID,
                    static_cast<std::uint32_t>(item.status),
                    item.error);
                continue;
            }

            if (!item.exists) {
                if (!operation.deleteIfFound) {
                    creates.push_back(std::move(operation));
                }
                continue;
            }

            const bool ownedBook =
                std::string_view(item.packageName) == kDFGPackage &&
                std::string_view(item.sourceSignature) == "BOOK";
            if (!ownedBook) {
                logger::error(
                    "[BookManager] DFG lookup '{}' encontrou package '{}' tipo '{}'/assinatura '{}'; "
                    "registro nao sera alterado.",
                    operation.editorID,
                    item.packageName,
                    item.formKind,
                    item.sourceSignature);
                continue;
            }

            if (operation.deleteIfFound) {
                deletes.push_back(std::move(operation));
            } else if (!item.form ||
                       BookJsonNeedsUpdate(item.formJson, item.formJsonLength, operation.updateJson)) {
                updates.push_back(std::move(operation));
            } else {
                manager->HandleBookLookupResult(item, operation);
                ++reused;
            }
        }

        if (count != context->operations.size()) {
            logger::error("[BookManager] Batch lookup DFG retornou {} de {} resultados.",
                count, context->operations.size());
        }
        logger::info(
            "[BookManager] Batch lookup DFG: {} encontrados, {} ausentes, {} falhas; "
            "{} reutilizados, {} updates, {} creates, {} deletes.",
            result->foundCount,
            result->missingCount,
            result->failureCount,
            reused,
            updates.size(),
            creates.size(),
            deletes.size());
    } else {
        logger::error("[BookManager] Callback batch lookup DFG recebeu resultado invalido.");
    }

    manager->QueueUpdateBatch(std::move(updates));
    manager->QueueCreateBatch(std::move(creates));
    manager->QueueDeleteBatch(std::move(deletes));
    manager->FinishBatchOperation();
}

void BookManager::OnCreateBatchResult(const DFG::BatchOperationResult* result, void* userData) {
    std::unique_ptr<BatchContext> context(static_cast<BatchContext*>(userData));
    auto manager = GetSingleton();
    std::vector<OperationContext> retryUpdates;
    if (result && context && (result->resultCount == 0 || result->results)) {
        const auto count = (std::min)(
            static_cast<std::size_t>(result->resultCount),
            context->operations.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& item = result->results[i];
            auto& operation = context->operations[i];
            if (item.status == DFG::Status::Success) {
                manager->HandleBookResult(item, operation);
            } else if (item.status == DFG::Status::EditorIdAlreadyExists) {
                retryUpdates.push_back(std::move(operation));
            } else {
                logger::error("[BookManager] DFG create '{}' falhou: status {} '{}'.",
                    operation.editorID,
                    static_cast<std::uint32_t>(item.status),
                    item.error);
            }
        }
        if (count != context->operations.size()) {
            logger::error("[BookManager] Batch create DFG retornou {} de {} resultados.",
                count, context->operations.size());
        }
        logger::info("[BookManager] Batch create DFG: {} sucessos, {} falhas.",
            result->successCount, result->failureCount);
    } else {
        logger::error("[BookManager] Callback batch create DFG recebeu resultado invalido.");
    }

    manager->QueueUpdateBatch(std::move(retryUpdates));
    manager->FinishBatchOperation();
}

void BookManager::OnUpdateBatchResult(const DFG::BatchOperationResult* result, void* userData) {
    std::unique_ptr<BatchContext> context(static_cast<BatchContext*>(userData));
    auto manager = GetSingleton();
    std::vector<OperationContext> creates;
    if (result && context && (result->resultCount == 0 || result->results)) {
        const auto count = (std::min)(
            static_cast<std::size_t>(result->resultCount),
            context->operations.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& item = result->results[i];
            auto& operation = context->operations[i];
            if (item.status == DFG::Status::Success) {
                manager->HandleBookResult(item, operation);
            } else if (item.status == DFG::Status::EditorIdNotFound) {
                creates.push_back(std::move(operation));
            } else {
                logger::error("[BookManager] DFG update '{}' falhou: status {} '{}'.",
                    operation.editorID,
                    static_cast<std::uint32_t>(item.status),
                    item.error);
            }
        }
        if (count != context->operations.size()) {
            logger::error("[BookManager] Batch update DFG retornou {} de {} resultados.",
                count, context->operations.size());
        }
        logger::info("[BookManager] Batch update DFG: {} sucessos, {} falhas; {} creates necessarios.",
            result->successCount, result->failureCount, creates.size());
    } else {
        logger::error("[BookManager] Callback batch update DFG recebeu resultado invalido.");
    }

    manager->QueueCreateBatch(std::move(creates));
    manager->FinishBatchOperation();
}

void BookManager::OnDeleteBatchResult(const DFG::BatchOperationResult* result, void* userData) {
    std::unique_ptr<BatchContext> context(static_cast<BatchContext*>(userData));
    auto manager = GetSingleton();
    if (result && context && (result->resultCount == 0 || result->results)) {
        const auto count = (std::min)(
            static_cast<std::size_t>(result->resultCount),
            context->operations.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& item = result->results[i];
            const auto& operation = context->operations[i];
            if (item.status == DFG::Status::Success) {
                logger::info("[BookManager] DFG delete '{}' concluiu; slot {}:{:06X} liberado.",
                    operation.editorID, item.pluginNumber, item.localId);
            } else if (item.status != DFG::Status::EditorIdNotFound) {
                logger::error("[BookManager] DFG delete '{}' falhou: status {} '{}'.",
                    operation.editorID,
                    static_cast<std::uint32_t>(item.status),
                    item.error);
            }
        }
        if (count != context->operations.size()) {
            logger::error("[BookManager] Batch delete DFG retornou {} de {} resultados.",
                count, context->operations.size());
        }
        logger::info("[BookManager] Batch delete DFG: {} sucessos, {} falhas.",
            result->successCount, result->failureCount);
    } else {
        logger::error("[BookManager] Callback batch delete DFG recebeu resultado invalido.");
    }
    manager->FinishBatchOperation();
}

void BookManager::HandleBookResult(
    const DFG::FormOperationResult& result,
    const OperationContext& context) {
    auto book = result.form ? result.form->As<RE::TESObjectBOOK>() : nullptr;
    auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(context.perkID);
    const auto info = Manager::GetSingleton()->GetInfoByID("Perk", context.perkID);
    auto baseBook = RE::TESForm::LookupByID<RE::TESObjectBOOK>(_baseBookID);
    if (!book || !perk || !info || !baseBook) {
        logger::error("[BookManager] Resultado DFG '{}' nao pode ser associado ao perk/base runtime.",
            context.editorID);
        return;
    }

    RegisterBookForPerk(
        book,
        perk,
        *info,
        baseBook,
        { result.pluginNumber, result.localId, result.pluginName },
        result.recoveredExistingSlot ? "recovered" : "resolved");
}

void BookManager::HandleBookLookupResult(
    const DFG::FormLookupResult& result,
    const OperationContext& context) {
    auto book = result.form ? result.form->As<RE::TESObjectBOOK>() : nullptr;
    auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(context.perkID);
    const auto info = Manager::GetSingleton()->GetInfoByID("Perk", context.perkID);
    auto baseBook = RE::TESForm::LookupByID<RE::TESObjectBOOK>(_baseBookID);
    if (!book || !perk || !info || !baseBook) {
        logger::error("[BookManager] Lookup DFG '{}' nao pode ser associado ao perk/base runtime.",
            context.editorID);
        return;
    }

    RegisterBookForPerk(
        book,
        perk,
        *info,
        baseBook,
        { result.pluginNumber, result.localId, result.pluginName },
        "lookup");
}

void BookManager::FinishBatchOperation() {
    if (_pendingBatches > 0) {
        --_pendingBatches;
    }
    if (_initializing && _pendingBatches == 0) {
        FinishBatch();
    }
}

void BookManager::FinishBatch() {
    _initializing = false;
    _initialized = true;
    logger::info("[BookManager] Batch DFG concluido. {} livros registrados.", _books.size());

    if (_rebuildRequested) {
        _rebuildRequested = false;
        RebuildDynamicBooks();
    }
}

void BookManager::ClearRuntimeMappings() {
    _books.clear();
    _bookToPerk.clear();
    _perkToBook.clear();
    _bookDescriptions.clear();
    _bookSlots.clear();
}

RE::TESObjectBOOK* BookManager::GetBookForPerk(RE::BGSPerk* perk) const {
    if (!perk) {
        return nullptr;
    }

    const auto it = _perkToBook.find(perk->GetFormID());
    return it != _perkToBook.end() ?
        RE::TESForm::LookupByID<RE::TESObjectBOOK>(it->second) :
        nullptr;
}

RE::BGSPerk* BookManager::GetPerkForBook(RE::TESForm* book) const {
    if (!book) {
        return nullptr;
    }

    const auto it = _bookToPerk.find(book->GetFormID());
    return it != _bookToPerk.end() ?
        RE::TESForm::LookupByID<RE::BGSPerk>(it->second) :
        nullptr;
}

bool BookManager::IsBookOfPerk(RE::TESForm* book) const {
    return book && _bookToPerk.contains(book->GetFormID());
}

bool BookManager::AddBookToRef(
    RE::TESObjectREFR* target,
    RE::BGSPerk* perk,
    std::int32_t count) const {
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

    const auto it = _bookDescriptions.find(book->GetFormID());
    return it != _bookDescriptions.end() ? &it->second : nullptr;
}

std::optional<BookManager::DynamicFormSlot> BookManager::GetDynamicFormSlot(
    RE::TESObjectBOOK* book) const {
    if (!book) {
        return std::nullopt;
    }

    const auto it = _bookSlots.find(book->GetFormID());
    return it != _bookSlots.end() ? std::optional{ it->second } : std::nullopt;
}

bool BookManager::ApplyBookPerk(
    RE::TESObjectBOOK* book,
    RE::TESObjectREFR* reader) const {
    auto perk = GetPerkForBook(book);
    auto actor = reader ? reader->As<RE::Actor>() : nullptr;
    if (!perk || !actor || actor->HasPerk(perk)) {
        return false;
    }

    actor->AddPerk(perk, 0);
    logger::info("[BookManager] Perk {:08X} aplicado ao ator {:08X} via livro {:08X}.",
        perk->GetFormID(), actor->GetFormID(), book->GetFormID());
    return true;
}

bool BookManager::ApplyBookPerkAndConsume(
    RE::TESObjectBOOK* book,
    RE::TESObjectREFR* reader) const {
    if (!ApplyBookPerk(book, reader)) {
        return false;
    }
    ConsumeBook(book, reader);
    return true;
}

bool BookManager::HandleBookMenuOpen(
    RE::TESObjectBOOK* book,
    RE::TESObjectREFR* reader) const {
    if (!IsBookOfPerk(book)) {
        return false;
    }

    auto actorReader = reader && reader->As<RE::Actor>() ?
        reader :
        static_cast<RE::TESObjectREFR*>(RE::PlayerCharacter::GetSingleton());
    ApplyBookPerkAndConsume(book, actorReader);
    logger::info("[BookManager] BookMenu bloqueado para livro de perk {:08X}.", book->GetFormID());
    return true;
}

bool BookManager::RegisterBookForPerk(
    RE::TESObjectBOOK* book,
    RE::BGSPerk* perk,
    const InternalFormInfo& info,
    RE::TESObjectBOOK* baseBook,
    DynamicFormSlot slot,
    std::string_view resolution) {
    if (!book || !perk) {
        return false;
    }

    const auto bookID = book->GetFormID();
    const auto perkID = perk->GetFormID();
    RemoveBookRuntimeMapping(perkID);
    ConfigureBookForPerk(book, perk);

    if (std::ranges::find(_books, book) == _books.end()) {
        _books.push_back(book);
    }
    _bookToPerk[bookID] = perkID;
    _perkToBook[perkID] = bookID;
    const auto perkKey = BookSettings::MakePerkKey(perk);
    const auto overrideData = BookSettings::GetOverride(perkKey);
    _bookDescriptions[bookID] = overrideData && !overrideData->description.empty() ?
        overrideData->description :
        info.description;
    _bookSlots[bookID] = slot;

    logger::info("[BookManager] DFG '{}' -> perk {:08X}, book {:08X}, slot {}:{:06X} '{}'.",
        resolution,
        perkID,
        bookID,
        slot.pluginNumber,
        slot.localID,
        slot.pluginName);
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
    _bookSlots.erase(bookID);
    std::erase_if(_books, [bookID](RE::TESObjectBOOK* book) {
        return !book || book->GetFormID() == bookID;
    });
}

void BookManager::ConfigureBookForPerk(
    RE::TESObjectBOOK* book,
    RE::BGSPerk* perk) const {
    if (!book || !perk) {
        return;
    }

    book->TESDescription::fileOffset = perk->TESDescription::fileOffset;
    book->TESDescription::descriptionText = perk->TESDescription::descriptionText;
    book->itemCardDescription.fileOffset = perk->TESDescription::fileOffset;
    book->itemCardDescription.descriptionText = perk->TESDescription::descriptionText;
}

void BookManager::ConsumeBook(
    RE::TESObjectBOOK* book,
    RE::TESObjectREFR* reader) const {
    if (!book || !reader) {
        return;
    }

    auto actor = reader->As<RE::Actor>();
    if (!actor) {
        return;
    }

    actor->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
    RE::SendUIMessage::SendInventoryUpdateMessage(actor, nullptr);
    logger::info("[BookManager] Livro {:08X} consumido do ator {:08X}.",
        book->GetFormID(), actor->GetFormID());
}

std::uint32_t BookManager::GiveAllBooksToPlayer() const {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        logger::warn("[BookManager] Player indisponivel. Livros nao adicionados.");
        return 0;
    }

    std::uint32_t addedCount = 0;
    for (auto book : _books) {
        if (book && player->GetItemCount(book) <= 0) {
            player->AddObjectToContainer(book, nullptr, 1, nullptr);
            ++addedCount;
        }
    }

    logger::info("[BookManager] Adicionados {} livros ao inventario do player.", addedCount);
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
            if (book && !book->IsDeleted() && !book->IsIgnored() &&
                BookSettings::MakeFormKey(book) == baseBookKey) {
                logger::info("[BookManager] Usando livro base '{}' ({:08X}).",
                    baseBookKey, book->GetFormID());
                return book;
            }
        }
        logger::warn("[BookManager] Livro base '{}' nao encontrado; usando fallback.", baseBookKey);
    }

    for (auto book : books) {
        if (book && !book->IsDeleted() && !book->IsIgnored() && !IsGeneratedBook(book) &&
            book->IsBookTome() && book->CanBeTaken() && book->inventoryModel) {
            return book;
        }
    }
    for (auto book : books) {
        if (book && !book->IsDeleted() && !book->IsIgnored() && !IsGeneratedBook(book)) {
            return book;
        }
    }
    return nullptr;
}
