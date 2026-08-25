add_executable(vulkan-smoke
    VulkanSmoke.cpp)

target_link_libraries(vulkan-smoke PRIVATE
    core
    ${CMAKE_DL_LIBS})

add_executable(capture-layout-smoke
    CaptureLayoutSmoke.cpp)

add_test(NAME vulkan-smoke COMMAND vulkan-smoke)
add_test(NAME capture-layout-smoke COMMAND capture-layout-smoke)
set_tests_properties(vulkan-smoke capture-layout-smoke PROPERTIES
    LABELS vulkan
    TIMEOUT 120)

if (APPLE)
    find_library(MOLTENVK_TEST_LIBRARY
        NAMES MoltenVK
        HINTS
            "${PROJECT_SOURCE_DIR}/external/moltenvk"
            "$ENV{VULKAN_SDK}/lib"
        PATHS
            /opt/homebrew/lib
            /usr/local/lib)
    if (MOLTENVK_TEST_LIBRARY)
        configure_file(
            MoltenVK_icd.json.in
            MoltenVK_icd.json
            @ONLY)
        set_tests_properties(vulkan-smoke PROPERTIES
            ENVIRONMENT "VK_DRIVER_FILES=${CMAKE_CURRENT_BINARY_DIR}/MoltenVK_icd.json")
    else()
        message(WARNING
            "MoltenVK was not found; vulkan-smoke will require a Vulkan ICD configured in the environment")
    endif()
endif()
