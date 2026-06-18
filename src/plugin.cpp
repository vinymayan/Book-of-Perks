#include "logger.h"
#include "BookManager.h"
#include "BookMenu.h"
#include "DPFAPI.h"
#include "Manager.h"
#include "Papyrus.h"
#include "Serialization.h"

#include <cstdint>

namespace {
    constexpr std::uint32_t MakeFourCC(char a, char b, char c, char d) {
        return static_cast<std::uint32_t>(a) |
            (static_cast<std::uint32_t>(b) << 8) |
            (static_cast<std::uint32_t>(c) << 16) |
            (static_cast<std::uint32_t>(d) << 24);
    }

    void InitializeBookManagers() {
        auto manager = Manager::GetSingleton();
        manager->PopulateAllLists();

        if (!DPF::GetAPI()) {
            logger::warn("[BookManager] DPF API ainda indisponivel. Initialize adiado.");
            return;
        }

        BookManager::GetSingleton()->Initialize();
       
    }

    void QueueInitializeBookManagers() {
        if (const auto task = SKSE::GetTaskInterface()) {
            task->AddTask(InitializeBookManagers);
        } else {
            InitializeBookManagers();
        }
    }

}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        BookMenu::Register();
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        auto manager = Manager::GetSingleton();
        manager->PopulateAllLists();
        QueueInitializeBookManagers();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame) {
        BookManager::GetSingleton()->RebuildDynamicBooks();
    }

}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetPapyrusInterface()->Register(Papyrus::Register);
    BookManager::GetSingleton()->InstallHooks();
    auto serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID(MakeFourCC('B', 'O', 'P', '1'));
    serialization->SetRevertCallback(Serialization::RevertCallback);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
