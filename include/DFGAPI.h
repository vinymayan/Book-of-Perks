#pragma once

#include "RE/Skyrim.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace DFG
{
    inline constexpr std::uint32_t InterfaceVersion = 1;
    inline constexpr const char* InterfaceName = "DynamicFormsGenerator";

    enum class Operation : std::uint32_t
    {
        Create = 1,
        Update = 2,
        Delete = 3,
        Lookup = 4
    };

    enum class Status : std::uint32_t
    {
        Success = 0,
        NotReady,
        InvalidArgument,
        InvalidJson,
        MissingEditorId,
        InvalidEditorId,
        MissingPackageName,
        InvalidPackageName,
        MissingFormKind,
        UnsupportedFormKind,
        EditorIdAlreadyExists,
        EditorIdNotFound,
        EditorIdMismatch,
        FormKindMismatch,
        ProtectedField,
        DPFUnavailable,
        DPFCreateFailed,
        ConfigureFailed,
        PersistenceFailed,
        DPFReleaseFailed,
        InternalError,
        BatchPartialSuccess,
        BatchFailed
    };

    struct CreateFormRequest
    {
        std::uint32_t structSize{ sizeof(CreateFormRequest) };
        const char* requester{ nullptr };
        const char* packageName{ nullptr };
        const char* formJson{ nullptr };
    };

    struct UpdateFormRequest
    {
        std::uint32_t structSize{ sizeof(UpdateFormRequest) };
        const char* requester{ nullptr };
        const char* editorId{ nullptr };
        const char* patchJson{ nullptr };
    };

    struct DeleteFormRequest
    {
        std::uint32_t structSize{ sizeof(DeleteFormRequest) };
        const char* requester{ nullptr };
        const char* editorId{ nullptr };
    };

    struct LookupFormRequest
    {
        std::uint32_t structSize{ sizeof(LookupFormRequest) };
        const char* requester{ nullptr };
        const char* editorId{ nullptr };
    };

    struct CreateFormsRequest
    {
        std::uint32_t structSize{ sizeof(CreateFormsRequest) };
        const CreateFormRequest* requests{ nullptr };
        std::uint32_t requestCount{ 0 };
    };

    struct UpdateFormsRequest
    {
        std::uint32_t structSize{ sizeof(UpdateFormsRequest) };
        const UpdateFormRequest* requests{ nullptr };
        std::uint32_t requestCount{ 0 };
    };

    struct DeleteFormsRequest
    {
        std::uint32_t structSize{ sizeof(DeleteFormsRequest) };
        const DeleteFormRequest* requests{ nullptr };
        std::uint32_t requestCount{ 0 };
    };

    struct LookupFormsRequest
    {
        std::uint32_t structSize{ sizeof(LookupFormsRequest) };
        const LookupFormRequest* requests{ nullptr };
        std::uint32_t requestCount{ 0 };
    };

    struct FormOperationResult
    {
        std::uint32_t structSize{ sizeof(FormOperationResult) };
        Operation operation{ Operation::Create };
        Status status{ Status::InternalError };
        RE::TESForm* form{ nullptr };
        RE::FormID formID{ 0 };
        std::uint32_t pluginNumber{ 0 };
        std::uint32_t localId{ 0 };
        std::uint8_t recoveredExistingSlot{ 0 };
        char editorId[128]{};
        char packageName[128]{};
        char pluginName[64]{};
        char error[256]{};
    };

    // The result pointer remains valid only for the duration of the callback.
    using FormOperationCallback = void (*)(const FormOperationResult* result, void* userData);

    struct BatchOperationResult
    {
        std::uint32_t structSize{ sizeof(BatchOperationResult) };
        Operation operation{ Operation::Create };
        Status status{ Status::BatchFailed };
        const FormOperationResult* results{ nullptr };
        std::uint32_t resultCount{ 0 };
        std::uint32_t successCount{ 0 };
        std::uint32_t failureCount{ 0 };
        char updatedSignatures[256]{};
        char error[256]{};
    };

    // BatchOperationResult and its results array remain valid only for the
    // duration of the callback.
    using BatchOperationCallback = void (*)(const BatchOperationResult* result, void* userData);

    struct FormLookupResult
    {
        std::uint32_t structSize{ sizeof(FormLookupResult) };
        Status status{ Status::InternalError };
        std::uint8_t exists{ 0 };
        RE::TESForm* form{ nullptr };
        RE::FormID formID{ 0 };
        std::uint32_t pluginNumber{ 0 };
        std::uint32_t localId{ 0 };
        char editorId[128]{};
        char packageName[128]{};
        char pluginName[64]{};
        char formKind[64]{};
        char sourceSignature[16]{};
        // Complete resolved JSON. Valid only during the lookup callback.
        const char* formJson{ nullptr };
        std::uint32_t formJsonLength{ 0 };
        char error[256]{};
    };

    using FormLookupCallback = void (*)(const FormLookupResult* result, void* userData);

    struct BatchLookupResult
    {
        std::uint32_t structSize{ sizeof(BatchLookupResult) };
        Status status{ Status::BatchFailed };
        const FormLookupResult* results{ nullptr };
        std::uint32_t resultCount{ 0 };
        std::uint32_t foundCount{ 0 };
        std::uint32_t missingCount{ 0 };
        std::uint32_t failureCount{ 0 };
        char error[256]{};
    };

    using BatchLookupCallback = void (*)(const BatchLookupResult* result, void* userData);

    class IDynamicFormsGenerator
    {
    public:
        virtual ~IDynamicFormsGenerator() = default;

        [[nodiscard]] virtual std::uint32_t GetVersion() const noexcept = 0;
        [[nodiscard]] virtual bool IsReady() const noexcept = 0;

        // Request strings are copied before these functions return. Work and
        // callbacks run later on the game's main thread.
        virtual bool QueueCreateForm(
            const CreateFormRequest* request,
            FormOperationCallback callback,
            void* userData) noexcept = 0;
        virtual bool QueueUpdateForm(
            const UpdateFormRequest* request,
            FormOperationCallback callback,
            void* userData) noexcept = 0;
        virtual bool QueueDeleteForm(
            const DeleteFormRequest* request,
            FormOperationCallback callback,
            void* userData) noexcept = 0;

        // Batch operations preserve the order of the supplied requests. Each
        // request gets an independent result; successful items are not rolled
        // back when another item fails.
        virtual bool QueueCreateForms(
            const CreateFormsRequest* request,
            BatchOperationCallback callback,
            void* userData) noexcept = 0;
        virtual bool QueueUpdateForms(
            const UpdateFormsRequest* request,
            BatchOperationCallback callback,
            void* userData) noexcept = 0;
        virtual bool QueueDeleteForms(
            const DeleteFormsRequest* request,
            BatchOperationCallback callback,
            void* userData) noexcept = 0;

        // Lookups are read-only: they never allocate, recover, configure, or
        // release a DPF slot. A successful lookup can return exists == 0.
        virtual bool QueueLookupForm(
            const LookupFormRequest* request,
            FormLookupCallback callback,
            void* userData) noexcept = 0;
        virtual bool QueueLookupForms(
            const LookupFormsRequest* request,
            BatchLookupCallback callback,
            void* userData) noexcept = 0;
    };

    using GetDFGAPI = void* (*)();

    inline IDynamicFormsGenerator* GetAPI() noexcept
    {
        const auto module = GetModuleHandleA("DynamicFormsGenerator.dll");
        if (!module) {
            return nullptr;
        }

        const auto getter = reinterpret_cast<GetDFGAPI>(GetProcAddress(module, "GetDynamicFormsGeneratorAPI"));
        if (!getter) {
            return nullptr;
        }

        auto* api = static_cast<IDynamicFormsGenerator*>(getter());
        if (!api || api->GetVersion() != InterfaceVersion) {
            return nullptr;
        }
        return api;
    }
}
