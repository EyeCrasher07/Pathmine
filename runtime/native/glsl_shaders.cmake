find_program(REMIX_GLSLANG glslangValidator REQUIRED)

set(legacyShaderRoot "${CMAKE_CURRENT_LIST_DIR}/../src/dxvk")
file(GLOB legacyShaders CONFIGURE_DEPENDS
  "${legacyShaderRoot}/shaders/*.comp"
  "${legacyShaderRoot}/shaders/*.frag"
  "${legacyShaderRoot}/shaders/*.vert"
  "${legacyShaderRoot}/shaders/*.geom"
  "${legacyShaderRoot}/hud/shaders/*.frag"
  "${legacyShaderRoot}/hud/shaders/*.vert")
set(legacyShaderOutput "${CMAKE_CURRENT_BINARY_DIR}/glsl_shaders")
file(MAKE_DIRECTORY "${legacyShaderOutput}")
set(legacyHeaders)
foreach(shader IN LISTS legacyShaders)
  get_filename_component(shaderName "${shader}" NAME_WE)
  set(output "${legacyShaderOutput}/${shaderName}.h")
  add_custom_command(OUTPUT "${output}"
    COMMAND "${REMIX_GLSLANG}" -V --vn "${shaderName}" "${shader}"
      -o "${output}" --target-env vulkan1.2 --quiet
    DEPENDS "${shader}" VERBATIM)
  list(APPEND legacyHeaders "${output}")
endforeach()
add_custom_target(remix-glsl-shaders DEPENDS ${legacyHeaders})
