cmake_minimum_required(VERSION 3.22)

# -----------------------------------------------------------------------------
# raw-radio-studio — free, open-source (AGPLv3) tracking-first DAW
#
# Stack: JUCE + Tracktion Engine, C++20, CMake >= 3.22.
# Platforms: macOS 12+ (CoreAudio), Ubuntu 24.04+ (ALSA hw directly).
#
# Epic 0 (part 2) — engine wiring:
#   * Pinned git submodules under third_party/ (see .gitmodules + DEPENDENCIES.md).
#   * `raw_radio_studio` is a JUCE GUI app that opens an empty DocumentWindow.
#   * JUCE + Tracktion Engine compile and link (Tracktion::Engine::getVersion()).
#
# Dependency pinning / OQ-3 (JUCE <-> Tracktion compatibility):
#   Tracktion Engine only adds its own bundled JUCE (modules/juce) when the
#   `juce::juce_core` target does NOT already exist (see its top-level
#   CMakeLists.txt). We therefore register our own pinned JUCE *first*, so
#   Tracktion compiles against that exact JUCE revision and we fully control the
#   JUCE pin. See DEPENDENCIES.md for the verified commit pair.
# -----------------------------------------------------------------------------

project(raw_radio_studio
    VERSION 0.1.1
    DESCRIPTION "raw-radio-studio — open-source (AGPLv3) tracking-first DAW"
    HOMEPAGE_URL "https://github.com/raw-radio/raw-radio-studio"
    LANGUAGES C CXX)

# --- Language standard -------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Sensible default build type for single-config generators ----------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# macOS 12+ per STUDIO_SPEC NFR-OS-1.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

find_package(Threads REQUIRED)

# --- Options (some are consumed in later epics) ------------------------------
# Epic 3: opt-in LV2 plugin hosting (maps to JUCE_PLUGINHOST_LV2=1).
option(RAW_RADIO_STUDIO_ENABLE_LV2
    "Enable LV2 plugin hosting (Epic 3, opt-in)" OFF)

