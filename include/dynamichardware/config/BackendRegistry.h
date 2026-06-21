#pragma once

// ============================================================================
// BackendRegistry — global backend module registration system.
//
// Each backend self-registers at static init time via a creator function that  
// returns (scanner, adapter) pairs.  Eliminates hardcoded if-blocks in factory code,  
// satisfying OCP — adding new backends requires zero factory changes.
// ============================================================================

#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/IRuntimeAdapter.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dynamichardware::config {

/// Factory function signature for creating a scanner+adapter pair.
using BackendCreator = std::function<std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                                                std::unique_ptr<dhdo::IRuntimeAdapter>>()>;

/// Type alias for the internal registry map.
using BackendCreators = std::unordered_map<std::string, BackendCreator>;

class BackendRegistry {
public:
    /// Register a backend type with its creator function.
    /// Called by each backend module's static initializer.
    static void registerBackend(const std::string& name, BackendCreator creator);

    /// Get all registered backend names (sorted alphabetically).
    [[nodiscard]] static std::vector<std::string> getAll();

    /// Look up creator function by backend name. Returns nullptr if not found.
    [[nodiscard]] static const BackendCreator* getCreator(const std::string& name);

    /// Create instances for ALL registered backends (batch creation helper).
    [[nodiscard]] static std::vector<std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                                               std::unique_ptr<dhdo::IRuntimeAdapter>>>
            createAll();

private:
    static BackendCreators creators_;  ///< Internal storage — thread-safe after init
};

} // namespace dynamichardware::config
