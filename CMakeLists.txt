cmake_minimum_required(VERSION 3.22)

# -----------------------------------------------------------------------------
# raw-radio-studio — free, open-source (AGPLv3) tracking-first DAW
#
# Stack (target): Tracktion Engine + JUCE, C++20, CMake >= 3.22.
# Platforms: macOS 12+ (CoreAudio), Ubuntu 24.04+ (ALSA hw directly).
#
# Epic 0 status: this is a BUILDABLE SKELETON ONLY. The audio engine is not
# wired. The `raw_radio_studio` target is a placeholder that prints a banner.
# JUCE + Tracktion Engine integration is deferred to the C++ specialist.
# -----------------------------------------------------------------------------

project(raw_radio_studio
    VERSION 0.0.0
    DESCRIPTION "raw-radio-studio — open-source (AGPLv3) tracking-first DAW"
    HOMEPAGE_URL "https://github.com/raw-radio/raw-radio-studio"
    LANGUAGES CXX)

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

# --- Options (placeholders; some are consumed in later epics) ----------------
# Epic 3: opt-in LV2 plugin hosting (maps to JUCE_PLUGINHOST_LV2=1).
option(RAW_RADIO_STUDIO_ENABLE_LV2
    "Enable LV2 plugin hosting (Epic 3, opt-in)" OFF)

# -----------------------------------------------------------------------------
# TODO(Epic 0, C++ specialist): add pinned JUCE + Tracktion Engine via
# FetchContent/CPM with pinned commit SHAs.
#
# Rules (see STUDIO_SPEC section 5.8 / 8.1):
#   - Pin EXACT commits; never track a floating branch in CI or release builds.
#   - JUCE upstream: juce-framework/JUCE (branch `develop`, pinned commit).
#   - Tracktion Engine: pinned to an exact commit; integrated as JUCE modules.
#   - Verify JUCE 9 <-> Tracktion Engine compatibility by building
#     (STUDIO_SPEC section 10.3 / OQ-3) and record the result in docs/.
#   - If incompatible, pin an older JUCE and document the constraint.
#
# Sketch (do NOT enable until pins are chosen and verified):
#
#   include(FetchContent)
#   FetchContent_Declare(JUCE
#       GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
#       GIT_TAG        <pinned-juce-commit-sha>)
#   FetchContent_MakeAvailable(JUCE)
#
#   FetchContent_Declare(tracktion_engine
#       GIT_REPOSITORY https://github.com/Tracktion/tracktion_engine.git
#       GIT_TAG        <pinned-tracktion-commit-sha>)
#   FetchContent_MakeAvailable(tracktion_engine)
# -----------------------------------------------------------------------------

# --- Placeholder executable --------------------------------------------------
# Replaced by the real application target once the engine is wired (Epic 0/1).
add_executable(raw_radio_studio src/main.cpp)

target_compile_features(raw_radio_studio PRIVATE cxx_std_20)

target_compile_definitions(raw_radio_studio PRIVATE
    RAW_RADIO_STUDIO_VERSION="${PROJECT_VERSION}")

# TODO(Epic 1+): link the engine/framework once pinned. Example (disabled):
#
#   target_link_libraries(raw_radio_studio PRIVATE
#       juce::juce_gui_basics
#       juce::juce_audio_utils
#       tracktion_engine)
#
# and enable the required JUCE module flags, e.g.:
#   target_compile_definitions(raw_radio_studio PRIVATE
#       JUCE_WEB_BROWSER=0
#       JUCE_USE_CURL=0
#       JUCE_PLUGINHOST_VST3=1
#       JUCE_PLUGINHOST_AU=1            # macOS only
#       JUCE_PLUGINHOST_LV2=$<BOOL:${RAW_RADIO_STUDIO_ENABLE_LV2}>)

if(APPLE)
    # TODO(Epic 0/1, C++ specialist): configure MACOSX_BUNDLE, bundle
    # identifier, Info.plist, and (later) unsigned .dmg packaging.
    # set_target_properties(raw_radio_studio PROPERTIES MACOSX_BUNDLE TRUE)
elseif(UNIX)
    # TODO(Epic 1/3): link ALSA (and later LV2/plugin-hosting libs) here.
    # find_package(ALSA REQUIRED)
    # target_link_libraries(raw_radio_studio PRIVATE ALSA::ALSA)
endif()

# TODO(Epic 0): add CTest targets once there is testable code.
# enable_testing()
