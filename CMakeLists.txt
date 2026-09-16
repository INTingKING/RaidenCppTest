cmake_minimum_required(VERSION 3.21)

project(RaidenCppTest
  VERSION 0.1.0
  DESCRIPTION "Cross-platform Vulkan renderer"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

option(RAIDEN_ENABLE_VALIDATION "Enable Vulkan validation layers" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SetupVulkan)
include(Dependencies)
include(Shaders)

add_executable(Raiden
  src/main.cpp
  src/Application.cpp
  src/core/File.cpp
  src/core/Mesh.cpp
  src/core/Paths.cpp
  src/platform/Window.cpp
  src/renderer/VulkanRenderer.cpp
  src/renderer/stb_image_impl.cpp)

add_executable(RaidenCppTest ALIAS Raiden)

target_include_directories(Raiden
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")

target_link_libraries(Raiden
  PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm)

target_compile_definitions(Raiden
  PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
    GLFW_INCLUDE_NONE
    $<$<BOOL:${RAIDEN_ENABLE_VALIDATION}>:RAIDEN_VALIDATION=1>
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN VK_USE_PLATFORM_WIN32_KHR>
    $<$<PLATFORM_ID:Darwin>:VK_ENABLE_BETA_EXTENSIONS>)

if(MSVC)
  target_compile_options(Raiden PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
  target_compile_options(Raiden PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE AND DEFINED ENV{VULKAN_SDK})
  set_target_properties(Raiden PROPERTIES
    BUILD_RPATH   "$ENV{VULKAN_SDK}/lib"
    INSTALL_RPATH "$ENV{VULKAN_SDK}/lib")
endif()

raiden_setup_assets(Raiden)

install(TARGETS Raiden RUNTIME DESTINATION bin)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Assets/" DESTINATION bin/Assets)
