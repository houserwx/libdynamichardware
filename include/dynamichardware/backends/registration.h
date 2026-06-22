#pragma once

// ============================================================================
// registration.h — zero-boilerplate self-registration for backend modules.
//
// Include this header and invoke REGISTER_BACKEND once at file scope in any
// backend's .cpp file. The macro uses a local static variable inside the
// constructor of an anonymous-namespace struct, guaranteeing exactly-once
// registration at first ODR-use (Meyers' singleton pattern). Static init order
// across TUs is not guaranteed, but runtime dispatch always occurs after all
// static constructors complete — discovery() calls happen well into main().
//
// Usage example (from src/dynamichardware/backends/ethercat/EthercatDiscovery.cpp):
//   #include "dynamichardware/backends/registration.h"
//   ...
//   REGISTER_BACKEND("EtherCAT", []() {
//       return std::make_pair(
//           std::make_unique<ethercat::EthercatDiscovery>(),
//           std::make_unique<ethercat::EthercatRTBackend>()
//       );
//   });
// ============================================================================

#include "dynamichardware/config/BackendRegistry.h"

/// Register a backend at static-init time with exactly-once guarantee via
/// function-local static initialization (C++11 thread-safe since C++11).
/// Each invocation creates an anonymous namespace to avoid symbol collisions.
#define REGISTER_BACKEND(Name, CreatorLambda) \
    namespace {                                \
        struct Registrar_##__LINE__ {          \
            Registrar_##__LINE__() {           \
                dynamichardware::config::BackendRegistry::registerBackend( \
                    Name, CreatorLambda);      \
            }                                  \
        };                                     \
        static Registrar_##__LINE__ sRegistrar_##__LINE__;               \
    }
