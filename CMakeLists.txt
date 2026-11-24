cmake_minimum_required(VERSION 3.20)
project(minicraft_cpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enforce clean compilation warnings.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>
  )
endif()

# ---------------------------------------------------------------------------
# Pak Codec & Packer Tool
# ---------------------------------------------------------------------------
add_library(minicraft_pak STATIC
  src/core/pak/fast_compress.cc
  src/core/pak/pak_reader.cc
)
target_include_directories(minicraft_pak PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(MINICRAFT_SHADER_NAMES
  sprite.vert
  sprite.frag
  light.vert
  light.frag
  quad.vert
  dither.frag
  blit.frag
)
set(MINICRAFT_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(MINICRAFT_SPV_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(MINICRAFT_SHADER_SOURCES "")
foreach(_shader_name IN LISTS MINICRAFT_SHADER_NAMES)
  list(APPEND MINICRAFT_SHADER_SOURCES "${MINICRAFT_SHADER_DIR}/${_shader_name}")
endforeach()

set(MINICRAFT_GENERATED_SHADERS_CC
  "${CMAKE_CURRENT_BINARY_DIR}/generated/precompiled_shaders.cc"
)

find_program(GLSLANG_VALIDATOR glslangValidator
  HINTS "C:/msys64/mingw64/bin" "C:/devkitPro/tools/bin"
  NO_CMAKE_FIND_ROOT_PATH
  REQUIRED
)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
  add_custom_command(
    OUTPUT "${MINICRAFT_GENERATED_SHADERS_CC}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND ${CMAKE_COMMAND}
      "-DGLSLANG=${GLSLANG_VALIDATOR}"
      "-DOUTPUT=${MINICRAFT_GENERATED_SHADERS_CC}"
      "-DSHADER_DIR=${MINICRAFT_SHADER_DIR}"
      "-DSPV_DIR=${MINICRAFT_SPV_DIR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/switch/cmake/generate_shaders.cmake"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/switch/cmake/generate_shaders.cmake"
      ${MINICRAFT_SHADER_SOURCES}
    COMMENT "Precompiling SPIR-V shaders and generating Switch shader table"
  )
else()
  add_executable(pak_packer
    tools/pak_packer.cc
  )
  target_include_directories(pak_packer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
  )
  target_link_libraries(pak_packer PRIVATE minicraft_pak)

  add_executable(embed_shaders
    tools/embed_shaders.cc
  )

  set(MINICRAFT_SPV_FILES "")
  foreach(_shader_name IN LISTS MINICRAFT_SHADER_NAMES)
    set(_src_shader "${MINICRAFT_SHADER_DIR}/${_shader_name}")
    set(_out_spv "${MINICRAFT_SPV_DIR}/${_shader_name}.spv")
    add_custom_command(
      OUTPUT "${_out_spv}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${MINICRAFT_SPV_DIR}"
      COMMAND "${GLSLANG_VALIDATOR}" -G -o "${_out_spv}" "${_src_shader}"
      DEPENDS "${_src_shader}"
      COMMENT "Precompiling SPIR-V shader ${_shader_name}"
    )
    list(APPEND MINICRAFT_SPV_FILES "${_out_spv}")
  endforeach()

  add_custom_command(
    OUTPUT "${MINICRAFT_GENERATED_SHADERS_CC}"
    COMMAND $<TARGET_FILE:embed_shaders>
      "${MINICRAFT_GENERATED_SHADERS_CC}"
      "${MINICRAFT_SHADER_DIR}"
      "${MINICRAFT_SPV_DIR}"
      ${MINICRAFT_SHADER_NAMES}
    DEPENDS embed_shaders ${MINICRAFT_SPV_FILES} ${MINICRAFT_SHADER_SOURCES}
    COMMENT "Generating precompiled shader table"
  )
endif()

# ---------------------------------------------------------------------------
# Core Library
# ---------------------------------------------------------------------------
add_library(minicraft_core STATIC
  src/core/util/logging.cc
  src/core/util/simplex.cc
  src/core/util/localization.cc
  src/core/util/book_data.cc
  src/core/util/advancements.cc
  src/core/gfx/color.cc
  src/core/gfx/geometry.cc
  src/core/gfx/sprite_atlas.cc
  src/core/gfx/screen.cc
  src/core/gfx/font.cc
  src/core/gfx/font_style.cc
  src/core/gfx/ellipsis.cc
  src/core/gfx/sprite_animation.cc
  src/core/io/settings.cc
  src/core/io/input_handler.cc
  src/core/io/sound.cc
  src/core/game/game_state.cc
  src/core/level/level_noise.cc
  src/core/level/biome.cc
  src/core/level/tile.cc
  src/core/level/tiles_nature.cc
  src/core/level/tiles_building.cc
  src/core/level/tiles_farming.cc
  src/core/level/tiles.cc
  src/core/level/chunk_manager.cc
  src/core/level/structure.cc
  src/core/level/level_gen.cc
  src/core/level/level.cc
  src/core/entity/entity.cc
  src/core/entity/particles.cc
  src/core/entity/projectiles.cc
  src/core/entity/boat.cc
  src/core/entity/furniture.cc
  src/core/entity/mob.cc
  src/core/entity/player.cc
  src/core/item/item.cc
  src/core/item/inventory.cc
  src/core/item/items.cc
  src/core/item/recipes.cc
  src/core/item/fishing_data.cc
  src/core/saveload/version.cc
  src/core/saveload/save.cc
  src/core/saveload/load.cc
  src/core/saveload/legacy_load.cc
  src/core/screen/entries.cc
  src/core/screen/menu.cc
  src/core/screen/display.cc
  src/core/screen/displays_main.cc
  src/core/screen/displays_game.cc
  src/core/game/world.cc
  src/core/game/updater.cc
  src/core/game/renderer.cc
  src/core/game/initializer.cc
  src/core/game/game.cc
)
target_include_directories(minicraft_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
)
target_link_libraries(minicraft_core PUBLIC minicraft_pak)

# ---------------------------------------------------------------------------
# OpenGL Loader (shared across OpenGL backends)
# ---------------------------------------------------------------------------
add_library(glad STATIC
  third_party/glad/src/glad.c
)
target_include_directories(glad PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include
)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
  # GCC 16 libstdc++ no longer pulls global size_t transitively everywhere.
  target_compile_options(minicraft_core PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include stddef.h>
  )
  # glad.c's unused gladLoadGL() fallback includes <dlfcn.h> when !WIN32;
  # Switch uses gladLoadGLLoader(eglGetProcAddress) directly.
  target_include_directories(glad PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/switch/compat"
  )

  # -------------------------------------------------------------------------
  # Nintendo Switch Platform Implementation
  # -------------------------------------------------------------------------
  add_library(minicraft_switch_platform STATIC
    src/platform/switch/switch_clock.cc
    src/platform/switch/switch_file_system.cc
    src/platform/switch/switch_audio_backend.cc
    src/platform/switch/switch_system.cc
    src/platform/switch/switch_window.cc
    src/platform/switch/switch_render_backend.cc
    src/platform/switch/switch_splash.cc
    ${MINICRAFT_GENERATED_SHADERS_CC}
  )
  target_include_directories(minicraft_switch_platform
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_include_directories(minicraft_switch_platform SYSTEM
    PUBLIC
      "${DKP_INSTALL_PREFIX_INIT}/include"
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
  )
  target_link_directories(minicraft_switch_platform PUBLIC
    "${DKP_INSTALL_PREFIX_INIT}/lib"
  )
  target_link_libraries(minicraft_switch_platform PUBLIC
    minicraft_core
    glad
    EGL
    glapi
    drm_nouveau
    nx
    m
  )

  add_executable(minicraft_switch
    src/platform/switch/main.cc
  )
  target_link_libraries(minicraft_switch PRIVATE minicraft_switch_platform)

  set(MINICRAFT_SWITCH_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
  file(MAKE_DIRECTORY "${MINICRAFT_SWITCH_ROMFS_DIR}")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/assets.pak")
    add_custom_command(
      OUTPUT "${MINICRAFT_SWITCH_ROMFS_DIR}/assets.pak"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/assets.pak"
        "${MINICRAFT_SWITCH_ROMFS_DIR}/assets.pak"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/assets.pak"
      COMMENT "Staging assets.pak into Switch RomFS"
    )
    add_custom_target(minicraft_switch_romfs
      DEPENDS "${MINICRAFT_SWITCH_ROMFS_DIR}/assets.pak"
    )
    add_dependencies(minicraft_switch minicraft_switch_romfs)
  endif()

  nx_generate_nacp(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/minicraft_switch.nacp"
    NAME "Minicraft+"
    AUTHOR "Minicraft+ Contributors"
    VERSION "2.3.0"
  )
  nx_create_nro(minicraft_switch
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/minicraft_switch.nro"
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/switch/icon.jpg"
    NACP "${CMAKE_CURRENT_BINARY_DIR}/minicraft_switch.nacp"
    ROMFS "${MINICRAFT_SWITCH_ROMFS_DIR}"
  )
else()
  # -------------------------------------------------------------------------
  # PC Platform Implementation
  # -------------------------------------------------------------------------
  find_package(glfw3 REQUIRED)

  set(MINICRAFT_GLFW_TARGET glfw)
  if(WIN32)
    # Prefer static GLFW so end users do not need glfw3.dll.
    get_target_property(_glfw_inc glfw INTERFACE_INCLUDE_DIRECTORIES)
    get_filename_component(_glfw_lib_hint "${_glfw_inc}/../lib" ABSOLUTE)
    find_library(GLFW_STATIC_LIB
      NAMES libglfw3.a glfw3_static
      HINTS "${_glfw_lib_hint}"
    )
    if(GLFW_STATIC_LIB)
      add_library(glfw_static STATIC IMPORTED)
      set_target_properties(glfw_static PROPERTIES
        IMPORTED_LOCATION "${GLFW_STATIC_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${_glfw_inc}"
      )
      set(MINICRAFT_GLFW_TARGET glfw_static)
    endif()
  endif()

  add_library(minicraft_pc_platform STATIC
    src/platform/pc/pc_clock.cc
    src/platform/pc/pc_file_system.cc
    src/platform/pc/miniaudio_backend.cc
    src/platform/pc/pc_splash.cc
    src/platform/pc/pc_window.cc
    src/platform/pc/opengl_render_backend.cc
    ${MINICRAFT_GENERATED_SHADERS_CC}
  )
  target_include_directories(minicraft_pc_platform
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
  )
  target_link_libraries(minicraft_pc_platform PUBLIC
    minicraft_core
    glad
    ${MINICRAFT_GLFW_TARGET}
  )
  if(WIN32)
    target_link_libraries(minicraft_pc_platform PUBLIC
      opengl32
      gdi32
      user32
      shell32
      winmm
      ole32
    )
  endif()

  add_executable(minicraft_pc
    src/platform/pc/main.cc
  )
  target_link_libraries(minicraft_pc PRIVATE minicraft_pc_platform)
  if(WIN32)
    enable_language(RC)
    target_sources(minicraft_pc PRIVATE src/platform/pc/minicraft.rc)
    # Hide the background console window in Release builds while keeping main().
    set_target_properties(minicraft_pc PROPERTIES
      WIN32_EXECUTABLE "$<CONFIG:Release,RelWithDebInfo,MinSizeRel>"
    )
    if(MSVC)
      target_link_options(minicraft_pc PRIVATE
        "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/ENTRY:mainCRTStartup>"
      )
    endif()
  endif()

  add_executable(minicraft_tests
    tests/core_tests.cc
  )
  target_link_libraries(minicraft_tests PRIVATE minicraft_pc_platform)

  if(MINGW)
    foreach(_exe_target IN ITEMS embed_shaders pak_packer minicraft_pc minicraft_tests)
      target_link_options(${_exe_target} PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
      )
    endforeach()
  endif()

  add_custom_command(TARGET minicraft_pc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:minicraft_pc>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${MINICRAFT_SPV_FILES}
      "$<TARGET_FILE_DIR:minicraft_pc>/shaders"
    COMMENT "Copying precompiled SPIR-V shaders alongside minicraft_pc"
  )

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/assets.pak")
    add_custom_command(TARGET minicraft_pc POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/assets.pak"
        "$<TARGET_FILE_DIR:minicraft_pc>/assets.pak"
      COMMENT "Copying assets.pak alongside minicraft_pc executable"
    )
  endif()
endif()
