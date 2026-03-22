cmake_minimum_required(VERSION 3.24)

project(TrueFlight LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRUEFLIGHT_ENABLE_STEAMWORKS "Enable optional Steamworks runtime support" OFF)
set(TRUEFLIGHT_STEAM_APP_ID "480" CACHE STRING "Development Steam AppID written to steam_appid.txt when Steamworks is enabled")
set(STEAMWORKS_SDK_ROOT "" CACHE PATH "Path to the Steamworks SDK root or sdk/ directory")

include(FetchContent)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.8.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SDL3)

file(GLOB VULKAN_SDK_HINTS "C:/VulkanSDK/*" "C:/Program Files/VulkanSDK/*" "C:/Program Files (x86)/VulkanSDK/*")

find_program(
    GLSLANG_VALIDATOR_EXECUTABLE
    NAMES glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}"
        ${VULKAN_SDK_HINTS}
    PATH_SUFFIXES
        Bin
        Bin32
)

if(NOT GLSLANG_VALIDATOR_EXECUTABLE)
    message(FATAL_ERROR "glslangValidator was not found. Install the Vulkan SDK or add glslangValidator to PATH.")
endif()

set(NATIVE_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Native/shaders)
set(NATIVE_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/Shaders)
set(NATIVE_SHADER_OUTPUTS)

function(compile_native_shader INPUT_NAME OUTPUT_NAME)
    set(INPUT_PATH ${NATIVE_SHADER_DIR}/${INPUT_NAME})
    set(OUTPUT_PATH ${NATIVE_SHADER_OUTPUT_DIR}/${OUTPUT_NAME})
    add_custom_command(
        OUTPUT ${OUTPUT_PATH}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NATIVE_SHADER_OUTPUT_DIR}
        COMMAND ${GLSLANG_VALIDATOR_EXECUTABLE} -V ${INPUT_PATH} -o ${OUTPUT_PATH}
        DEPENDS ${INPUT_PATH}
        VERBATIM
    )
    set(NATIVE_SHADER_OUTPUTS ${NATIVE_SHADER_OUTPUTS} ${OUTPUT_PATH} PARENT_SCOPE)
endfunction()

compile_native_shader(scene.vert scene.vert.spv)
compile_native_shader(scene.frag scene.frag.spv)
compile_native_shader(hud.vert hud.vert.spv)
compile_native_shader(hud.frag hud.frag.spv)

add_custom_target(NativeShaders ALL DEPENDS ${NATIVE_SHADER_OUTPUTS})

add_executable(TrueFlight
    Native/src/main.cpp
    Native/src/VulkanRenderer.cpp
    Native/src/SteamSupport.cpp
)

add_executable(TrueFlightNativeSmoke
    Native/tests/native_smoke.cpp
    Native/src/VulkanRenderer.cpp
    Native/src/SteamSupport.cpp
)

add_dependencies(TrueFlight NativeShaders)

target_include_directories(TrueFlight
    PRIVATE
        Native/include
        ${sdl3_SOURCE_DIR}/src/render
)

target_include_directories(TrueFlightNativeSmoke
    PRIVATE
        Native/include
        ${sdl3_SOURCE_DIR}/src/render
)

target_link_libraries(TrueFlight
    PRIVATE
        SDL3::SDL3
)

target_link_libraries(TrueFlightNativeSmoke
    PRIVATE
        SDL3::SDL3
)

target_compile_definitions(TrueFlight
    PRIVATE
        _CRT_SECURE_NO_WARNINGS
)

