#include "dynamichardware/config/BackendRegistry.h"
#include <algorithm>

namespace dynamichardware::config {

// Static member definition
BackendCreators BackendRegistry::creators_;

void BackendRegistry::registerBackend(const std::string& name, BackendCreator creator) {
    creators_[name] = std::move(creator);
}

std::vector<std::string> BackendRegistry::getAll() {
    std::vector<std::string> names;
    names.reserve(creators_.size());
    for (const auto& [key, _] : creators_) {
        names.push_back(key);
    }
    
    // Sort alphabetically for deterministic iteration order
    std::sort(names.begin(), names.end());
    return names;
}

const BackendCreator* BackendRegistry::getCreator(const std::string& name) {
    auto it = creators_.find(name);
    if (it != creators_.end()) {
        return &it->second;
    }
    return nullptr;  // Not found — caller should handle gracefully
}

std::vector<std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                      std::unique_ptr<dhdo::IRuntimeAdapter>>> 
BackendRegistry::createAll() {
    std::vector<std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                          std::unique_ptr<dhdo::IRuntimeAdapter>>> results;
    
    for (auto& [_, creator] : creators_) {
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
