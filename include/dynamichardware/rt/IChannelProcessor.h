#pragma once

// ============================================================================
// IChannelProcessor — pluggable signal processing pipeline interface.
//
// Makes debounce, pulse detection, filtering, etc. extensible without modifying  
// DHDOEntry internals.  Processors chain together for composable pipelines.
// ============================================================================

#include "dynamichardware/dhdo/DHDO.h"

namespace dynamichardware::rt {

class IChannelProcessor {
public:
    virtual ~IChannelProcessor() = default;

    /// Called after a value is read from hardware (input channels).
    virtual void processOnRead(DHDOEntry& entry) noexcept = 0;

    /// Called before a value is written to hardware (output channels).
    virtual void processOnWrite(DHDOEntry& entry) noexcept = 0;
};

} // namespace dynamichardware::rt
