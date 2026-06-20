#pragma once

// ============================================================================
// DynamicHardwareContext.h — forwarding header for libdynamichardware.
//
// Split into three single-responsibility classes:
//   SimulatedDefinitionBuilder  — test fixture definition generator
//   DynamicHardwareContextFactory — one-shot hardware scan and catalog update
//   DynamicHardwareContextObject  — runtime context (freeze/read/write lifecycle)
//
// New API Usage:
//   auto factory = DynamicHardwareContextFactory{}
//       .catalogPath("hardware.json")
//       .withSimulation("SimulatedAdapterDefinitions.json");
//   factory.discover();                           // scan hardware → populate/purge catalog
//   auto ctx = factory.buildRT();                 // create RT backends → return unique_ptr<DynamicHardwareContextObject>
//   ctx->freeze();                                // lock for real-time operation
//   while (running) { ctx->readAll(); ... ctx->writeAll(); }
// ============================================================================

#include "dynamichardware/SimulatedDefinitionBuilder.h"
#include "dynamichardware/DynamicHardwareContextFactory.h"
#include "dynamichardware/DynamicHardwareContextObject.h"

namespace dynamichardware {

} // namespace dynamichardware
