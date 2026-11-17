cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{DEVKITPRO} AND NOT "$ENV{DEVKITPRO}" STREQUAL "" AND NOT "$ENV{DEVKITPRO}" STREQUAL "/opt/devkitpro")
  file(TO_CMAKE_PATH "$ENV{DEVKITPRO}" DEVKITPRO)
elseif(CMAKE_HOST_WIN32 AND EXISTS "C:/devkitPro/cmake/Switch.cmake")
  set(DEVKITPRO "C:/devkitPro")
elseif(EXISTS "/opt/devkitpro/cmake/Switch.cmake")
  set(DEVKITPRO "/opt/devkitpro")
else()
  message(FATAL_ERROR "Could not locate devkitPro installation. Set DEVKITPRO.")
endif()

if(NOT CMAKE_HOST_WIN32)
  include("${DEVKITPRO}/cmake/Switch.cmake")
  return()
endif()

# Native Windows CMake wrapper mirroring devkitPro's Switch.cmake + devkitA64.cmake
# so cross-compilation works cleanly with Ninja/mingw64 CMake without MSYS2.
list(APPEND CMAKE_MODULE_PATH "${DEVKITPRO}/cmake")
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${DEVKITPRO}/cmake/dkp-rule-overrides.cmake")

if(NOT CMAKE_SYSTEM_NAME)
  set(CMAKE_SYSTEM_NAME NintendoSwitch)
endif()

include(dkp-toolchain-common)
set(DKP_BIN2S_ALIGNMENT 8)
__dkp_toolchain(devkitA64 aarch64 aarch64-none-elf)

set(NX_ROOT "${DEVKITPRO}/libnx")
set(DKP_INSTALL_PREFIX_INIT "${DEVKITPRO}/portlibs/switch")

__dkp_platform_prefix(
  "${DEVKITPRO}/portlibs/switch"
  "${NX_ROOT}"
)

find_program(PKG_CONFIG_EXECUTABLE NAMES aarch64-none-elf-pkg-config HINTS "${DEVKITPRO}/portlibs/switch/bin" NO_CMAKE_FIND_ROOT_PATH)
find_program(NX_ELF2NRO_EXE NAMES elf2nro HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
find_program(NX_ELF2KIP_EXE NAMES elf2kip HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
find_program(NX_ELF2NSO_EXE NAMES elf2nso HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
find_program(NX_BUILD_PFS0_EXE NAMES build_pfs0 HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
find_program(NX_NACPTOOL_EXE NAMES nacptool HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
find_program(NX_NPDMTOOL_EXE NAMES npdmtool HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
find_program(NX_UAM_EXE NAMES uam HINTS "${DEVKITPRO}/tools/bin" NO_CMAKE_FIND_ROOT_PATH)
find_file(NX_DEFAULT_ICON NAMES default_icon.jpg HINTS "${NX_ROOT}" NO_CMAKE_FIND_ROOT_PATH)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
