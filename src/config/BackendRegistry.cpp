#include "dynamichardware/config/BackendRegistry.h"
#include <algorithm>

namespace dynamichardware::config {

// Magic Static accessor — construct-on-first-use avoids static init order fiasco
// with REGISTER_BACKEND macros firing from anonymous namespace constructors across TUs
// before this TU's data members are ready. C++11 guarantees thread-safe initialization
// of local statics — first call creates the unordered_map before any registration lambda
// tries operator[].
static BackendCreators& registryMap() {
    static BackendCreators instance;
    return instance;
}

// Static member definition — no longer used directly but kept for ABI compatibility.
BackendCreators BackendRegistry::creators_;

void BackendRegistry::registerBackend(const std::string& name, BackendCreator creator) {
    registryMap()[name] = std::move(creator);
}

std::vector<std::string> BackendRegistry::getAll() {
    auto& map = registryMap();
    std::vector<std::string> names;
    names.reserve(map.size());
    for (const auto& [key, _] : map) {
        names.push_back(key);
    }

    // Sort alphabetically for deterministic iteration order
    std::sort(names.begin(), names.end());
    return names;
}

const BackendCreator* BackendRegistry::getCreator(const std::string& name) {
    auto& map = registryMap();
    auto it = map.find(name);
    if (it != map.end()) {
        return &it->second;
    }
    return nullptr;  // Not found — caller should handle gracefully
}

std::vector<std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                      std::unique_ptr<dhdo::IRuntimeAdapter>>> 
BackendRegistry::createAll() {
    auto& map = registryMap();
    std::vector<std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                          std::unique_ptr<dhdo::IRuntimeAdapter>>> results;

    for (auto& [_, creator] : map) {
        if (!creator) continue;  // Skip unregistered slots

        try {
            results.emplace_back(creator());  // Invoke factory function to create instances
        } catch (...) {
            // If a backend creation fails, skip that backend and continue.
            // Caller can check results size or log the failure separately.
        }
    }

    return results;
}

} // namespace dynamichardware::config
