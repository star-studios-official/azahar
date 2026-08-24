# Azahar iOS app — built only when targeting iOS.
# Uses the same C++ core as the Qt/Android builds but with a native SwiftUI
# frontend. To build:
#   cmake -S . -B build-ios -G Xcode \
#       -DCMAKE_SYSTEM_NAME=iOS \
#       -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
#       -DCMAKE_OSX_ARCHITECTURES=arm64 \
#       -DENABLE_QT=OFF -DENABLE_LIBRETRO=OFF \
#       -DENABLE_TESTS=OFF
#   cmake --build build-ios --config Release

if(NOT IOS OR ENABLE_LIBRETRO)
    return()
endif()

# ---------------------------------------------------------------------------
# azahar_ios — the C++/ObjC++ bridge (equivalent to citra-android)
# ---------------------------------------------------------------------------

# Fresh git build info, regenerated on every build so the log always shows the exact
# commit/date the app was built from (scm_rev.cpp is produced at configure time and can
# go stale on incremental builds).
find_package(Git QUIET)
set(IOS_BUILD_INFO_SRC "${CMAKE_CURRENT_BINARY_DIR}/ios_build_info.cpp")
add_custom_command(
    OUTPUT "${IOS_BUILD_INFO_SRC}"
    COMMAND "${CMAKE_COMMAND}"
            -DOUTPUT="${IOS_BUILD_INFO_SRC}"
            -DGIT_EXECUTABLE="${GIT_EXECUTABLE}"
            -DCMAKE_CURRENT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_SOURCE_DIR}/CMakeModules/GenerateIOSBuildInfo.cmake"
    DEPENDS "${CMAKE_SOURCE_DIR}/CMakeModules/GenerateIOSBuildInfo.cmake"
    COMMENT "Generating iOS build info (git commit/date)"
    VERBATIM)
set_source_files_properties("${IOS_BUILD_INFO_SRC}" PROPERTIES GENERATED TRUE)

add_library(azahar_ios STATIC
    "${IOS_BUILD_INFO_SRC}"
    AzaharBridge/ios_bridge.mm
    AzaharBridge/EmuWindowIOS.h
    AzaharBridge/EmuWindowIOS.mm
    AzaharBridge/config_ios.h
    AzaharBridge/config_ios.cpp
    AzaharBridge/default_ini_ios.h
    AzaharBridge/input_manager_ios.h
    AzaharBridge/input_manager_ios.cpp
    AzaharBridge/motion_ios.h
    AzaharBridge/motion_ios.mm
    AzaharBridge/logging_ios.h
    AzaharBridge/logging_ios.cpp
    AzaharBridge/camera_ios.h
    AzaharBridge/camera_ios.mm
    AzaharBridge/applets_ios.h
    AzaharBridge/applets_ios.mm
)

