# Copyright Citra Emulator Project / Azahar Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.
#
# Generates ios_build_info.cpp with the *current* git revision, branch and date.
# Unlike scm_rev.cpp (which is produced once at configure time and can go stale on
# incremental builds), this is re-run on every build so the iOS app always reports
# the exact commit it was built from.
#
# Required variables:
#   OUTPUT        - path of the generated .cpp file
#   GIT_EXECUTABLE - path to git (may be empty)

set(COMMIT "unknown")
set(BRANCH "unknown")
set(DATE "unknown")

if(NOT GIT_EXECUTABLE OR NOT EXISTS "${GIT_EXECUTABLE}")
    find_package(Git QUIET)
    if(Git_FOUND)
        set(GIT_EXECUTABLE "${GIT_EXECUTABLE}")
    endif()
endif()

if(GIT_EXECUTABLE AND EXISTS "${GIT_EXECUTABLE}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE COMMIT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT RESULT EQUAL 0)
        set(COMMIT "unknown")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE BRANCH
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT RESULT EQUAL 0)
        set(BRANCH "unknown")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" log -1 --format=%cI
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE DATE
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT RESULT EQUAL 0)
        set(DATE "unknown")
    endif()
endif()

if(DATE STREQUAL "unknown")
    string(TIMESTAMP DATE UTC)
endif()

file(WRITE "${OUTPUT}"
"// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// AUTO-GENERATED at build time by CMakeModules/GenerateIOSBuildInfo.cmake - do not edit.

namespace Azahar {

const char* GetIOSBuildCommit() { return \"${COMMIT}\"; }
const char* GetIOSBuildBranch() { return \"${BRANCH}\"; }
const char* GetIOSBuildDate() { return \"${DATE}\"; }

} // namespace Azahar
")
