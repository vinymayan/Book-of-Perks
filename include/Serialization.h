
#pragma once

namespace Serialization {
    void SaveCallback(SKSE::SerializationInterface* intfc);
    void LoadCallback(SKSE::SerializationInterface* intfc);
    void RevertCallback(SKSE::SerializationInterface* intfc);
}
