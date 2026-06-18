#include "backends/grpc/GrpcAdapter.h"

namespace fc::grpc {

GrpcAdapter::GrpcAdapter(std::string name)
    : name_(std::move(name))
{
}

void GrpcAdapter::reserve(std::size_t n)
{
    pdos_[0].entries.reserve(n * 2);
    channels_.reserve(n);
}

int GrpcAdapter::addChannel(std::string name, uint32_t simFailMod)
{
    const int idx = static_cast<int>(channels_.size());

    // MessageOut entry
    fc::pdo::PDOEntry outEntry;
    outEntry.type = fc::pdo::EntryType::MessageOut;
    outEntry.uuid = name_ + ".out." + std::to_string(idx);

    // MessageIn entry
    fc::pdo::PDOEntry inEntry;
    inEntry.type = fc::pdo::EntryType::MessageIn;
    inEntry.uuid = name_ + ".in." + std::to_string(idx);

    pdos_[0].entries.push_back(std::move(outEntry));
    pdos_[0].entries.push_back(std::move(inEntry));

    auto ch = std::make_unique<Channel>();
    ch->name = std::move(name);
    ch->simFailMod = simFailMod;
    channels_.push_back(std::move(ch));

    return idx;
}

bool GrpcAdapter::initialize()
{
    pdos_[0].freeze();
    return true;
}

void GrpcAdapter::onBeforeReadInputs() noexcept
{
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        auto& ch = channels_[i];
        fc::pdo::PDOEntry& inEntry = pdos_[0].entries[i * 2 + 1];

        GrpcResultMessage msg;
        if (ch->resultIn.tryPop(msg)) {
            inEntry.setInMessageRaw(&msg, sizeof(msg));
        }
    }
}

void GrpcAdapter::onAfterWriteOutputs() noexcept
{
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        auto& ch = channels_[i];
        fc::pdo::PDOEntry& outEntry = pdos_[0].entries[i * 2];

        GrpcTriggerMessage trig;
        if (outEntry.tryConsumeOutMessage(trig)) {
            if (ch->simFailMod > 0) {
                // Sim relay: synthesize result immediately
                ch->simSeq++;
                GrpcResultMessage result;
                result.seq = trig.seq;
                result.result = (ch->simSeq % ch->simFailMod == 0)
                    ? InspectionResult::Fail : InspectionResult::Pass;
                (void)ch->resultIn.tryPush(result);
            } else {
                // gRPC relay: push to triggerOut for service thread
                (void)ch->triggerOut.tryPush(trig);
            }
        }
    }
}

} // namespace fc::grpc
