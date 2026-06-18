
#include "Serialization.h"

#include "BookManager.h"
#include "Manager.h"

namespace Serialization {
    void SaveCallback(SKSE::SerializationInterface*) {
    }

    void LoadCallback(SKSE::SerializationInterface*) {
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        BookManager::GetSingleton()->Revert();
        Manager::GetSingleton()->Revert(nullptr);
    }
}