# -----------------------------------------------------------------------------
# Pinned dependencies (git submodules — exact commits recorded in DEPENDENCIES.md)
#
# Both submodules are pinned gitlinks (mode 160000). Initialise with:
#     git submodule update --init
# (non-recursive on purpose: Tracktion's own `modules/juce` nested submodule uses
#  an SSH URL and is deliberately NOT used — we pin JUCE ourselves.)
# -----------------------------------------------------------------------------
set(RAW_RADIO_STUDIO_JUCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE")
set(RAW_RADIO_STUDIO_TRACKTION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracktion_engine")
set(RAW_RADIO_STUDIO_SIGNALSMITH_STRETCH_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch")

foreach(_raw_radio_dep IN ITEMS
        "${RAW_RADIO_STUDIO_JUCE_DIR}"
        "${RAW_RADIO_STUDIO_TRACKTION_DIR}")
    if(NOT EXISTS "${_raw_radio_dep}/CMakeLists.txt")
        message(FATAL_ERROR
            "Dependency submodule is not initialised: ${_raw_radio_dep}\n"
            "Run:  git submodule update --init")
    endif()
endforeach()

# Signalsmith Stretch is header-only and ships no CMake usage requirement we can
# use as-is: its own CMakeLists delegates FFTs to the separate signalsmith-linear
# repo via FetchContent (configure-time network). We therefore do NOT add its
# subdirectory; we expose the headers through a plain INTERFACE target and rely
# on the self-contained `dsp/` copy bundled at the pinned tag 1.1.0.
if(NOT EXISTS "${RAW_RADIO_STUDIO_SIGNALSMITH_STRETCH_DIR}/signalsmith-stretch.h")
    message(FATAL_ERROR
        "Dependency submodule is not initialised: "
        "${RAW_RADIO_STUDIO_SIGNALSMITH_STRETCH_DIR}\n"
        "Run:  git submodule update --init")
endif()

add_library(rrs_signalsmith_stretch INTERFACE)
target_include_directories(rrs_signalsmith_stretch INTERFACE
    "${RAW_RADIO_STUDIO_SIGNALSMITH_STRETCH_DIR}")

# Tracktion Engine: skip its examples/tests; we only need the module targets.
set(TE_ADD_EXAMPLES OFF CACHE BOOL "Add the Tracktion Engine example targets" FORCE)

# Tracktion Engine's runtime Engine::getVersion() string is stale upstream, so
# read the authoritative module version from its VERSION.md for the banner.
set(RAW_RADIO_STUDIO_TRACKTION_VERSION_FILE
    "${RAW_RADIO_STUDIO_TRACKTION_DIR}/VERSION.md")
if(EXISTS "${RAW_RADIO_STUDIO_TRACKTION_VERSION_FILE}")
    file(READ "${RAW_RADIO_STUDIO_TRACKTION_VERSION_FILE}"
        RAW_RADIO_STUDIO_TRACKTION_VERSION)
    string(STRIP "${RAW_RADIO_STUDIO_TRACKTION_VERSION}"
        RAW_RADIO_STUDIO_TRACKTION_VERSION)
else()
    message(FATAL_ERROR
        "Tracktion Engine version file not found: "
        "${RAW_RADIO_STUDIO_TRACKTION_VERSION_FILE}\n"
        "The submodule looks incomplete — run:  git submodule update --init")
endif()

# 1) Register the pinned JUCE first (defines juce::juce_core, so Tracktion uses it).
add_subdirectory("${RAW_RADIO_STUDIO_JUCE_DIR}"     "${CMAKE_BINARY_DIR}/_deps/JUCE")
# 2) Register the Tracktion Engine modules (tracktion::tracktion_engine, ...).
add_subdirectory("${RAW_RADIO_STUDIO_TRACKTION_DIR}" "${CMAKE_BINARY_DIR}/_deps/tracktion_engine")

# -----------------------------------------------------------------------------
# Brand assets — vendored, compiled into the binary (no runtime file I/O).
#
#   * assets/icons/*.svg — Phosphor Icons (regular, MIT); every fill-capable
#     element pinned to fill="#FFFFFF" because JUCE does not resolve SVG
#     `currentColor`.
#     Tinted at runtime by IconCache via Drawable::replaceColour.
#   * assets/fonts/*.ttf — Inter + JetBrains Mono (OFL-1.1), registered at
#     startup by BrandFonts via Typeface::createSystemTypefaceFor.
# -----------------------------------------------------------------------------
file(GLOB RAW_RADIO_STUDIO_ICON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
file(GLOB RAW_RADIO_STUDIO_FONT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf")

juce_add_binary_data(RawRadioStudioAssets
    HEADER_NAME RawRadioStudioAssets.h
    NAMESPACE RawRadioStudioAssets
    SOURCES ${RAW_RADIO_STUDIO_ICON_SOURCES} ${RAW_RADIO_STUDIO_FONT_SOURCES})

# -----------------------------------------------------------------------------
# Application target: opens an empty JUCE window; links JUCE + Tracktion Engine.
# -----------------------------------------------------------------------------
# AU hosting is macOS-only. Pass PLUGINHOST_AU with an explicit value only on
# Apple: an empty `${APPLE}` expansion on Linux would leave a bare
# `PLUGINHOST_AU` keyword, which JUCE parses as TRUE.
set(_raw_radio_juce_app_options)
if(APPLE)
    list(APPEND _raw_radio_juce_app_options PLUGINHOST_AU 1)
endif()

juce_add_gui_app(raw_radio_studio
    PRODUCT_NAME "raw-radio-studio"
    COMPANY_NAME "raw-radio"
    BUNDLE_ID "ru.rawradio.studio"
    VERSION "${PROJECT_VERSION}"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "raw-radio-studio requires audio input access to record."
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    ${_raw_radio_juce_app_options})

target_sources(raw_radio_studio PRIVATE
    src/main.cpp
    src/studio/AudioEngine.cpp
    src/studio/AudioImport.cpp
    src/studio/LastDirectoryStore.cpp
    src/studio/PluginHost.cpp
    src/studio/PluginPresets.cpp
    src/studio/Session.cpp
    src/studio/StemsExport.cpp
    src/studio/TimeStretch.cpp
    src/studio/WavExport.cpp
    src/ui/DevicePanel.cpp
    src/ui/InputMeter.cpp
    src/ui/MixerPanel.cpp
    src/ui/PluginBrowser.cpp
    src/ui/RoutingPanel.cpp
    src/ui/Timeline.cpp
    src/ui/BrandButton.cpp
    src/ui/BrandLookAndFeel.cpp
    src/ui/BrandFonts.cpp
    src/ui/IconCache.cpp
    src/ui/MainComponent.cpp)

# macOS app icon: the raster `.icns` generated from assets/brand/app-icon.svg by
# assets/brand/generate-icons.sh (committed so CI bundles are reproducible). It
# is copied into the bundle's Resources and referenced from Info.plist, so
# Finder/Dock show the RAW Radio icon rather than a generic app glyph. The SVG
# itself lives under assets/brand/ (not assets/icons/) so the RawRadioStudioAssets
# BinaryData glob does not pull it in — it is not a runtime icon.
#
# Note: JUCE generates its own Info.plist through `juceaide` and ignores CMake's
# MACOSX_BUNDLE_ICON_FILE property, so we also patch `CFBundleIconFile` into the
# copied plist after the bundle is assembled. MACOSX_BUNDLE_ICON_FILE is still
# set for tooling that reads it.
if(APPLE)
    set(_rrs_app_icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/AppIcon.icns")

    target_sources(raw_radio_studio PRIVATE "${_rrs_app_icon}")
    set_source_files_properties("${_rrs_app_icon}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(raw_radio_studio PROPERTIES
        MACOSX_BUNDLE_ICON_FILE AppIcon.icns)

    add_custom_command(TARGET raw_radio_studio POST_BUILD
        COMMAND /usr/libexec/PlistBuddy -c
                "Set :CFBundleIconFile AppIcon.icns"
                "$<TARGET_BUNDLE_CONTENT_DIR:raw_radio_studio>/Info.plist"
        VERBATIM)
endif()

juce_generate_juce_header(raw_radio_studio)

target_include_directories(raw_radio_studio PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(raw_radio_studio PRIVATE cxx_std_20)

target_compile_definitions(raw_radio_studio PRIVATE
    # JUCE module configuration (mirrors Tracktion Engine's DemoRunner).
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=$<BOOL:${RAW_RADIO_STUDIO_ENABLE_LV2}>
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:raw_radio_studio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:raw_radio_studio,JUCE_VERSION>"
    # Version banner (see src/main.cpp).
    RAW_RADIO_STUDIO_VERSION="${PROJECT_VERSION}"
    RAW_RADIO_STUDIO_TRACKTION_VERSION="${RAW_RADIO_STUDIO_TRACKTION_VERSION}")

if(APPLE)
    target_compile_definitions(raw_radio_studio PRIVATE JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(raw_radio_studio PRIVATE
    RawRadioStudioAssets
    rrs_signalsmith_stretch
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_warning_flags
    Threads::Threads)

if(APPLE)
    # Tracktion Engine's own examples use this on Darwin.
    target_compile_options(raw_radio_studio PRIVATE "-fno-aligned-allocation")
elseif(UNIX)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_link_libraries(raw_radio_studio PRIVATE atomic)
    endif()
endif()

# -----------------------------------------------------------------------------
# Tests (Epic 1)
#
# Small unit tests for pure/testable logic:
#   * audio-device error classification (FR-MON-5 / NFR-IO-4),
#   * ALSA `hw` device-name policy,
#   * device-panel layout (status/error labels must stay visible),
#   * 24-bit WAV writer round-trip (FR-EXP-1 / NFR-A-1).
#
# doctest is vendored (pinned by content) under third_party/doctest/ — no
# configure-time network access is required. See NOTICE for the inventory entry.
# -----------------------------------------------------------------------------
enable_testing()

add_library(raw_radio_studio_doctest INTERFACE)
target_include_directories(raw_radio_studio_doctest INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest")

juce_add_console_app(raw_radio_studio_tests
    PRODUCT_NAME "raw-radio-studio-tests")

target_sources(raw_radio_studio_tests PRIVATE
    tests/test_studio.cpp
    src/studio/LastDirectoryStore.cpp
    src/studio/PluginPresets.cpp
    src/studio/TimeStretch.cpp
    src/ui/BrandFonts.cpp
    src/ui/IconCache.cpp)

target_include_directories(raw_radio_studio_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(raw_radio_studio_tests PRIVATE cxx_std_20)

target_compile_definitions(raw_radio_studio_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(raw_radio_studio_tests PRIVATE
    raw_radio_studio_doctest
    RawRadioStudioAssets
    rrs_signalsmith_stretch
    juce::juce_audio_formats
    juce::juce_graphics
    juce::juce_core
    juce::juce_recommended_warning_flags)

juce_generate_juce_header(raw_radio_studio_tests)

add_test(NAME raw_radio_studio_tests COMMAND raw_radio_studio_tests)

# -----------------------------------------------------------------------------
# Headless integration tests (Epic 1 QA)
#
# Same doctest framework, but linked against the real Tracktion Engine so the
# acceptance-critical paths can run without hardware:
#   * WavExport offline render (FR-EXP-1 / NFR-A-1),
#   * project save/open round-trip (FR-PRJ-1),
#   * autosave `.tmp_` naming + crash-recovery scan (FR-PRJ-2 / FR-REC-8).
# The engine is created with device auto-initialisation disabled (see
# tests/test_integration.cpp), so no audio device is required.
# -----------------------------------------------------------------------------
juce_add_console_app(raw_radio_studio_integration_tests
    PRODUCT_NAME "raw-radio-studio-integration-tests")

target_sources(raw_radio_studio_integration_tests PRIVATE
    tests/test_integration.cpp
    src/studio/AudioEngine.cpp
    src/studio/AudioImport.cpp
    src/studio/PluginHost.cpp
    src/studio/PluginPresets.cpp
    src/studio/Session.cpp
    src/studio/StemsExport.cpp
    src/studio/TimeStretch.cpp
    src/studio/WavExport.cpp
    # UI sources needed by the plugin-browser layout regression test (BUG 2): the
    # browser is a real component laid out at the minimum window size.
    src/ui/PluginBrowser.cpp
    src/ui/BrandButton.cpp
    src/ui/IconCache.cpp
    src/ui/BrandFonts.cpp)

target_include_directories(raw_radio_studio_integration_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(raw_radio_studio_integration_tests PRIVATE cxx_std_20)

# Mirror the app's JUCE module configuration so the shared engine/JUCE modules
# are compiled consistently across targets.
target_compile_definitions(raw_radio_studio_integration_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=$<BOOL:${RAW_RADIO_STUDIO_ENABLE_LV2}>
    JUCE_STANDALONE_APPLICATION=1)

if(APPLE)
    target_compile_definitions(raw_radio_studio_integration_tests PRIVATE JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(raw_radio_studio_integration_tests PRIVATE
    raw_radio_studio_doctest
    RawRadioStudioAssets
    rrs_signalsmith_stretch
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_warning_flags
    Threads::Threads)

if(APPLE)
    target_compile_options(raw_radio_studio_integration_tests PRIVATE "-fno-aligned-allocation")
elseif(UNIX AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_libraries(raw_radio_studio_integration_tests PRIVATE atomic)
endif()

juce_generate_juce_header(raw_radio_studio_integration_tests)

add_test(NAME raw_radio_studio_integration_tests COMMAND raw_radio_studio_integration_tests)
