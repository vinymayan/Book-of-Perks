#include "logger.h"
#include "BookManager.h"
#include "BookMenu.h"
#include "DFGAPI.h"
#include "Manager.h"
#include "Papyrus.h"

namespace {
    bool hasDFG = false;

    void InitializeBookManagers() {
        const auto dfg = DFG::GetAPI();
        if (!dfg || !dfg->IsReady()) {
            logger::warn("[BookManager] DFG API ainda indisponivel ou nao pronta. Initialize adiado.");
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

    void PopulateListsForDFGUpdate(std::string_view a_signatures)
    {
        Manager::GetSingleton()->RefreshLists(a_signatures);
        QueueInitializeBookManagers();
    }

    class DynamicFormsGeneratorListener : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static DynamicFormsGeneratorListener* GetSingleton()
        {
            static DynamicFormsGeneratorListener singleton;
            return &singleton;
        }

        void Register()
        {
            if (auto dispatcher = SKSE::GetModCallbackEventSource()) {
                dispatcher->AddEventSink(this);
            }
        }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;

            std::string_view eventName = a_event->eventName.c_str();
            if (eventName == "DynamicFormsGeneratorLoaded") {
                Manager::GetSingleton()->PopulateAllLists();
                QueueInitializeBookManagers();
                return RE::BSEventNotifyControl::kContinue;
            }
            if (eventName == "DynamicFormsGeneratorUpdated") {
                const std::string_view signature = a_event->strArg.c_str();
                auto bookManager = BookManager::GetSingleton();
                if (signature == "BOOK" && bookManager->IsInitializing()) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                PopulateListsForDFGUpdate(a_event->strArg.c_str());
                return RE::BSEventNotifyControl::kContinue;
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        hasDFG = GetModuleHandleA("DynamicFormsGenerator.dll") != nullptr;
        if (hasDFG) {
            logger::info("DynamicFormsGenerator.dll found");
        }
        BookMenu::Register();
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        Manager::GetSingleton()->PopulateAllLists();
        if (hasDFG) {
            QueueInitializeBookManagers();
        } else {
            logger::error("[BookManager] DynamicFormsGenerator.dll nao encontrado; livros nao serao criados.");
        }
    }

}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetPapyrusInterface()->Register(Papyrus::Register);
    BookManager::GetSingleton()->InstallHooks();
    DynamicFormsGeneratorListener::GetSingleton()->Register();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
