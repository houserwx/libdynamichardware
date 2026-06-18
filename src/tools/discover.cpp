// dh-discover — enumerate available backends and build/inspect hardware catalog
//
// Usage:
//   dh-discover                          # auto-detect all available backends
//   dh-discover --catalog path.json      # discover + save catalog to path.json
//   dh-discover --inspect path.json      # print existing catalog entries
//   dh-discover --catalog out.json --sim # use simulated backend only (no hardware)

#include "backends/pdo/HardwareRegistry.h"
#include "backends/pdo/PDO.h"
#include "backends/ethercat/HardwareCatalog.h"
#include "backends/ethercat/EthercatAdapter.h"
#include "backends/gpio/GPIOAdapter.h"
#include "backends/gpio/BoardVariant.h"
#include "backends/i2c/I2CAdapter.h"
#include "backends/spi/SPIAdapter.h"
#include "backends/simulated/SimulatedAdapter.h"
#include "dynamichardware/rt/SignalProcess.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ── Helpers ──────────────────────────────────────────────────────────────────

static void printSeparator()
{
    std::printf("\n%s\n", "==============================================================");
}

static void printHeader(const char* title)
{
    printSeparator();
    std::printf(" %s\n", title);
    printSeparator();
}

// ── Backend detection ────────────────────────────────────────────────────────

struct BackendInfo {
    std::string name;
    bool        available;
    bool        probed;
    std::string detail;
};

static BackendInfo detectEtherCAT()
{
    BackendInfo info{"EtherCAT", false, false, ""};

#ifndef ETHERCAT_AVAILABLE
    info.detail = "libethercat not found at build time — stub mode";
    return info;
#endif

    // Try to open master 0
    info.probed = true;
#ifdef ETHERCAT_AVAILABLE
    auto* master = ecrt_request_master(0);
    if (master) {
        info.available = true;
        info.detail    = "Master 0 available";

        // Query master state to see if slaves respond
        ec_master_state_t state;
        if (ecrt_master_state(master, &state) == 0) {
            if (state.slaves_responding > 0) {
                info.detail = "Master 0 — " + std::to_string(state.slaves_responding) + " slave(s) responding";
            }
        }
        ecrt_release_master(master);
    } else {
        info.detail = "Cannot request master 0 (check permissions or hardware)";
    }
#else
    info.detail = "stub";
#endif
    return info;
}

static BackendInfo detectGPIO()
{
    BackendInfo info{"GPIO", false, false, ""};

#ifndef GPIO_LIBGPIOD_AVAILABLE
    info.detail = "libgpiod not found at build time — stub mode";
    return info;
#endif

    info.probed = true;

    auto variant = fc::gpio::detectBoardVariant();
    std::string chipPath = fc::gpio::gpioChipPath(variant);

    if (variant == fc::gpio::BoardVariant::UNKNOWN) {
        info.detail = "Not a recognized Raspberry Pi (may still work in stub mode)";
    } else {
        info.detail = fc::gpio::boardVariantName(variant);

        if (fc::gpio::gpioChipAvailable(variant)) {
            info.available = true;
            info.detail += " — " + chipPath + " accessible (" +
                           std::to_string(fc::gpio::gpioLineCount(variant)) + " lines)";
        } else {
            info.detail += " — " + chipPath + " not accessible";
        }
    }
    return info;
}

static BackendInfo detectI2C()
{
    BackendInfo info{"I2C", false, false, ""};
    info.probed = true;

    // Check for common I2C dev nodes
    std::vector<std::string> i2cDevs = {
        "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2", "/dev/i2c-3", "/dev/i2c-4"
    };
    std::vector<std::string> found;
    for (const auto& path : i2cDevs) {
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            found.push_back(path);
            fclose(f);
        }
    }

    if (!found.empty()) {
        info.available = true;
        info.detail = found.size() > 1
            ? std::to_string(found.size()) + " buses: " + found[0] + "..."
            : "1 bus: " + found[0];
    } else {
        info.detail = "No /dev/i2c-* nodes found";
    }
    return info;
}

