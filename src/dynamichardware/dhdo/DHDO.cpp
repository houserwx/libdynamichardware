#include "dynamichardware/dhdo/DHDO.h"

// read()/write() are now inline in the header with [[gnu::always_inline]] and composable bitmask dispatch.

namespace dynamichardware::dhdo {

bool DHDOEntry::getBool() const noexcept
{
    if (type == EntryType::BoolInput) return boolVal_;
    if (type == EntryType::BoolOutput) return pulse.isHighOrLatched();
    return false;
}

void DHDOEntry::setBool(bool v) noexcept
{
    if (type == EntryType::BoolOutput) {
        pulse.arm(v, dynamichardware::rt::signalProcessNowNs());
    }
}

void DHDO::freeze()
{
    entries.shrink_to_fit();
    image.shrink_to_fit();

    // Re-base image pointers for adapters that own their buffer.
    // If image.empty(), entry image pointers are left untouched — they already
    // point into backend-owned memory (e.g., EtherCAT domainData_).
    if (!image.empty()) {
        for (auto& entry : entries) {
            entry.image = image.data() + entry.byteOffset;
        }
    }
}

} // namespace dynamichardware::dhdo
