#pragma once
#include <cstdint>

namespace fc::grpc {

enum class InspectionResult : uint8_t {
    Unknown = 0,
    Pass    = 1,
    Fail    = 2,
    Rejected = 3,
    Corrected = 4,
    Fired   = 5,
};

// Trivially copyable message — adapter pushes onBeforeReadInputs.
struct GrpcResultMessage {
    uint64_t           seq{0};
    InspectionResult   result{InspectionResult::Unknown};
};

} // namespace fc::grpc