target_compile_definitions(TrueFlightNativeSmoke
    PRIVATE
        _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(TrueFlight PRIVATE /W4 /permissive-)
    target_compile_options(TrueFlightNativeSmoke PRIVATE /W4 /permissive-)
else()
    target_compile_options(TrueFlight PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(TrueFlightNativeSmoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(TrueFlight PRIVATE gdi32 dxgi psapi bcrypt)
    target_link_libraries(TrueFlightNativeSmoke PRIVATE gdi32 dxgi psapi bcrypt)

    add_custom_command(TARGET TrueFlight POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:TrueFlight> $<TARGET_RUNTIME_DLLS:TrueFlight>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:TrueFlight>/Shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${NATIVE_SHADER_OUTPUT_DIR} $<TARGET_FILE_DIR:TrueFlight>/Shaders
        COMMAND_EXPAND_LISTS
    )

    add_custom_command(TARGET TrueFlightNativeSmoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:TrueFlightNativeSmoke> $<TARGET_RUNTIME_DLLS:TrueFlightNativeSmoke>
        COMMAND_EXPAND_LISTS
    )
endif()

if(TRUEFLIGHT_ENABLE_STEAMWORKS)
    if(NOT WIN32)
        message(FATAL_ERROR "TRUEFLIGHT_ENABLE_STEAMWORKS is currently supported on Windows only.")
    endif()

    if(STEAMWORKS_SDK_ROOT STREQUAL "")
        set(STEAMWORKS_SDK_ROOT "${CMAKE_SOURCE_DIR}/steamworks_sdk_164/sdk")
    endif()

    if(IS_DIRECTORY "${STEAMWORKS_SDK_ROOT}/sdk")
        set(STEAMWORKS_SDK_ROOT "${STEAMWORKS_SDK_ROOT}/sdk")
    endif()

    if(NOT EXISTS "${STEAMWORKS_SDK_ROOT}/public/steam/steam_api.h")
        message(FATAL_ERROR "Steamworks was enabled, but no valid SDK root was found at: ${STEAMWORKS_SDK_ROOT}")
    endif()

    set(STEAMWORKS_REDIST_DIR "${STEAMWORKS_SDK_ROOT}/redistributable_bin/win64")
    if(NOT EXISTS "${STEAMWORKS_REDIST_DIR}/steam_api64.lib")
        message(FATAL_ERROR "Steamworks was enabled, but steam_api64.lib was not found in: ${STEAMWORKS_REDIST_DIR}")
    endif()

    foreach(TARGET_NAME TrueFlight TrueFlightNativeSmoke)
        target_include_directories(${TARGET_NAME}
            PRIVATE
                "${STEAMWORKS_SDK_ROOT}/public"
        )
        target_compile_definitions(${TARGET_NAME}
            PRIVATE
                TRUEFLIGHT_ENABLE_STEAMWORKS=1
                TRUEFLIGHT_STEAM_APP_ID=${TRUEFLIGHT_STEAM_APP_ID}
        )
        target_link_libraries(${TARGET_NAME}
            PRIVATE
                "${STEAMWORKS_REDIST_DIR}/steam_api64.lib"
        )
    endforeach()

    set(STEAM_APPID_FILE "${CMAKE_CURRENT_BINARY_DIR}/steam_appid.txt")
    file(WRITE "${STEAM_APPID_FILE}" "${TRUEFLIGHT_STEAM_APP_ID}\n")

    add_custom_command(TARGET TrueFlight POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAM_APPID_FILE}" $<TARGET_FILE_DIR:TrueFlight>/steam_appid.txt
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAMWORKS_REDIST_DIR}/steam_api64.dll" $<TARGET_FILE_DIR:TrueFlight>
        COMMAND_EXPAND_LISTS
    )
    add_custom_command(TARGET TrueFlightNativeSmoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAM_APPID_FILE}" $<TARGET_FILE_DIR:TrueFlightNativeSmoke>/steam_appid.txt
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAMWORKS_REDIST_DIR}/steam_api64.dll" $<TARGET_FILE_DIR:TrueFlightNativeSmoke>
        COMMAND_EXPAND_LISTS
    )
else()
    foreach(TARGET_NAME TrueFlight TrueFlightNativeSmoke)
        target_compile_definitions(${TARGET_NAME}
            PRIVATE
                TRUEFLIGHT_ENABLE_STEAMWORKS=0
                TRUEFLIGHT_STEAM_APP_ID=480
        )
    endforeach()
endif()
