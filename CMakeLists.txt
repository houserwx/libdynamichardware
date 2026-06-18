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
    # PDO core
    src/backends/pdo/PDO.cpp
    src/backends/pdo/HardwareRegistry.cpp
    # EtherCAT adapter
    src/backends/ethercat/EthercatAdapter.cpp
    src/backends/ethercat/HardwareCatalog.cpp
    src/backends/ethercat/kSlaveTypes.cpp
    # GPIO adapter
    src/backends/gpio/BoardVariant.cpp
    src/backends/gpio/GPIOAdapter.cpp
    # I2C adapter
    src/backends/i2c/I2CAdapter.cpp
    # SPI adapter
    src/backends/spi/SPIAdapter.cpp
    # Simulated adapter
    src/backends/simulated/SimulatedAdapter.cpp
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

install(TARGETS dh-discover
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
