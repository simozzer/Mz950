cmake_minimum_required(VERSION 3.22)

project(VirtualS950 VERSION 0.6.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#
# JUCE lives beside the repository rather than inside it: a few hundred megabytes that
# every JUCE project on the machine can share, and nothing for this repository to carry.
# Override with -DJUCE_PATH=... if it is somewhere else.
#
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE" CACHE PATH "Where JUCE is checked out")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "No JUCE at ${JUCE_PATH}.\n"
        "  cd ${CMAKE_CURRENT_SOURCE_DIR}/../..\n"
        "  git clone --depth 1 https://github.com/juce-framework/JUCE\n"
        "or pass -DJUCE_PATH=<wherever it is>.")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

#
# Where a built plugin gets installed.
#
# JUCE's default is C:\Program Files\Common Files\VST3, which cannot be written to - or
# even created - without elevation, so every build would fail at the last step unless the
# whole thing ran as administrator. Building as administrator to test an audio plugin is
# the wrong trade.
#
# This is the other location the VST3 specification names, the per-user one, which needs no
# elevation. Hosts differ on whether they scan it by default: in Ableton it goes in
# Preferences - Plug-Ins - VST3 Plug-In Custom Folder, once.
#
# On a Mac the per-user folder is ~/Library/Audio/Plug-Ins/VST3, which every host scans; on
# Linux it is ~/.vst3. Neither needs elevation either.
#
if(WIN32)
    set(VST3_DEFAULT "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
elseif(APPLE)
    set(VST3_DEFAULT "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
else()
    set(VST3_DEFAULT "$ENV{HOME}/.vst3")
endif()

set(VST3_DESTINATION "${VST3_DEFAULT}" CACHE PATH "Where to install the built VST3")

#
# The icon, which JUCE wants as PNGs rather than as the .ico Windows executables carry -
# it builds the platform resource itself from these. Both come out of Icon\build-icon.ps1
# alongside the .ico, from the same two drawings, so the plugin and the editor cannot end
# up wearing different faces.
#
set(ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Icon")

#
# VST3 and the standalone everywhere; on a Mac an Audio Unit as well, because Logic and
# GarageBand load nothing else. The AU's identity is the same two codes as the VST3's.
#
set(MZ950_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MZ950_FORMATS AU)
endif()

# The icon PNGs come out of Icon\build-icon.ps1, which needs Windows. A build without them
# - the Mac job, or a fresh clone anywhere else - goes without an icon rather than failing.
set(MZ950_ICONS)
if(EXISTS "${ICON_DIR}/icon-1024.png" AND EXISTS "${ICON_DIR}/icon-128.png")
    set(MZ950_ICONS ICON_BIG "${ICON_DIR}/icon-1024.png" ICON_SMALL "${ICON_DIR}/icon-128.png")
endif()

#
# THE NAME AND THE IDENTITY ARE TWO DIFFERENT THINGS
#
# PRODUCT_NAME is what a player sees: the plugin in a host's browser, the .vst3 file, the
# standalone's window. It was "VirtualS950" and is now "Mz950", so the product no longer
# wears another company's model name - Akai and S950 are inMusic's trademarks, and this is
# an independent project that is merely compatible with their disks.
#
# The two codes below are the plugin's IDENTITY, and must NEVER change. A VST3's class ID
# is made from them and nothing else (JUCE: VST3Interface::jucePluginId), and a host finds
# the plugin in a saved project by that ID. Change either and every Live set ever made with
# this plugin opens with it missing. "Ak95" is only four letters inside the binary; nobody
# sees it.
#
# The CMake target is still called VirtualS950 for the same kind of reason: it names build
# folders and scripts, not anything a player sees, and renaming it buys nothing.
#
juce_add_plugin(VirtualS950
    COMPANY_NAME                "simozzer"
    PRODUCT_NAME                "Mz950"
    PLUGIN_MANUFACTURER_CODE    Smoz          # never change - see above
    PLUGIN_CODE                 Ak95          # never change - see above

    # Explicitly, because JUCE does NOT inherit it from project(). Without this the VST3
    # reported 0.1.0 - JUCE's own default - while the project said 0.3.0 and the installer
    # said 0.3.0, so the one version a DAW actually shows the player was the wrong one.
    VERSION                     "${PROJECT_VERSION}"

    ${MZ950_ICONS}

    VST3_COPY_DIR               "${VST3_DESTINATION}"

    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    # Installed by install_vst3.cmake below rather than by JUCE, so that a host holding the
    # old binary open is a sentence rather than a failed build. See that file.
    COPY_PLUGIN_AFTER_BUILD     FALSE

    # Standalone as well as VST3: it opens without a DAW, which makes "is the plugin
    # broken or is the host unhappy with it" answerable in one step rather than two.
    FORMATS                     ${MZ950_FORMATS})

target_sources(VirtualS950 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ProgramPage.cpp
    Source/SynthPage.cpp
    Source/Look.cpp
    Source/NativeFileDialog.cpp
    Source/Demo.cpp
    Source/S950/Voice.cpp
    Source/S950/Engine.cpp
    Source/S950/Disk.cpp
    Source/S950/Synth.cpp
    Source/S950/SynthPatch.cpp
    Source/S950/Hfe.cpp)

target_include_directories(VirtualS950 PRIVATE Source)

#
# Install the built bundle where a host will find it, without the build failing when the
# host has the old one open. install_vst3.cmake explains why that is worth the detour.
#
add_custom_command(TARGET VirtualS950_VST3 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DSRC=${CMAKE_CURRENT_BINARY_DIR}/VirtualS950_artefacts/$<CONFIG>/VST3/Mz950.vst3"
            "-DDST=${VST3_DESTINATION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install_vst3.cmake"
    VERBATIM)

target_compile_definitions(VirtualS950 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VirtualS950
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#
# The conformance check, built by the same configure so it cannot be forgotten.
#
# It links no JUCE at all, which is the point: if the engine disagrees with the C# it came
# from, nothing built on top of it is worth debugging. Run it before the plugin.
#
add_executable(ConformanceCheck
    Tests/ConformanceCheck.cpp
    Source/S950/Voice.cpp
    Source/S950/Engine.cpp)

target_include_directories(ConformanceCheck PRIVATE Source/S950 Tests)

#
# The disk writer's check, likewise JUCE-free. build.ps1 compiles it by hand on Windows; this
# target is for everywhere else - the Mac job runs it on the sound library the Windows job
# wrote, so a disk edited on a Mac is held to the same byte-for-byte round trip.
#
add_executable(DiskEditCheck
    Tests/DiskEditCheck.cpp
    Source/S950/Disk.cpp
    Source/S950/Synth.cpp
    Source/S950/SynthPatch.cpp
    Source/S950/Hfe.cpp)

target_include_directories(DiskEditCheck PRIVATE Source/S950 Tests)

enable_testing()
add_test(NAME conformance COMMAND ConformanceCheck)
