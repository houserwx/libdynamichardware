// dh-discover — enumerate available backends, discover hardware, manage catalogs
//
// Usage:
//   dh-discover                              # auto-detect all available backends
//   dh-discover --catalog path.json          # discover + save catalog to path.json
//   dh-discover --inspect path.json          # print existing catalog entries
//   dh-discover --sim --catalog sim.json     # simulated backend only
//   dh-discover --gen-simdefs cat.json out.json  # generate sim definitions from catalog

#include "dynamichardware/DynamicHardwareContext.h"
#include "dynamichardware/pdo/HardwareCatalog.h"
#include "dynamichardware/backends/gpio/BoardVariant.h"

#ifdef ETHERCAT_AVAILABLE
#include "dynamichardware/backends/ethercat/EthercatAdapter.h"
#endif

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ── Helpers ─────────────────────────────────────────────────────

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

// ── Backend detection (useful for CLI probing) ──────────────────

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

    info.probed = true;
#ifdef ETHERCAT_AVAILABLE
    auto* master = ecrt_request_master(0);
    if (master) {
        info.available = true;
        ec_master_state_t state;
        if (ecrt_master_state(master, &state) == 0 && state.slaves_responding > 0) {
            info.detail = "Master 0 — " + std::to_string(state.slaves_responding) + " slave(s)";
        } else {
            info.detail = "Master 0 available";
        }
        ecrt_release_master(master);
    } else {
        info.detail = "Cannot request master 0";
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
    auto variant = dynamichardware::gpio::detectBoardVariant();
    if (variant == dynamichardware::gpio::BoardVariant::UNKNOWN) {
        info.detail = "Not a recognized Raspberry Pi";
    } else {
        info.detail = dynamichardware::gpio::boardVariantName(variant);
        if (dynamichardware::gpio::gpioChipAvailable(variant)) {
            info.available = true;
            info.detail += " — " + dynamichardware::gpio::gpioChipPath(variant);
        }
    }
    return info;
}

