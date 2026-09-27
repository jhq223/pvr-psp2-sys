cmake_minimum_required(VERSION 3.22)
# SNC/PRX have their own linking rules; CMake owns scheduling and dependencies,
# and invokes only the official SDK's native compiler, linker and stub tool.
project(pvr_psp2_driver LANGUAGES NONE)
find_program(PVR_RUSTC NAMES rustc REQUIRED)
set(CHECK_EXE "${CMAKE_BINARY_DIR}/pvr-native-check")
if(CMAKE_HOST_WIN32)
    string(APPEND CHECK_EXE ".exe")
endif()
add_custom_command(OUTPUT "${CHECK_EXE}"
    COMMAND "${PVR_RUSTC}" --edition=2024 --crate-name pvr_native_check
            "${CMAKE_CURRENT_SOURCE_DIR}/check.rs" -o "${CHECK_EXE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/check.rs"
            "${CMAKE_CURRENT_SOURCE_DIR}/check/abi.rs"
            "${CMAKE_CURRENT_SOURCE_DIR}/check/depfile.rs"
            "${CMAKE_CURRENT_SOURCE_DIR}/check/optimizations.rs"
            "${CMAKE_CURRENT_SOURCE_DIR}/check/allocators.rs" "${PVR_RUSTC}"
    COMMENT "Build host PVR checker with Rust (standard library only)" VERBATIM)

set(PSVSDK "$ENV{SCE_PSP2_SDK_DIR}" CACHE PATH "Official PSVSDK root (host_tools and target)")
if(NOT PSVSDK)
    message(FATAL_ERROR "Set -DPSVSDK to your official PSVSDK root")
