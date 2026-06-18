#pragma once
#include "backends/pdo/IHardwareAdapter.h"
#include "backends/grpc/GrpcTriggerMessage.h"
#include "backends/grpc/GrpcResultMessage.h"
#include "dynamichardware/rt/VectorBuffer.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fc::grpc {

// ============================================================================
// GrpcAdapter — multi-channel message backend for one Queue.
// One GrpcAdapter instance owns ALL message channel pairs for a single Queue.
// ============================================================================

class GrpcAdapter final : public fc::pdo::IHardwareAdapter {
public:
    static constexpr std::size_t kQueueCapacity = 256;

    explicit GrpcAdapter(std::string name);

    void reserve(std::size_t n);
    [[nodiscard]] int addChannel(std::string name, uint32_t simFailMod = 0);

    [[nodiscard]] fc::pdo::PDOEntry& outEntry(int ch) noexcept
        { return pdos_[0].entries[static_cast<std::size_t>(ch) * 2    ]; }
    [[nodiscard]] fc::pdo::PDOEntry& inEntry (int ch) noexcept
        { return pdos_[0].entries[static_cast<std::size_t>(ch) * 2 + 1]; }

    [[nodiscard]] std::size_t channelCount() const noexcept { return channels_.size(); }

    bool initialize()                  override;
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    [[nodiscard]] common::rt::VectorBuffer<GrpcTriggerMessage>&
        triggerOut(int ch) noexcept { return channels_[static_cast<std::size_t>(ch)]->triggerOut; }
    [[nodiscard]] common::rt::VectorBuffer<GrpcResultMessage>&
        resultIn  (int ch) noexcept { return channels_[static_cast<std::size_t>(ch)]->resultIn;  }

private:
    struct Channel {
        common::rt::VectorBuffer<GrpcTriggerMessage> triggerOut{kQueueCapacity};
        common::rt::VectorBuffer<GrpcResultMessage>  resultIn  {kQueueCapacity};
        std::string  name;
        uint32_t     simFailMod{0};
        uint64_t     simSeq    {0};
    };

    std::string                        name_;
    std::vector<std::unique_ptr<Channel>> channels_;
};

} // namespace fc::grpc