static BackendInfo detectI2C()
{
    BackendInfo info{"I2C", false, false, ""};
    info.probed = true;

    std::vector<std::string> i2cDevs = {
        "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2", "/dev/i2c-3", "/dev/i2c-4"
    };
    std::vector<std::string> found;
    for (const auto& path : i2cDevs) {
        FILE* f = fopen(path.c_str(), "r");
        if (f) { found.push_back(path); fclose(f); }
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
        if (f) { found.push_back(path); fclose(f); }
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
        if (f) { found.push_back(path); fclose(f); }
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

// ── Catalog inspection ─────────────────────────────────────

static bool inspectCatalog(const std::vector<dynamichardware::pdo::CatalogEntry>& entries)
{
    printHeader("Hardware Catalog");
    std::printf("Entries: %zu\n", entries.size());
    printSeparator();

    if (entries.empty()) {
        std::printf("(empty catalog — no channels discovered)\n");
    } else {
        std::printf("%-8s  %-28s  %-14s  %-20s  %s\n",
                    "Output", "Key", "ChannelType", "UUID", "Name");
        std::printf("%-8s  %-28s  %-14s  %-20s  %s\n",
                    "------", "----------------------------", "--------------",
                    "--------------------", "--------------------");

        for (const auto& e : entries) {
            std::printf("%-8s  %-28s  %-14s  %s  %s\n",
                        e.isOutput ? "Y" : "N",
                        e.key.c_str(),
                        e.channelType.c_str(),
                        e.uuid.c_str(),
                        e.name.c_str());
        }
    }

    std::printf("\n");
    return true;
}

static bool inspectCatalogFile(const std::string& path)
{
    dynamichardware::pdo::HardwareCatalog catalog;
    if (!catalog.load(path)) {
        std::fprintf(stderr, "Failed to load catalog: %s\n", path.c_str());
        return false;
    }

    std::printf("Path:  %s\n", path.c_str());
    return inspectCatalog(catalog.entries());
}

// ── Discovery via DynamicHardwareContext ───────────────

static void runSimulatedDiscovery(
        const std::optional<std::string>& catalogPath,
        const std::optional<std::string>& defsPath)
{
    printHeader("Simulated Backend Discovery");

    auto builder = dynamichardware::DynamicHardwareContext::builder()
        .withSimulation(defsPath)
        .catalogPath(catalogPath.value_or("hardware.json"));

    auto ctx = builder.build();
    if (!ctx) {
        std::fprintf(stderr, "[discover] Failed to create context\n");
        return;
    }

    if (!ctx->build()) {
        std::fprintf(stderr, "[discover] Context build failed\n");
        return;
    }

    std::printf("Backends:  %zu\n", ctx->backendCount());
    std::printf("Entries:   %zu\n", ctx->entryCount());
    std::printf("Healthy:   %s\n", ctx->allBackendsHealthy() ? "yes" : "no");

    if (!ctx->catalogEntries().empty()) {
        inspectCatalog(ctx->catalogEntries());
    }
}

static void runRealDiscovery(const std::optional<std::string>& catalogPath)
{
    printHeader("Hardware Discovery");

    auto builder = dynamichardware::DynamicHardwareContext::builder()
        .catalogPath(catalogPath.value_or("hardware.json"));

    // Enable backends based on detection
    if (detectEtherCAT().available) builder.withEthercat();
    if (detectGPIO().available) builder.withGPIO();
    if (detectI2C().available) builder.withI2C();
    if (detectSPI().available) builder.withSPI();

    auto ctx = builder.build();
    if (!ctx) {
        std::fprintf(stderr, "[discover] Failed to create context\n");
        return;
    }

    if (!ctx->build()) {
        std::fprintf(stderr, "[discover] Context build failed\n");
        return;
    }

    std::printf("Backends:  %zu\n", ctx->backendCount());
    std::printf("Entries:   %zu\n", ctx->entryCount());
    std::printf("Healthy:   %s\n", ctx->allBackendsHealthy() ? "yes" : "no");

    if (!ctx->catalogEntries().empty()) {
        inspectCatalog(ctx->catalogEntries());
    } else {
        std::printf("[discover] No hardware channels discovered.\n");
        std::printf("  Run on target hardware or use --sim for simulated discovery.\n");
    }
}

// ── Generate simulated adapter definitions from catalog ────

static void generateSimDefs(const std::string& catalogPath, const std::string& outputPath)
{
    printHeader("Generate Simulated Adapter Definitions");

    dynamichardware::pdo::HardwareCatalog catalog;
    if (!catalog.load(catalogPath)) {
        std::fprintf(stderr, "Failed to load catalog: %s\n", catalogPath.c_str());
        return;
    }

    std::printf("Source catalog:  %s\n", catalogPath.c_str());
    std::printf("Output file:     %s\n", outputPath.c_str());
    std::printf("Channels:        %zu\n\n", catalog.entries().size());

    std::ofstream out(outputPath);
    if (!out) {
        std::fprintf(stderr, "Cannot open output file: %s\n", outputPath.c_str());
        return;
    }

    using json = nlohmann::json;
    json defs = {{"cycleTimeUs", 1000}, {"channels", json::array()}};

    for (const auto& e : catalog.entries()) {
        json ch;
        ch["name"] = e.name;
        ch["uuid"] = e.uuid;
        ch["channelType"] = e.channelType;

        // Preserve sim parameters from source entry (if present)
        json sim;
        if (e.sim.rpm > 0.0f) sim["rpm"] = e.sim.rpm;
        if (e.sim.rollerDiamMm > 0.0f) sim["rollerDiamMm"] = e.sim.rollerDiamMm;
        if (e.sim.resolutionPpr > 0) sim["resolutionPpr"] = e.sim.resolutionPpr;
        if (e.sim.quadrature) sim["quadrature"] = true;
        if (e.sim.partsPerMin > 0.0f) sim["partsPerMin"] = e.sim.partsPerMin;
        if (e.sim.partWidthMm > 0.0f) sim["partWidthMm"] = e.sim.partWidthMm;
        if (e.sim.variancePercent > 0.0f) sim["variancePercent"] = e.sim.variancePercent;
        if (e.sim.pulseMs > 0) sim["pulseMs"] = e.sim.pulseMs;
        if (e.sim.debounceMs > 0) sim["debounceMs"] = e.sim.debounceMs;
        if (!sim.empty()) ch["sim"] = sim;

        defs["channels"].push_back(ch);
    }

    out << defs.dump(2) << std::endl;
    out.close();

    std::printf("Generated %s with %zu channel definitions\n",
                outputPath.c_str(), defs["channels"].size());
    std::printf("  Use with: dh-discover --sim --defs %s\n\n", outputPath.c_str());
}

// ── CLI ───────────────────────────────────────────────

static void printUsage(const char* prog)
{
    std::printf(
        "Usage: %s [options]\n"
        "\n"
        "Enumerate available hardware backends and build/inspect catalog.\n"
        "\n"
        "Options:\n"
        "  --catalog <path>         Save discovered catalog to path (JSON)\n"
        "  --inspect <path>         Print catalog entries from an existing file\n"
        "  --sim                    Use simulated backend (no hardware required)\n"
        "  --defs <path>            Load simulated adapter definitions from JSON\n"
        "  --gen-simdefs <in> <out> Generate sim definitions from an existing catalog\n"
        "  --help                   Show this help\n"
        "\n"
        "Examples:\n"
        "  %s                                  # detect all available backends\n"
        "  %s --catalog hardware.json          # discover + save catalog\n"
        "  %s --inspect hardware.json          # view saved catalog\n"
        "  %s --sim --catalog sim.json         # simulated discovery\n"
        "  %s --gen-simdefs hw.json defs.json  # generate sim definitions\n",
        prog, prog, prog, prog, prog, prog
    );
}

int main(int argc, char** argv)
{
    std::optional<std::string> catalogPath;
    std::optional<std::string> inspectPath;
    std::optional<std::string> simDefsPath;
    bool simulated = false;
    std::optional<std::string> genSimDefsIn;
    std::optional<std::string> genSimDefsOut;

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
        } else if (strcmp(argv[i], "--defs") == 0) {
            if (++i >= argc) {
                std::fprintf(stderr, "Missing path for --defs\n");
                return 1;
            }
            simDefsPath = argv[i];
        } else if (strcmp(argv[i], "--gen-simdefs") == 0) {
            if (++i >= argc || i + 1 >= argc) {
                std::fprintf(stderr, "Missing input/output paths for --gen-simdefs\n");
                return 1;
            }
            genSimDefsIn = argv[i++];
            genSimDefsOut = argv[i];
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
        return inspectCatalogFile(inspectPath.value()) ? 0 : 1;
    }

    // Generate sim definitions mode
    if (genSimDefsIn.has_value() && genSimDefsOut.has_value()) {
        generateSimDefs(genSimDefsIn.value(), genSimDefsOut.value());
        return 0;
    }

    // Backend detection table
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
        "yes"
#else
        "no (stub)"
#endif
    );
    printSeparator();

    std::vector<BackendInfo> backends = {
        detectEtherCAT(), detectGPIO(), detectI2C(), detectSPI(), detectCAN(), detectUART()
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

    // Run discovery
    if (simulated) {
        runSimulatedDiscovery(catalogPath, simDefsPath);
    } else {
        runRealDiscovery(catalogPath);
    }

    return 0;
}