target_include_directories(azahar_ios PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(azahar_ios PRIVATE
    citra_common citra_core video_core audio_core input_common citra_network inih
)

# Define CITRA_IOS preprocessor macro so C++ code can detect iOS platform
# (Can't use just "IOS" as it conflicts with Camera::IOS namespace)
target_compile_definitions(azahar_ios PUBLIC CITRA_IOS)

# ObjC++ files need ARC and the ObjC++ standard
set_target_properties(azahar_ios PROPERTIES
    OBJCXX_STANDARD 20
    CXX_STANDARD 20
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
)

# ---------------------------------------------------------------------------
# Swift sources — enumerated explicitly (no globbing, per repo convention)
# ---------------------------------------------------------------------------
set(AZAHAR_SWIFT_SOURCES
    AzaharApp/AzaharApp.swift
    AzaharApp/AppDelegate.swift
    AzaharApp/SceneDelegate.swift
    AzaharApp/ExternalSceneDelegate.swift
    AzaharApp/AzaharBridge.swift
    AzaharApp/Models/Game.swift
    AzaharApp/Models/WhatsNewEntry.swift
    AzaharApp/Models/TouchControlSettings.swift
    AzaharApp/ViewModels/EmulationViewModel.swift
    AzaharApp/Views/RootView.swift
    AzaharApp/Views/GameListView.swift
    AzaharApp/Views/GameGridView.swift
    AzaharApp/Views/GamePropertiesView.swift
    AzaharApp/Views/PerGameSettingsView.swift
    AzaharApp/Views/EmulationView.swift
    AzaharApp/Views/EmulationLoadingView.swift
    AzaharApp/Views/MetalView.swift
    AzaharApp/Views/TouchControlsView.swift
    AzaharApp/Views/AmiiboFilePicker.swift
    AzaharApp/Views/SaveStateDialog.swift
    AzaharApp/Views/ControllerRemapperView.swift
    AzaharApp/Views/Settings/SettingsView.swift
    AzaharApp/Views/Settings/SystemInfoView.swift
    AzaharApp/Views/Settings/JITSettingsView.swift
    AzaharApp/Views/Settings/LogViewerView.swift
    AzaharApp/Views/Applets/SoftwareKeyboardView.swift
    AzaharApp/Views/Applets/MiiSelectorView.swift
    AzaharApp/Views/NetPlayView.swift
    AzaharApp/Views/CheatsView.swift
    AzaharApp/Views/SystemFilesView.swift
    AzaharApp/Views/SystemFilesDownloaderView.swift
    AzaharApp/Views/RetroAchievements/RetroAchievementsView.swift
    AzaharApp/Views/RetroAchievements/AchievementListView.swift
    AzaharApp/Views/RetroAchievements/LeaderboardListView.swift
    AzaharApp/Views/RetroAchievements/RANotificationView.swift
    AzaharApp/Views/AboutView.swift
    AzaharApp/Views/WhatsNewView.swift
    AzaharApp/Managers/ControllerManager.swift
    AzaharApp/Managers/SaveStateManager.swift
    AzaharApp/Managers/CompatibilityManager.swift
    AzaharApp/Utilities/GamepadManager.swift
    AzaharApp/Utilities/UpdateChecker.swift
    AzaharApp/Utilities/SMDHLoader.swift
    AzaharApp/Utilities/JITEnableContext.swift
    AzaharApp/Utilities/EntitlementChecker.swift
    AzaharApp/Utilities/DisplayManager.swift
    AzaharApp/Utilities/LocalPlayManager.swift
    AzaharApp/Utilities/AppLogger.swift
)

# ---------------------------------------------------------------------------
# Swift support for the iOS app target.
# The Xcode generator (recommended) handles Swift natively: SDK path, target
# triple, bridging header, and framework linking are all derived from Xcode
# build settings. The Ninja generator needs manual help, so all the plumbing
# below is guarded to non-Xcode generators only.
# ---------------------------------------------------------------------------
enable_language(Swift)

if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    # For iOS cross-compilation swiftc needs the target triple; set it before
    # enable_language so CMake passes it to both swiftc and the linker.
    if(NOT CMAKE_Swift_COMPILER_TARGET AND CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_Swift_COMPILER_TARGET
            "${CMAKE_OSX_ARCHITECTURES}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()

    # Ensure Swift compiler uses the correct iOS SDK
    if(NOT CMAKE_Swift_FLAGS_INIT AND CMAKE_OSX_SYSROOT)
        set(CMAKE_Swift_FLAGS_INIT "-sdk ${CMAKE_OSX_SYSROOT}")
    endif()

    # Swift doesn't recognize -pthread or -Wl, style flags; they come from C++
    # libraries. Use a Python wrapper script to filter/transform them (Python
    # is more reliable than bash for complex argument parsing).
    set(_SWIFTC_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/swiftc_wrapper.py")
    if(NOT EXISTS "${_SWIFTC_WRAPPER}")
        message(FATAL_ERROR "Swift wrapper script not found: ${_SWIFTC_WRAPPER}")
    endif()

    # Override CMAKE_Swift_LINK_EXECUTABLE for this directory scope
    set(CMAKE_Swift_LINK_EXECUTABLE
        "${_SWIFTC_WRAPPER} <CMAKE_Swift_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
    )
endif()

add_executable(azahar_ios_app MACOSX_BUNDLE
    ${AZAHAR_SWIFT_SOURCES}
)

set_target_properties(azahar_ios_app PROPERTIES
    OUTPUT_NAME "Azahar"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "org.azahar_emu.Azahar"
    MACOSX_BUNDLE_BUNDLE_IDENTIFIER "org.azahar_emu.Azahar"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.azahar_emu.Azahar"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/Info.plist"
    MACOSX_BUNDLE_ICON_FILE "AppIcon"
    MACOSX_BUNDLE_LAUNCH_SCREEN_STORYBOARD ""
    MACOSX_BUNDLE_SCREEN_ORIENTATIONS "UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight UIInterfaceOrientationPortrait"
    # Asset catalog (app icon). Xcode compiles it via actool and embeds it.
    MACOSX_BUNDLE_RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/Assets.xcassets"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/AzaharBridge/azahar_ios.h"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleDisplayName "Azahar"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UIApplicationSceneManifest_Generation YES
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UIApplicationSupportsIndirectInputEvents YES
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UISupportedInterfaceOrientations "~UIInterfaceOrientationPortrait"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_UILaunchScreen_Generation YES
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/Azahar.entitlements"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    LINKER_LANGUAGE Swift
)

# Code signing configuration — can be overridden via CMake variables or environment
# Default to disabled for local builds; CI can enable by passing -DIOS_CODE_SIGN_IDENTITY
if(DEFINED ENV{IOS_CODE_SIGN_IDENTITY})
    set(IOS_CODE_SIGN_IDENTITY "$ENV{IOS_CODE_SIGN_IDENTITY}" CACHE STRING "iOS code signing identity")
endif()
if(DEFINED ENV{IOS_DEVELOPMENT_TEAM})
    set(IOS_DEVELOPMENT_TEAM "$ENV{IOS_DEVELOPMENT_TEAM}" CACHE STRING "iOS development team ID")
endif()
if(DEFINED ENV{IOS_PROVISIONING_PROFILE_SPECIFIER})
    set(IOS_PROVISIONING_PROFILE_SPECIFIER "$ENV{IOS_PROVISIONING_PROFILE_SPECIFIER}" CACHE STRING "iOS provisioning profile specifier")
endif()

if(IOS_CODE_SIGN_IDENTITY)
    set_target_properties(azahar_ios_app PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${IOS_CODE_SIGN_IDENTITY}"
    )
    if(IOS_DEVELOPMENT_TEAM)
        set_target_properties(azahar_ios_app PROPERTIES
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
        )
    endif()
    if(IOS_PROVISIONING_PROFILE_SPECIFIER)
        set_target_properties(azahar_ios_app PROPERTIES
            XCODE_ATTRIBUTE_INFOPLIST_KEY_UIAPPLICATIONSCENEMANIFEST_GENERATION "YES"
            XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/Azahar.entitlements"
        )
    endif()
else()
    # Default: no code signing for local dev builds
    set_target_properties(azahar_ios_app PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )
endif()

# With the Ninja generator the XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER is
# ignored; pass it to swiftc directly so the az_* C API is visible to Swift.
# Also pass the SDK path explicitly so Swift can find UIKit and other iOS
# frameworks. Xcode derives both from its own build settings.
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    target_compile_options(azahar_ios_app PRIVATE
        $<$<COMPILE_LANGUAGE:Swift>:-import-objc-header;$<TARGET_PROPERTY:azahar_ios_app,XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER>>
        $<$<COMPILE_LANGUAGE:Swift>:-sdk;${CMAKE_OSX_SYSROOT}>
    )
endif()

target_link_libraries(azahar_ios_app PRIVATE azahar_ios)

# Ensure CITRA_IOS preprocessor macro is available to all linked targets
target_compile_definitions(azahar_ios_app PRIVATE CITRA_IOS)

# Add WhatsNew.json and compatibility_list.json as bundle resources
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/WhatsNew.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/compatibility_list.json"
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
)
target_sources(azahar_ios_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/WhatsNew.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/compatibility_list.json"
)

# Add button PNG assets as bundle resources
file(GLOB BUTTON_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/Buttons/*.png")
set_source_files_properties(
    ${BUTTON_ASSETS}
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
)
target_sources(azahar_ios_app PRIVATE ${BUTTON_ASSETS})

# Bundle MoltenVK framework into the iOS app
# The framework will be copied to Azahar.app/Frameworks/MoltenVK.framework
if (ENABLE_VULKAN)
    set(MOLTENVK_FRAMEWORK "${CMAKE_BINARY_DIR}/externals/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework")
    if (EXISTS "${MOLTENVK_FRAMEWORK}")
        set_target_properties(azahar_ios_app PROPERTIES
            XCODE_EMBED_FRAMEWORKS "${MOLTENVK_FRAMEWORK}"
            XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
        )
        message(STATUS "MoltenVK framework will be embedded in iOS app bundle")
        
        # Also copy the framework binary as a standalone dylib for maximum compatibility
        # This provides a fallback if framework loading fails
        set(MOLTENVK_BINARY "${MOLTENVK_FRAMEWORK}/MoltenVK")
        if (EXISTS "${MOLTENVK_BINARY}")
            add_custom_command(TARGET azahar_ios_app POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${MOLTENVK_BINARY}"
                    "$<TARGET_BUNDLE_DIR:azahar_ios_app>/Frameworks/libMoltenVK.dylib"
                COMMENT "Copying MoltenVK binary as dylib fallback"
            )
            message(STATUS "MoltenVK dylib fallback will also be copied")
        endif()
    else()
        message(WARNING "MoltenVK framework not found at ${MOLTENVK_FRAMEWORK}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Code signing for on-device deployment (generator-agnostic).
# Set AZAHAR_CODE_SIGN_IDENTITY (e.g. "Apple Development") and optionally
# AZAHAR_PROVISIONING_PROFILE to an absolute .mobileprovision path at
# configure time.  Both can be left empty to skip signing (CI / simulator).
# ---------------------------------------------------------------------------
set(_SIGN_ID "${AZAHAR_CODE_SIGN_IDENTITY}" CACHE STRING
    "Code-sign identity for iOS device builds (leave empty to skip)")
set(_PROVISION "${AZAHAR_PROVISIONING_PROFILE}" CACHE FILEPATH
    "Path to a .mobileprovision file for device builds (optional)")
set(_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/AzaharApp/Resources/Azahar.entitlements")

if(_SIGN_ID)
    set(_SIGN_ARGS -s "${_SIGN_ID}" --entitlements "${_ENTITLEMENTS}")
    if(_PROVISION)
        list(APPEND _SIGN_ARGS --provisioning-profile "${_PROVISION}")
    endif()
    add_custom_command(TARGET azahar_ios_app POST_BUILD
        COMMAND /usr/bin/codesign --force --deep --timestamp
                ${_SIGN_ARGS}
                "$<TARGET_BUNDLE_DIR:azahar_ios_app>"
        COMMENT "Code-signing Azahar.app with identity '${_SIGN_ID}'"
    )
endif()
