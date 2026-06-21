cmake_minimum_required(VERSION 3.20)
project(DynamicHardware
    VERSION 0.1.0
    DESCRIPTION "Hardware abstraction layer for dynamic I/O channels"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ── Sources ──────────────────────────────────────────────────────────────────

set(LD_SOURCES
    # Context — split into separate classes (1 class per file)
    src/SimulatedDefinitionBuilder.cpp
    src/DynamicHardwareContextFactory.cpp
    src/DynamicHardwareContextObject.cpp
    # DHDO core (transport-agnostic process image abstraction)
    src/dynamichardware/dhdo/DHDO.cpp
    src/dynamichardware/dhdo/HardwareRegistry.cpp
    src/dynamichardware/dhdo/DHDOFactory.cpp
    src/dynamichardware/dhdo/HardwareCatalog.cpp
    # EtherCAT backend (split discovery + RT)
    src/dynamichardware/backends/ethercat/EthercatDiscovery.cpp
    src/dynamichardware/backends/ethercat/EthercatRTBackend.cpp
    src/dynamichardware/backends/ethercat/kSlaveTypes.cpp
    # GPIO backend (split discovery + RT)
    src/dynamichardware/backends/gpio/BoardVariant.cpp
    src/dynamichardware/backends/gpio/GPIODiscovery.cpp
    src/dynamichardware/backends/gpio/GPIORTBackend.cpp
    # I2C backend (split discovery + RT)
    src/dynamichardware/backends/i2c/I2CDiscovery.cpp
    src/dynamichardware/backends/i2c/I2CRTBackend.cpp
    # SPI backend (split discovery + RT)
    src/dynamichardware/backends/spi/SPIDiscovery.cpp
    src/dynamichardware/backends/spi/SPIRTBackend.cpp
    # Simulated backend (split discovery + RT)
    src/dynamichardware/backends/simulated/SimulatedDiscovery.cpp
    src/dynamichardware/backends/simulated/SimulatedRTBackend.cpp
)

add_library(dynamichardware STATIC ${LD_SOURCES})
add_library(DynamicHardware::dynamichardware ALIAS dynamichardware)

target_include_directories(dynamichardware
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_options(dynamichardware PRIVATE -Wall -Wextra -Wpedantic)

# ── Dependencies ─────────────────────────────────────────────────────────────

# nlohmann_json (vendored header-only — always available, included via thirdparty/ above)
target_compile_definitions(dynamichardware PUBLIC NLOHMANN_JSON_AVAILABLE)

# IgH EtherCAT (optional — stub mode when absent)
# Cross-compile: accept ETHERCAT_LIB + ETHERCAT_INCLUDE_DIR from parent CMake.
# Native: auto-detect from system paths.
if(CMAKE_CROSSCOMPILING)
    if(ETHERCAT_LIB)
        # Accept parent-provided include dir, otherwise search parent thirdparty/
        if(NOT ETHERCAT_INCLUDE_DIR)
            find_path(ETHERCAT_INCLUDE_DIR NAMES ecrt.h
                PATHS "${CMAKE_SOURCE_DIR}/thirdparty/ethercat/include"
                      "${CMAKE_CURRENT_SOURCE_DIR}/../ethercat/include"
                NO_DEFAULT_PATH
            )
        endif()
        if(ETHERCAT_INCLUDE_DIR)
            set(ETHERCAT_AVAILABLE ON)
        endif()
    endif()
else()
    find_library(ETHERCAT_LIB NAMES ethercat
        PATHS /usr/local/lib /usr/lib
    )
    if(ETHERCAT_LIB)
        find_path(ETHERCAT_INCLUDE_DIR NAMES ecrt.h
            PATHS /usr/local/include /usr/include
        )
        if(ETHERCAT_INCLUDE_DIR)
            set(ETHERCAT_AVAILABLE ON)
        endif()
    endif()
endif()

if(ETHERCAT_AVAILABLE)
    message(STATUS "EtherCAT support enabled")
    target_link_libraries(dynamichardware PRIVATE ${ETHERCAT_LIB})
    target_include_directories(dynamichardware PRIVATE ${ETHERCAT_INCLUDE_DIR})
    target_compile_definitions(dynamichardware PRIVATE ETHERCAT_AVAILABLE)
else()
    message(STATUS "EtherCAT not found — stub mode")
endif()

# libgpiod (optional — GPIO adapter)
# Cross-compile: accept LIBGPIOD_LIB + LIBGPIOD_INCLUDE_DIR from parent CMake.
# Native: auto-detect from system paths.
if(CMAKE_CROSSCOMPILING)
    if(LIBGPIOD_LIB AND EXISTS "${LIBGPIOD_LIB}")
        # Accept parent-provided include dir (e.g., from deploy.sh staging area).
        # If not provided, search common cross-compile sysroot locations.
        if(NOT LIBGPIOD_INCLUDE_DIR)
            find_path(LIBGPIOD_INCLUDE_DIR NAMES gpiod.h
                PATHS "${CMAKE_SOURCE_DIR}/deploy/include"
                      "${CMAKE_CURRENT_SOURCE_DIR}/../include"
                NO_DEFAULT_PATH
            )
        endif()
        set(LIBGPIOD_FOUND TRUE)
    endif()
else()
    find_library(LIBGPIOD_LIB NAMES gpiod PATHS /usr/lib /usr/local/lib)
    find_path(LIBGPIOD_INCLUDE_DIR NAMES gpiod.h PATHS /usr/include /usr/local/include)
    if(LIBGPIOD_LIB AND LIBGPIOD_INCLUDE_DIR)
        set(LIBGPIOD_FOUND TRUE)
    endif()
endif()

if(LIBGPIOD_FOUND)
    message(STATUS "libgpiod found — GPIO adapter enabled (v2 API)")
    target_link_libraries(dynamichardware PRIVATE ${LIBGPIOD_LIB})
    if(LIBGPIOD_INCLUDE_DIR)
        target_include_directories(dynamichardware PRIVATE ${LIBGPIOD_INCLUDE_DIR})
    endif()
    target_compile_definitions(dynamichardware PRIVATE GPIO_LIBGPIOD_AVAILABLE)
else()
    message(STATUS "libgpiod not found — GPIO adapter will use stub mode")
endif()

# ── dh-discover CLI tool ─────────────────────────────────────────────────────

add_executable(dh-discover src/tools/discover.cpp)
target_link_libraries(dh-discover PRIVATE dynamichardware)
target_include_directories(dh-discover PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Propagate compile definitions + include dirs from the library to the tool
# (needed because dh-discover includes backend headers that #include ecrt.h/gpiod.h)
if(ETHERCAT_AVAILABLE)
    target_compile_definitions(dh-discover PRIVATE ETHERCAT_AVAILABLE)
    target_include_directories(dh-discover PRIVATE ${ETHERCAT_INCLUDE_DIR})
endif()
if(LIBGPIOD_FOUND)
    target_compile_definitions(dh-discover PRIVATE GPIO_LIBGPIOD_AVAILABLE)
    if(LIBGPIOD_INCLUDE_DIR)
        target_include_directories(dh-discover PRIVATE ${LIBGPIOD_INCLUDE_DIR})
    endif()
endif()
set_target_properties(dh-discover PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ── Examples ─────────────────────────────────────────────────────────────────

add_executable(simulated_io_demo examples/simulated_io_demo.cpp)
target_link_libraries(simulated_io_demo PRIVATE dynamichardware)
target_include_directories(simulated_io_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(ETHERCAT_AVAILABLE)
    target_compile_definitions(simulated_io_demo PRIVATE ETHERCAT_AVAILABLE)
    target_include_directories(simulated_io_demo PRIVATE ${ETHERCAT_INCLUDE_DIR})
endif()
if(LIBGPIOD_FOUND)
    target_compile_definitions(simulated_io_demo PRIVATE GPIO_LIBGPIOD_AVAILABLE)
    if(LIBGPIOD_INCLUDE_DIR)
        target_include_directories(simulated_io_demo PRIVATE ${LIBGPIOD_INCLUDE_DIR})
    endif()
endif()

add_executable(ethercat_demo examples/ethercat_demo.cpp)
target_link_libraries(ethercat_demo PRIVATE dynamichardware)
target_include_directories(ethercat_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(ETHERCAT_AVAILABLE)
    target_compile_definitions(ethercat_demo PRIVATE ETHERCAT_AVAILABLE)
    target_include_directories(ethercat_demo PRIVATE ${ETHERCAT_INCLUDE_DIR})
endif()


# Bare-metal libgpiod smoke test (no libdynamichardware dependencies)
if(LIBGPIOD_FOUND)
    add_executable(gpio_bare_test examples/gpio_bare_test.cpp)
    target_include_directories(gpio_bare_test PRIVATE ${LIBGPIOD_INCLUDE_DIR})
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LIBGPIOD_PC libgpiod QUIET)
        if(LIBGPIOD_PC_FOUND)
            target_link_libraries(gpio_bare_test PRIVATE ${LIBGPIOD_PC_LIBRARIES})
        else()
            target_link_libraries(gpio_bare_test PRIVATE gpiod)
        endif()
    else()
        target_link_libraries(gpio_bare_test PRIVATE gpiod)
    endif()
endif()

# GPIO demo (correct pattern — follows EtherCAT style)
if(LIBGPIOD_FOUND)
    add_executable(gpio_correct_demo examples/gpio_correct_demo.cpp)
    target_link_libraries(gpio_correct_demo PRIVATE dynamichardware)
    target_include_directories(gpio_correct_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(gpio_correct_demo PRIVATE GPIO_LIBGPIOD_AVAILABLE)
    if(LIBGPIOD_INCLUDE_DIR)
        target_include_directories(gpio_correct_demo PRIVATE ${LIBGPIOD_INCLUDE_DIR})
    endif()
endif()

install(TARGETS dh-discover ethercat_demo simulated_io_demo
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── Packaging for install / find_package ─────────────────────────────────────

# Export targets
install(TARGETS dynamichardware
    EXPORT DynamicHardwareTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install headers + vendored thirdparty
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY thirdparty/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install export set
install(EXPORT DynamicHardwareTargets
    FILE DynamicHardwareTargets.cmake
    NAMESPACE DynamicHardware::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DynamicHardware
)

# Config and version files
configure_package_config_file(
    cmake/DynamicHardwareConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/DynamicHardwareConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DynamicHardware
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/DynamicHardwareConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/DynamicHardwareConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/DynamicHardwareConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/DynamicHardware
)

# ── Unit tests ──────────────────────────────────────────────────────────────

option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()

    # Fetch Catch2 (no system dependency required)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.0
    )
    FetchContent_MakeAvailable(Catch2)

    set(TEST_SOURCES
        tests/test_entrytype_bitmask.cpp
        tests/test_dhdo_entry.cpp
        tests/test_signal_process.cpp
        tests/test_vector_buffer.cpp
        tests/test_dhdo_freeze.cpp
        tests/test_dhdo_factory.cpp
        tests/test_hardware_registry.cpp
        tests/test_hardware_catalog.cpp
    )

    add_executable(unit_tests ${TEST_SOURCES})
    target_link_libraries(unit_tests PRIVATE dynamichardware Catch2::Catch2WithMain)
    target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(unit_tests PRIVATE -Wall -Wextra)

    # Propagate optional backend definitions so test compilation doesn't fail
    if(ETHERCAT_AVAILABLE)
        target_compile_definitions(unit_tests PRIVATE ETHERCAT_AVAILABLE)
        target_include_directories(unit_tests PRIVATE ${ETHERCAT_INCLUDE_DIR})
    endif()
    if(LIBGPIOD_FOUND)
        target_compile_definitions(unit_tests PRIVATE GPIO_LIBGPIOD_AVAILABLE)
        if(LIBGPIOD_INCLUDE_DIR)
            target_include_directories(unit_tests PRIVATE ${LIBGPIOD_INCLUDE_DIR})
        endif()
    endif()

    include(CTest)
    add_test(NAME unit_tests COMMAND unit_tests)
endif()
