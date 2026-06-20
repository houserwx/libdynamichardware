// ============================================================================
// main_demo.cpp — entry point for the pdo_model_demo development binary.
//
// The demo binary is identical in structure to the production main.cpp but
// creates a DemoApplication (HardwareDemoRoutine patterns) instead of the
// production Application (Queue / FunctionEvaluator pipeline).
//
// Use this binary to smoke-test hardware layer changes:
//   sudo ./build/pdo_model_demo [config/default/hardware.json]
//
// Production binary: pdo_model (see src/main.cpp)
// ============================================================================

#include "Config.h"
#include "EthercatAdapter.h"
#include "HardwareCatalog.h"
#include "SimulatedAdapter.h"
#include "HardwareRegistry.h"
#include "DemoApplication.h"
#include "log/Logger.h"
#include "log/LogHelper.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <sys/mman.h>

static civ_control::DemoApplication* gApp = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static void sigHandler(int /*unused*/)
{
    if (gApp != nullptr) { gApp->requestStop(); }
}

int main(int argc, char* argv[]) // NOLINT(readability-function-cognitive-complexity,readability-function-size)
{
    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    std::string configPath = (argc > 1) ? argv[1] : "config/default/hardware.json"; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    {
        std::ifstream probe(configPath);
        if (!probe && configPath.find('/') == std::string::npos) {
            std::string alt = "config/" + configPath;
            if (std::ifstream{alt}) { configPath = alt; }
        }
    }

    if (mlockall(MCL_CURRENT) != 0) {
        std::fprintf(stderr, "[demo] mlockall(MCL_CURRENT) failed (continuing without lock)\n"); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }

    civ_control::LoggerConfiguration logConfig;
    logConfig.output   = civ_control::LogOutput::Console;
    logConfig.minLevel = civ_control::messages::LogLevel::Debug;
    civ_control::services::Logger::instance().init(logConfig);
    civ_control::services::Logger::instance().start();
    civ_control::threadLoggerInit(false);

    pdomodel::Config cfg;
    try {
        cfg = pdomodel::Config::loadFromJson(configPath);
        std::printf("[demo] Config loaded: %zu pdoEntries, cycleTimeUs=%ld\n", // NOLINT(cppcoreguidelines-pro-type-vararg)
                    cfg.pdoEntries.size(), static_cast<long>(cfg.cycleTimeUs));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[demo] Config error: %s — using defaults\n", ex.what()); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }

    const uint32_t cycleNs = static_cast<uint32_t>(cfg.cycleTimeUs) * 300U;

    pdomodel::HardwareCatalog catalog;
    catalog.load(cfg.hardwareCatalogPath);

    pdomodel::HardwareRegistry registry;

    auto ec = std::make_unique<pdomodel::EthercatAdapter>(cycleNs);
    ec->setCatalog(&catalog);
    ec->setConfig(&cfg);
    const bool hasEthercat = ec->initialize();
    if (!catalog.empty()) {
        system("mkdir -p config/shared"); // NOLINT(cert-env33-c)
        catalog.save(cfg.hardwareCatalogPath);
    }
    if (hasEthercat) {
        std::printf("[demo] EtherCAT adapter ready: %d slave(s), WC=%u, FC=%s\n", // NOLINT(cppcoreguidelines-pro-type-vararg)
                    ec->slaveCount(), ec->workingCounter(),
                    ec->isFullyCommunicating() ? "YES" : "NO (may still sync)");
    } else {
        std::printf("[demo] EtherCAT not available — running simulated only\n"); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }
    registry.addBackend(std::move(ec));

    auto sim = std::make_unique<pdomodel::SimulatedAdapter>(cfg);
    if (!sim->initialize()) {
        std::fprintf(stderr, "[demo] SimulatedAdapter init failed\n"); // NOLINT(cppcoreguidelines-pro-type-vararg)
        civ_control::services::Logger::instance().stop();
        return 1;
    }
    registry.addBackend(std::move(sim));

    registry.freezeForRt();

    civ_control::DemoApplication app(registry, cycleNs);
    gApp = &app;

    app.start();
    app.join();
    gApp = nullptr;

    std::printf("\nTiming: overruns=%d  max=%lld ns\n", // NOLINT(cppcoreguidelines-pro-type-vararg)
                app.overrunCount(),
                static_cast<long long>(app.maxOverrunNs()));

    std::printf("\n[demo] Checking kernel log for EtherCAT errors (last 10s)...\n"); // NOLINT(cppcoreguidelines-pro-type-vararg)
    std::fflush(stdout);
    system("dmesg -T | grep -iE '(ethercat|ecm|datagram|timeout|slave|domain|working.counter)' | tail -30"); // NOLINT(cert-env33-c)

    civ_control::services::Logger::instance().stop();
    return 0;
}
