function(_raiden_newest_path out_var)
  set(_found "")
  foreach(_pattern IN LISTS ARGN)
    file(GLOB _matches ${_pattern})
    if(_matches)
      list(SORT _matches COMPARE NATURAL ORDER DESCENDING)
      list(GET _matches 0 _head)
      set(_found "${_head}")
      break()
    endif()
  endforeach()
  set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED ENV{VULKAN_SDK} OR NOT EXISTS "$ENV{VULKAN_SDK}")
  if(WIN32)
    _raiden_newest_path(_vk_root "C:/VulkanSDK/*")
    if(_vk_root)
      set(ENV{VULKAN_SDK} "${_vk_root}")
    endif()
  elseif(APPLE)
    _raiden_newest_path(_vk_macos
      "$ENV{HOME}/VulkanSDK/*/macOS"
      "/usr/local/VulkanSDK/*/macOS")
    if(_vk_macos)
      set(ENV{VULKAN_SDK} "${_vk_macos}")
    endif()
  else()
    _raiden_newest_path(_vk_linux
      "$ENV{HOME}/VulkanSDK/*/x86_64"
      "$ENV{HOME}/VulkanSDK/*")
    if(_vk_linux)
      set(ENV{VULKAN_SDK} "${_vk_linux}")
    endif()
  endif()
endif()

if(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}")
  list(PREPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")
  message(STATUS "VULKAN_SDK = $ENV{VULKAN_SDK}")
endif()

find_package(Vulkan REQUIRED)

if(NOT Vulkan_GLSLC_EXECUTABLE AND NOT Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
  message(FATAL_ERROR
    "No GLSL compiler found (glslc or glslangValidator). "
    "Install the LunarG Vulkan SDK, or on Linux the shaderc/glslang packages.")
endif()

if(Vulkan_GLSLC_EXECUTABLE)
  message(STATUS "Shader compiler: ${Vulkan_GLSLC_EXECUTABLE}")
else()
  message(STATUS "Shader compiler: ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
endif()
