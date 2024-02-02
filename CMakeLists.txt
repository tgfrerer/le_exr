set (TARGET le_exr)

# list modules this module depends on

depends_on_island_module(le_renderer)
depends_on_island_module(le_log)

# Come on, OpenEXR!!! ---------------

set(OPENEXR_INSTALL OFF CACHE BOOL "Install OpenEXR libraries" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "Install OpenEXR tools" FORCE)
set(OPENEXR_INSTALL_EXAMPLES OFF CACHE BOOL "build exr examples" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "build exr tools" FORCE)

# OpenEXR depends on Imath -----------

add_subdirectory( 3rdparty/le_exr/Imath/)
add_subdirectory( 3rdparty/le_exr/openexr/)

# Source files for this Island module ----

set (SOURCES "le_exr.cpp")
set (SOURCES ${SOURCES} "le_exr.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    add_dynamic_linker_flags()
  
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib( ${TARGET} )

endif()

# ----------------------------------------------------------------------
# Add include directories -
# Note that these are set as PRIVATE - they apply to the current module.
# ----------------------------------------------------------------------

target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/le_exr/Imath/src/Imath")
target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/le_exr/Imath/src/Imath")

target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/le_exr/openexr/src/lib/OpenEXR")
target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/le_exr/openexr/src/lib/OpenEXRUtil")
target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/le_exr/openexr/src/lib/OpenEXRCore")
target_include_directories(${TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/le_exr/openexr/src/lib/Iex")

# ----------------------------------------------------------------------
# Configuration files are temporary, and held in the local build directory
# We must add these too, as PRIVATE includes, so that the current library
# has access to them.

target_include_directories(${TARGET} PRIVATE
    "${CMAKE_BINARY_DIR}/le_exr/3rdparty/le_exr/openexr/cmake")

target_include_directories(${TARGET} PRIVATE
    "${CMAKE_BINARY_DIR}/le_exr/3rdparty/le_exr/Imath/config")

# ----------------------------------------------------------------------

if (WIN32)
    set (LINKER_FLAGS ${LINKER_FLAGS} OpenEXR)
else()
    set (LINKER_FLAGS -Wl,--whole-archive OpenEXR -Wl,--no-whole-archive)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
