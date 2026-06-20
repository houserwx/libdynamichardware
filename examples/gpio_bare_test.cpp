// ============================================================================
// gpio_bare_test.cpp — Minimal libgpiod smoke test (NO libdynamichardware)
//
// Directly opens gpiochip0, requests lines {17,22,23,24,25,27} as outputs,
// and runs a simple chaser pattern. Used to isolate hang/crash from our
// higher-level abstractions.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <vector>

#include <gpiod.h>

int main()
{
    std::printf("=== Bare GPIO Test ===\n"); fflush(stdout);

    // Step 1: Open the chip
    std::printf("[1] Opening /dev/gpiochip0...\n"); fflush(stdout);
    struct gpiod_chip* chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) {
        std::fprintf(stderr, "[FAIL] Cannot open gpiochip0\n");
        return 1;
    }
    std::printf("[OK] Chip opened\n"); fflush(stdout);

    const uint32_t pins[] = {17, 22, 23, 24, 25, 27};
    constexpr int npins = 6;
    const char* consumer = "bare_test";
    bool ok = true;

    // Step 2: Get line handles for each pin
    std::vector<struct gpiod_line*> lines(npins, nullptr);
    for (int i = 0; i < npins && ok; ++i) {
        std::printf("[2] Getting line %u...\n", pins[i]); fflush(stdout);
        lines[i] = gpiod_chip_get_line(chip, pins[i]);
        if (!lines[i]) {
            std::fprintf(stderr, "[FAIL] Cannot get line %u\n", pins[i]);
            ok = false;
        } else {
            std::printf("   [OK] Line %u handle acquired\n", pins[i]); fflush(stdout);
        }
    }

    // Step 3: Request as outputs (initial value = 0 / LOW)
    for (int i = 0; i < npins && ok; ++i) {
        std::printf("[3] Requesting line %u as OUTPUT (init=LOW)...\n", pins[i]); fflush(stdout);
        int rc = gpiod_line_request_output(lines[i], consumer, 0);
        if (rc != 0) {
            std::fprintf(stderr, "[FAIL] Cannot request line %u as output (rc=%d)\n", pins[i], rc);
            ok = false;
        } else {
            std::printf("   [OK] Line %u is now output\n", pins[i]); fflush(stdout);
        }
    }

    if (!ok) {
        std::fprintf(stderr, "[FAIL] Setup failed — aborting.\n");
        gpiod_chip_close(chip);
        return 1;
    }

    std::printf("\n=== Chaser Pattern ===\n");
    std::printf("Each light ON for ~500ms. Press Ctrl+C to stop.\n\n"); fflush(stdout);

    unsigned currentLight = 0;
    auto startTime = std::chrono::steady_clock::now();

   try {
        while (true) {
            // All OFF first
            for (int i = 0; i < npins; ++i) {
                gpiod_line_set_value(lines[i], 0);
            }

            // Current one ON
            gpiod_line_set_value(lines[currentLight], 1);

            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime).count();

            std::printf("[%lus] GPIO%u ON\n", elapsed, pins[currentLight]); fflush(stdout);

            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            currentLight = (currentLight + 1) % npins;
        }
    } catch (...) {
        std::fprintf(stderr, "[Demo] Interrupted.\n");
    }

    std::printf("\n[4] Closing chip...\n"); fflush(stdout);
    gpiod_chip_close(chip);
    std::printf("[OK] Done.\n");
    return 0;
}
