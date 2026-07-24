#pragma once

#include "RE/Skyrim.h"
#include <Windows.h>
#include <cstdint>
#include <string>

namespace DPF {
    constexpr auto InterfaceName = "DynamicPersistentForms";
    constexpr uint32_t InterfaceVersion = 1;

    inline std::string PluginNameForNumber(const uint32_t pluginNumber) {
        if (pluginNumber == 0) {
            return {};
        }
        if (pluginNumber == 1) {
            return "DPF.esp";
        }
        return "DPF " + std::to_string(pluginNumber) + ".esp";
    }

    // Persistent slots are always identified with both pluginNumber and localId.
    // pluginNumber 1 is DPF.esp, 2 is DPF 2.esp, and so on.
    class IDynamicPersistentForms {
    public:
        virtual ~IDynamicPersistentForms() = default;

        virtual uint32_t GetVersion() const = 0;

        virtual RE::TESForm* Create(RE::TESForm* baseItem) = 0;
        virtual RE::TESForm* CreateByType(uint32_t formType) = 0;
        virtual RE::TESForm* GetOrCreateByPluginLocalId(uint32_t pluginNumber, uint32_t localId,
            uint32_t formType) = 0;
        virtual RE::TESForm* GetOrCreateByFormId(RE::FormID formId, uint32_t formType) = 0;
        virtual RE::TESForm* GetOrCreateByOwnerKey(const char* owner, const char* key, uint32_t formType,
            uint32_t* pluginNumber, uint32_t* localId, bool* existed) = 0;
        virtual uint32_t GetPluginNumberForFormId(RE::FormID formId) const = 0;
        virtual bool ReleaseByOwnerKey(const char* owner, const char* key) = 0;
        virtual bool ReleaseByPluginLocalId(uint32_t pluginNumber, uint32_t localId, const char* owner) = 0;
        virtual uint32_t ReleaseOwner(const char* owner) = 0;
    };

    using GetDPFAPI = void* (*)();

    inline IDynamicPersistentForms* GetAPI() {
        const auto module = GetModuleHandleA("DPF.dll");
        if (!module) {
            return nullptr;
        }

        const auto getter = reinterpret_cast<GetDPFAPI>(GetProcAddress(module, "GetDPFAPI"));
        if (!getter) {
            return nullptr;
        }

        auto* api = static_cast<IDynamicPersistentForms*>(getter());
        if (!api || api->GetVersion() != InterfaceVersion) {
            return nullptr;
        }

        return api;
    }
}
