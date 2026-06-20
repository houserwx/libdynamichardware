#include "dynamichardware/DynamicHardwareContext.h"

// Legacy implementation split into three single-responsibility classes:
//   SimulatedDefinitionBuilder.{h,cpp}  — test fixture definition generator
//   DynamicHardwareContextFactory.{h,cpp} — one-shot hardware scan + catalog update
//   DynamicHardwareContextObject.{h,cpp}  — runtime context (freeze/read/write)
//
// This file is intentionally minimal — all implementations live in the files above.
// Kept only to satisfy CMakeLists.txt source list during transition period.

namespace dynamichardware {

} // namespace dynamichardware
