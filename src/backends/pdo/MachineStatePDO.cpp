#include "backends/pdo/MachineStatePDO.h"

namespace fc::pdo {

MachineStatePDO::MachineStatePDO(std::vector<uint8_t>  image,
                                 std::vector<FlagEntry> flagEntries,
                                 std::vector<PDOEntry>  entries) noexcept
    : image_(std::move(image))
    , flagEntries_(std::move(flagEntries))
{
    pdos_[0].image = std::vector<uint8_t>();  // externally owned (image_)
    pdos_[0].entries = std::move(entries);
    // Set image pointers directly into image_
    for (std::size_t i = 0; i < pdos_[0].entries.size(); ++i) {
        pdos_[0].entries[i].image = image_.data() + i;
    }
}

void MachineStatePDO::onBeforeReadInputs() noexcept
{
    for (auto& fe : flagEntries_) {
        *fe.imageSlot = fe.source ? (*fe.source ? 1u : 0u) : 0u;
    }
}

} // namespace fc::pdo