static BackendInfo detectSPI()
{
    BackendInfo info{"SPI", false, false, ""};
    info.probed = true;

    std::vector<std::string> spiDevs = {
        "/dev/spidev0.0", "/dev/spidev0.1",
        "/dev/spidev1.0", "/dev/spidev1.1"
    };
    std::vector<std::string> found;
    for (const auto& path : spiDevs) {
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            found.push_back(path);
            fclose(f);
        }
    }

    if (!found.empty()) {
        info.available = true;
        info.detail = found.size() > 1
            ? std::to_string(found.size()) + " devices: " + found[0] + "..."
            : "1 device: " + found[0];
    } else {
        info.detail = "No /dev/spidev* nodes found";
    }
    return info;
}

static BackendInfo detectCAN()
{
    BackendInfo info{"CAN", false, false, ""};
    info.probed = true;

    // Check for can interfaces via /sys/class/net
    FILE* fp = popen("ls /sys/class/net/ 2>/dev/null | grep -E '^can[0-9]+$'", "r");
    if (fp) {
        char buf[256];
        std::vector<std::string> found;
        while (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\n")] = 0;
            if (buf[0]) found.push_back(buf);
        }
        pclose(fp);

        if (!found.empty()) {
            info.available = true;
            info.detail = found.size() > 1
                ? std::to_string(found.size()) + " interfaces: " + found[0] + "..."
                : "1 interface: " + found[0];
        } else {
            info.detail = "No can* network interfaces found";
        }
    } else {
        info.detail = "Cannot check /sys/class/net";
    }
    return info;
}

static BackendInfo detectUART()
{
    BackendInfo info{"UART", false, false, ""};
    info.probed = true;

    std::vector<std::string> uartDevs = {
        "/dev/ttyS0", "/dev/ttyAMA0", "/dev/ttyAMA1",
        "/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyUSB2", "/dev/ttyUSB3",
        "/dev/serial0", "/dev/serial1"
    };
    std::vector<std::string> found;
    for (const auto& path : uartDevs) {
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            found.push_back(path);
            fclose(f);
        }
    }

    if (!found.empty()) {
        info.available = true;
        info.detail = found.size() > 1
            ? std::to_string(found.size()) + " ports: " + found[0] + "..."
            : "1 port: " + found[0];
    } else {
        info.detail = "No serial devices found";
    }
    return info;
}

// ── Catalog inspection ───────────────────────────────────────────────────────

static bool inspectCatalog(const std::string& path)
{
    fc::pdo::HardwareCatalog catalog;
    if (!catalog.load(path)) {
        std::fprintf(stderr, "Failed to load catalog: %s\n", path.c_str());
        return false;
    }

    printHeader("Hardware Catalog");
    std::printf("Path:  %s\n", path.c_str());
    std::printf("Entries: %zu\n", catalog.entries().size());
    printSeparator();

    // Group by slave name
    if (catalog.entries().empty()) {
        std::printf("(empty catalog — no channels discovered)\n");
    } else {
        const auto& entries = catalog.entries();

        // Header
        std::printf("%-8s  %-28s  %-6s  %-5s  %-5s  %-14s  %-20s\n",
                    "Output", "Key", "Pos", "PDO", "Sub", "ChannelType", "UUID");
        std::printf("%-8s  %-28s  %-6s  %-5s  %-5s  %-14s  %-20s\n",
                    "------", "----------------------------", "------",
                    "-----", "-----", "--------------", "--------------------");

        for (const auto& e : entries) {
            std::printf("%-8s  %-28s  %-6u  %-5u  %-5u  %-14s  %s\n",
                        e.isOutput ? "Y" : "N",
                        e.key.c_str(),
                        e.slavePos,
                        e.pdoIndex,
                        e.pdoSubindex,
                        e.channelType.c_str(),
                        e.uuid.c_str());
        }
    }

    std::printf("\n");
    return true;
}

// ── Simulated discovery (no hardware needed) ─────────────────────────────────

