#pragma once
#include "dynamichardware/pdo/PDO.h"
#include <vector>

// ============================================================
// IHardwareAdapter — abstract transport backend.
//
// RT DESIGN PATTERN: Virtual Dispatch at Backend Level (The Exception)
// Only 2 virtual calls PER BACKEND PER CYCLE.
// Per-entry read/write is concrete (PDOEntry::read/write).
// ============================================================

namespace dynamichardware::pdo {

class IHardwareAdapter {
public:
    virtual ~IHardwareAdapter() = default;

    IHardwareAdapter(const IHardwareAdapter&)            = delete;
    IHardwareAdapter& operator=(const IHardwareAdapter&) = delete;
    IHardwareAdapter(IHardwareAdapter&&)                 = delete;
    IHardwareAdapter& operator=(IHardwareAdapter&&)      = delete;

    virtual bool initialize() = 0;
    virtual void onBeforeReadInputs()  noexcept {}
    virtual void onAfterWriteOutputs() noexcept {}

    [[nodiscard]] std::vector<PDO>& getPDOs() noexcept { return pdos_; }

protected:
    IHardwareAdapter() = default;
    std::vector<PDO> pdos_;
};

} // namespace dynamichardware::pdo