endif()
set(PVR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../vendor/PVR_PSP2")
get_filename_component(PVR_ROOT "${PVR_ROOT}" ABSOLUTE)
set(SNC "${PSVSDK}/host_tools/build/bin/psp2snc.exe")
set(LINKER "${PSVSDK}/host_tools/build/bin/psp2ld.exe")
set(LIBGEN "${PSVSDK}/host_tools/build/bin/armlibgen.exe")
foreach(tool SNC LINKER LIBGEN)
    if(NOT EXISTS "${${tool}}")
        message(FATAL_ERROR "Missing official SDK tool: ${${tool}}")
    endif()
endforeach()
set(MODULE_DIR "${CMAKE_BINARY_DIR}/module")
set(IMPORT_DIR "${CMAKE_BINARY_DIR}/imports")
file(MAKE_DIRECTORY "${MODULE_DIR}" "${IMPORT_DIR}")

set(PVR_IMPORT_LIBRARIES
    "${IMPORT_DIR}/libSceGpuEs4User_stub.a"
    "${IMPORT_DIR}/libSceGpuEs4UserForVsh_stub.a"
    "${IMPORT_DIR}/libSceGpuEs4ForUser_stub.a"
    "${IMPORT_DIR}/libSceDmacmgr_stub.a"
    "${IMPORT_DIR}/libSceSysmoduleInternal_stub.a"
    "${IMPORT_DIR}/libSceModulemgrInternal_stub.a"
    "${IMPORT_DIR}/libSceDisplayUserInternal_stub.a")
add_custom_command(OUTPUT "${IMPORT_DIR}/ready" ${PVR_IMPORT_LIBRARIES}
    COMMAND "${LIBGEN}" --dump "${PVR_ROOT}/lib/SceGpuEs4User.emd" --stub-archive
    COMMAND "${LIBGEN}" --dump "${PVR_ROOT}/lib/SceGpuEs4UserForVsh.emd" --stub-archive
    COMMAND "${LIBGEN}" --dump "${CMAKE_CURRENT_LIST_DIR}/imports/extensions.emd" --stub-archive
    COMMAND "${CMAKE_COMMAND}" -E touch "${IMPORT_DIR}/ready"
    DEPENDS "${PVR_ROOT}/lib/SceGpuEs4User.emd" "${PVR_ROOT}/lib/SceGpuEs4UserForVsh.emd"
            "${CMAKE_CURRENT_LIST_DIR}/imports/extensions.emd" "${LIBGEN}"
    WORKING_DIRECTORY "${IMPORT_DIR}" VERBATIM)

include(cmake/sources.cmake)

set(PVR_OPT_MASK "1023" CACHE STRING "Driver optimization groups, bit 0 through bit 9")

option(PVR_PROBE_DIAGNOSTICS "Persist render allocation failures for the standalone probe" OFF)

function(pvr_module name)
    set(args -DPVR_OPT_MASK=${PVR_OPT_MASK} -c -O3 -Xthumb=1 -include "${CMAKE_CURRENT_SOURCE_DIR}/include/sdk_extensions.h"
             -I "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(PVR_PROBE_DIAGNOSTICS)
        list(APPEND args -DPVR_PROBE_DIAGNOSTICS=1)
    endif()
    foreach(definition IN LISTS ${name}_DEFINES)
        list(APPEND args "-D${definition}")
    endforeach()
    foreach(include IN LISTS ${name}_INCLUDES)
        list(APPEND args -I "${PVR_ROOT}/${include}")
    endforeach()
    set(objects)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/${name}")
    foreach(source IN LISTS ${name}_SOURCES)
        string(SHA256 id "${source}")
        string(SUBSTRING "${id}" 0 12 id)
        set(object "${CMAKE_BINARY_DIR}/${name}/${id}.o")
        add_custom_command(OUTPUT "${object}"
            COMMAND "${SNC}" ${args} -MD -MF "${object}.raw.d" "${PVR_ROOT}/${source}" -o "${object}"
            COMMAND "${CHECK_EXE}" depfile "${object}.raw.d" "${object}.d" "${object}"
            DEPENDS "${PVR_ROOT}/${source}" "${SNC}" "${CHECK_EXE}"
            DEPFILE "${object}.d"
            COMMENT "PSVSDK ${name}: ${source}" VERBATIM)
        list(APPEND objects "${object}")
    endforeach()
    set(imports "${IMPORT_DIR}/libSceGpuEs4User_stub.a"
                "${IMPORT_DIR}/libSceGpuEs4UserForVsh_stub.a"
                "${IMPORT_DIR}/libSceGpuEs4ForUser_stub.a"
                "${IMPORT_DIR}/libSceDmacmgr_stub.a"
                "${IMPORT_DIR}/libSceSysmoduleInternal_stub.a"
                "${IMPORT_DIR}/libSceModulemgrInternal_stub.a"
                "${IMPORT_DIR}/libSceDisplayUserInternal_stub.a")
    set(dependencies)
    foreach(dependency IN LISTS ARGN)
        list(APPEND dependencies "${MODULE_DIR}/${dependency}_stub.a")
    endforeach()
    set(system_libraries -lSceGxm_stub -lSceDisplay_stub -lSceAppMgr_stub -lScePower_stub)
    if(name STREQUAL "libGLESv2")
        list(APPEND system_libraries -lSceSysmodule_stub -lSceUlt_stub_weak -lSceSha256)
    endif()
    set(module "${MODULE_DIR}/${name}.suprx")
    set(stub "${MODULE_DIR}/${name}_stub.a")
    add_custom_command(OUTPUT "${module}" "${stub}"
        COMMAND "${LINKER}" --oformat=prx "--prx-stub-output-dir=${MODULE_DIR}"
                -o "${module}" ${objects} ${dependencies} ${imports} ${system_libraries}
        COMMAND "${CHECK_EXE}" abi "${CMAKE_CURRENT_SOURCE_DIR}/abi/${name}.nids" "${stub}"
        DEPENDS ${objects} ${dependencies} ${imports} "${IMPORT_DIR}/ready" "${LINKER}"
                "${CHECK_EXE}" "${CMAKE_CURRENT_SOURCE_DIR}/abi/${name}.nids"
        COMMENT "PSVSDK link and ABI check: ${name}" VERBATIM)
    add_custom_target(${name} ALL DEPENDS "${module}" "${stub}")
endfunction()

pvr_module(libgpu_es4_ext)
pvr_module(libpvrPSP2_WSEGL libgpu_es4_ext)
pvr_module(libIMGEGL libgpu_es4_ext libpvrPSP2_WSEGL)
pvr_module(libGLESv2 libgpu_es4_ext libpvrPSP2_WSEGL libIMGEGL)