static void runSimulatedDiscovery(const std::optional<std::string>& catalogPath)
{
    printHeader("Simulated Backend Discovery");

    fc::pdo::HardwareCatalog catalog;
    auto adapter = std::make_unique<fc::simulated::SimulatedAdapter>();
    adapter->setCatalog(&catalog);
    adapter->setCycleTimeUs(1000);

    // Register example simulated channels directly into catalog
    fc::pdo::CatalogEntry enc;
    enc.key = "SIM|SimEncoder-A"; enc.uuid = "virt-enc-a-0001";
    enc.channelType = "Encoder"; enc.name = "SimEncoder-A";
    enc.slaveName = "Simulated"; enc.isSimulated = true;
    enc.sim.rpm = 3000.0f; enc.sim.rollerDiamMm = 50.0f;
    enc.sim.resolutionPpr = 1024;
    catalog.addEntry(std::move(enc));

    fc::pdo::CatalogEntry di;
    di.key = "SIM|SimDigitalInput-1"; di.uuid = "virt-di-0001";
    di.channelType = "DigitalInput"; di.name = "SimDigitalInput-1";
    di.slaveName = "Simulated"; di.isSimulated = true;
    di.sim.partsPerMin = 120.0f;
    catalog.addEntry(std::move(di));

    fc::pdo::CatalogEntry do_ch;
    do_ch.key = "SIM|SimDigitalOutput-1"; do_ch.uuid = "virt-do-0001";
    do_ch.channelType = "DigitalOutput"; do_ch.name = "SimDigitalOutput-1";
    do_ch.slaveName = "Simulated"; do_ch.isSimulated = true;
    do_ch.isOutput = true; do_ch.sim.pulseMs = 100;
    catalog.addEntry(std::move(do_ch));

    if (!adapter->initialize()) {
        std::fprintf(stderr, "Simulated adapter initialization failed\n");
        return;
    }

    auto& pdos = adapter->getPDOs();
    std::printf("PDOs:      %zu\n", pdos.size());

    std::size_t totalEntries = 0;
    for (const auto& pdo : pdos) {
        totalEntries += pdo.entries.size();
    }
    std::printf("Entries:   %zu\n", totalEntries);

    for (const auto& pdo : pdos) {
        for (const auto& entry : pdo.entries) {
            std::printf("  %-20s  Type: %u  Offset: %u\n",
                        entry.uuid.c_str(),
                        static_cast<unsigned>(entry.type), entry.byteOffset);
        }
    }

    // Save catalog if requested
    if (catalogPath.has_value()) {
        if (catalog.save(catalogPath.value())) {
            std::printf("[discover] Simulated catalog saved to %s\n\n", catalogPath.value().c_str());
        }
    }
    std::printf("\n");
}

// ── Real hardware discovery ──────────────────────────────────────────────────

static void runRealDiscovery(const std::optional<std::string>& catalogPath)
{
    fc::pdo::HardwareCatalog catalog;

    // Try to load existing catalog first (preserves UUIDs across runs)
    if (catalogPath.has_value()) {
        catalog.load(catalogPath.value());
    }

    // Collect adapters
    std::vector<std::unique_ptr<fc::pdo::IHardwareAdapter>> adapters;

    // EtherCAT
    {
        printHeader("EtherCAT Backend");
#ifdef ETHERCAT_AVAILABLE
        auto ecAdapter = std::make_unique<fc::ethercat::EthercatAdapter>(1'000'000u);
        ecAdapter->setCatalog(&catalog);

        std::printf("Initializing...\n");
        if (ecAdapter->initialize()) {
            std::printf("Status:    OK\n");
            std::printf("Slaves:    %d\n", ecAdapter->slaveCount());
            std::printf("Available: %s\n", ecAdapter->isAvailable() ? "yes" : "no");
            std::printf("Channels:  %zu\n", catalog.entries().size());
            adapters.push_back(std::move(ecAdapter));
        } else {
            std::printf("Initialization failed (no slaves or master unavailable)\n");
        }
#else
        std::printf("Status:    stub (libethercat not linked)\n");
#endif
        std::printf("\n");
    }

    // GPIO
    {
        printHeader("GPIO Backend");
#ifdef GPIO_LIBGPIOD_AVAILABLE
        auto gpioAdapter = std::make_unique<fc::gpio::GPIOAdapter>();
        gpioAdapter->setCatalog(&catalog);

        auto variant = fc::gpio::detectBoardVariant();
        std::printf("Board:     %s\n", fc::gpio::boardVariantName(variant).c_str());

        // Try to discover available lines
        if (!gpioAdapter->initialize()) {
            std::printf("Initialization failed (GPIO chip not accessible)\n");
        } else {
            std::printf("Status:    OK\n");
            std::printf("Lines:     %zu\n", gpioAdapter->lineCount());
        }
#else
        std::printf("Status:    stub (libgpiod not linked)\n");
#endif
        std::printf("\n");
    }

    // I2C / SPI (stub backends — show they exist)
    {
        printHeader("I2C Backend");
        std::printf("Status:    stub (sysfs I2C not yet implemented)\n\n");

        printHeader("SPI Backend");
        std::printf("Status:    stub (sysfs SPI not yet implemented)\n\n");
    }

    // Print discovered catalog
    if (!catalog.empty()) {
        inspectCatalog(catalogPath.has_value() ? catalogPath.value() : "(memory)");

        // Save if a path was provided
        if (catalogPath.has_value()) {
            if (catalog.save(catalogPath.value())) {
                std::printf("[discover] Catalog saved to %s\n\n", catalogPath.value().c_str());
            }
        }
    } else {
        std::printf("[discover] No hardware channels discovered.\n");
        std::printf("  Run on target hardware or use --sim for simulated discovery.\n\n");
    }
}

