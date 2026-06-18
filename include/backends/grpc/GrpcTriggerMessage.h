#pragma once
#include <cstdint>

namespace fc::grpc {

// Trivially copyable message — RT arms once per trigger.
struct GrpcTriggerMessage {
    uint64_t seq{0};
    int64_t  productId{0};
    uint64_t timestampNs{0};
};

} // namespace fc::grpc
