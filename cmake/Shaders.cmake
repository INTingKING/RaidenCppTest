function(raiden_setup_assets TARGET_NAME)
  set(SHADER_SRC_DIR "${PROJECT_SOURCE_DIR}/Assets/shaders")
  set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/Assets/shaders")
  file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

  set(_shader_sources
    "${SHADER_SRC_DIR}/mesh.vert"
    "${SHADER_SRC_DIR}/mesh.frag")

  set(_spv_outputs)
  foreach(_shader IN LISTS _shader_sources)
    get_filename_component(_name "${_shader}" NAME)
    set(_spv "${SHADER_OUT_DIR}/${_name}.spv")
    if(Vulkan_GLSLC_EXECUTABLE)
      add_custom_command(
        OUTPUT "${_spv}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" -o "${_spv}" "${_shader}"
        DEPENDS "${_shader}"
        COMMENT "Compiling shader ${_name}"
        VERBATIM)
    else()
      add_custom_command(
        OUTPUT "${_spv}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}" -V "${_shader}" -o "${_spv}"
        DEPENDS "${_shader}"
        COMMENT "Compiling shader ${_name}"
        VERBATIM)
    endif()
    list(APPEND _spv_outputs "${_spv}")
  endforeach()

  add_custom_target(${TARGET_NAME}_shaders ALL DEPENDS ${_spv_outputs})
  add_dependencies(${TARGET_NAME} ${TARGET_NAME}_shaders)

  add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${PROJECT_SOURCE_DIR}/Assets"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>/Assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${SHADER_OUT_DIR}"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>/Assets/shaders"
    COMMENT "Deploying Assets next to ${TARGET_NAME}"
    VERBATIM)
endfunction()