// ── CLI ──────────────────────────────────────────────────────────────────────

static void printUsage(const char* prog)
{
    std::printf(
        "Usage: %s [options]\n"
        "\n"
        "Enumerate available hardware backends and build/inspect catalog.\n"
        "\n"
        "Options:\n"
        "  --catalog <path>    Save discovered catalog to path (JSON)\n"
        "  --inspect <path>    Print catalog entries from an existing file\n"
        "  --sim               Use simulated backend (no hardware required)\n"
        "  --help              Show this help\n"
        "\n"
        "Examples:\n"
        "  %s                              # detect all available backends\n"
        "  %s --catalog hardware.json      # discover + save catalog\n"
        "  %s --inspect hardware.json      # view saved catalog\n"
        "  %s --sim --catalog sim.json     # simulated discovery\n",
        prog, prog, prog, prog, prog
    );
}

int main(int argc, char** argv)
{
    std::optional<std::string> catalogPath;
    std::optional<std::string> inspectPath;
    bool simulated = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--catalog") == 0) {
            if (++i >= argc) {
                std::fprintf(stderr, "Missing path for --catalog\n");
                return 1;
            }
            catalogPath = argv[i];
        } else if (strcmp(argv[i], "--inspect") == 0) {
            if (++i >= argc) {
                std::fprintf(stderr, "Missing path for --inspect\n");
                return 1;
            }
            inspectPath = argv[i];
        } else if (strcmp(argv[i], "--sim") == 0) {
            simulated = true;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    // Inspect mode: just read and print
    if (inspectPath.has_value()) {
        return inspectCatalog(inspectPath.value()) ? 0 : 1;
    }

    printHeader("Backend Detection");
    std::printf("Platform:  Linux (detected backends)\n");
    std::printf("Build:     nlohmann_json=%s, EtherCAT=%s, GPIO=%s\n",
#ifdef NLOHMANN_JSON_AVAILABLE
        "yes",
#else
        "no",
#endif
#ifdef ETHERCAT_AVAILABLE
        "yes",
#else
        "no (stub)",
#endif
#ifdef GPIO_LIBGPIOD_AVAILABLE
        "yes",
#else
        "no (stub)"
#endif
    );
    printSeparator();

    // Enumerate all backends
    std::vector<BackendInfo> backends = {
        detectEtherCAT(),
        detectGPIO(),
        detectI2C(),
        detectSPI(),
        detectCAN(),
        detectUART()
    };

    std::printf("%-12s  %-6s  %s\n", "Backend", "Found", "Detail");
    std::printf("%-12s  %-6s  %s\n", "--------", "-----", "------");
    for (const auto& b : backends) {
        std::printf("%-12s  %-6s  %s\n",
                    b.name.c_str(),
                    b.available ? "yes" : "no",
                    b.detail.c_str());
    }
    std::printf("\n");

    // Now do actual discovery
    if (simulated) {
        runSimulatedDiscovery(catalogPath);
    } else {
        runRealDiscovery(catalogPath);
    }

    return 0;
}
