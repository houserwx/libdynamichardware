// ============================================================================
// kSlaveTypes.cpp — Slave type definitions for known EtherCAT devices.
//
// Initially hand-populated with modules used in this project.
// Re-run scripts/parse_esi.py with ESI XML files to regenerate.
//
// Beckhoff vendor ID: 0x00000002
// ============================================================================

#include "backends/ethercat/SlaveTypeInfo.h"

namespace fc::ethercat {

// ---- Beckhoff vendor ID ----
static constexpr uint32_t kBeckhoffVendorId = 0x00000002u;

// ---- DC operating modes ----

static const DcOpMode kDcModes_EL1124[] = {
    {0x0000u, "FreeRun", "FreeRun"},
};

static const DcOpMode kDcModes_EL2124[] = {
    {0x0000u, "FreeRun", "FreeRun"},
};

static const DcOpMode kDcModes_EL3632[] = {
    {0x0730u, "DcSync", "2 Channels"},
    {0x0730u, "DcSync2", "2 Ch. - 2 times oversampling"},
    {0x0730u, "DcSync4", "2 Ch. - 4 times oversampling"},
    {0x0730u, "DcSync5", "2 Ch. - 5 times oversampling"},
    {0x0730u, "DcSync8", "2 Ch. - 8 times oversampling"},
    {0x0730u, "DcSync10", "2 Ch. - 10 times oversampling"},
    {0x0730u, "DcSync16", "2 Ch. - 16 times oversampling"},
    {0x0730u, "DcSync20", "2 Ch. - 20 times oversampling"},
    {0x0730u, "DcSync40", "2 Ch. - 40 times oversampling"},
    {0x0730u, "DcSync50", "2 Ch. - 50 times oversampling"},
};

// ---- Slave type registry ----

static const SlaveTypeInfo kSlaveTypesData[] = {
    // EL1124: Digital Input Terminal (4x 24V)
    {
        kBeckhoffVendorId, 0x00010001u,  // Product code EL1124
        "EL1124",
        "EL1124 4ch. digital input",
        kDcModes_EL1124,
        1
    },
    // EL2124: Digital Output Terminal (4x 24V)
    {
        kBeckhoffVendorId, 0x00020001u,  // Product code EL2124
        "EL2124",
        "EL2124 4ch. digital output",
        kDcModes_EL2124,
        1
    },
    // EL3632: Analog Input Terminal (2x -10...10V, 16-bit)
    {
        kBeckhoffVendorId, 0x00050001u,  // Product code EL3632
        "EL3632",
        "EL3632 2ch. analog input (-10..10V)",
        kDcModes_EL3632,
        10
    },
};

const SlaveTypeInfo kSlaveTypes[] = {
    kSlaveTypesData[0],
    kSlaveTypesData[1],
    kSlaveTypesData[2],
};
const size_t        kSlaveTypeCount = 3;

} // namespace fc::ethercat
