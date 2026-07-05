cmake_minimum_required(VERSION 3.20)
project(sixtyeightk_starfighters C)

# SGDK_DIR must point at an unzipped SGDK release. Set it in a local,
# gitignored CMakeUserPresets.json (copy CMakeUserPresets.json.example) --
# see README.md. It is not read from an OS environment variable.
if(NOT DEFINED SGDK_DIR OR SGDK_DIR STREQUAL "")
    message(FATAL_ERROR
        "SGDK_DIR is not set. Copy CMakeUserPresets.json.example to "
        "CMakeUserPresets.json and set SGDK_DIR to your SGDK install path.")
endif()

# --- Indexing only: gives CLion the include paths so SGDK headers resolve.
# This target is never actually linked -- there is no host-side linker that
# can produce m68k output, so it stays out of the default build set.
include_directories(
    ${SGDK_DIR}/inc
    ${SGDK_DIR}/res
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/src
)

file(GLOB_RECURSE PROJECT_SOURCES CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE PROJECT_HEADERS CONFIGURE_DEPENDS inc/*.h src/*.h)

add_library(indexing_only OBJECT EXCLUDE_FROM_ALL ${PROJECT_SOURCES} ${PROJECT_HEADERS})
set_target_properties(indexing_only PROPERTIES EXCLUDE_FROM_ALL TRUE)

# --- Real build: shells out to SGDK's own make.exe + makefile.gen. SGDK_DIR
# is forwarded as both SGDK and GDK env vars since the expected name has
# varied slightly across SGDK releases.
set(SGDK_MAKE "${SGDK_DIR}/bin/make.exe")
set(SGDK_MAKEFILE "${SGDK_DIR}/makefile.gen")

add_custom_target(rom
    COMMAND ${CMAKE_COMMAND} -E env "GDK=${SGDK_DIR}" "SGDK=${SGDK_DIR}"
            "${SGDK_MAKE}" -f "${SGDK_MAKEFILE}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building ROM via SGDK makefile.gen (release)"
    VERBATIM
)

add_custom_target(rom_debug
    COMMAND ${CMAKE_COMMAND} -E env "GDK=${SGDK_DIR}" "SGDK=${SGDK_DIR}"
            "${SGDK_MAKE}" -f "${SGDK_MAKEFILE}" debug
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building ROM via SGDK makefile.gen (debug)"
    VERBATIM
)

add_custom_target(rom_clean
    COMMAND ${CMAKE_COMMAND} -E env "GDK=${SGDK_DIR}" "SGDK=${SGDK_DIR}"
            "${SGDK_MAKE}" -f "${SGDK_MAKEFILE}" clean
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Cleaning SGDK build output"
    VERBATIM
)
