#pragma once

#include "backends/pdo/IHardwareAdapter.h"
#include "backends/pdo/PDO.h"

#include <cstdint>
#include <vector>

namespace fc::pdo {

// Forward declaration
class MachineStateController;

// ---------------------------------------------------------------------------
// FlagEntry — one projection binding.
// ---------------------------------------------------------------------------
struct FlagEntry {
    const bool* source;
    uint8_t*    imageSlot;
};

// ---------------------------------------------------------------------------
// MachineStatePDO — RT-phase IHardwareAdapter that projects MachineState bits
// into a process image so they are accessible as standard DigitalInput entries.
// ---------------------------------------------------------------------------
class MachineStatePDO final : public IHardwareAdapter {
public:
    MachineStatePDO(std::vector<uint8_t>  image,
                    std::vector<FlagEntry> flagEntries,
                    std::vector<PDOEntry>  entries) noexcept;

    bool initialize() override { return true; }
    void onBeforeReadInputs() noexcept override;

private:
    std::vector<uint8_t>   image_;
    std::vector<FlagEntry> flagEntries_;
};

} // namespace fc::pdo
