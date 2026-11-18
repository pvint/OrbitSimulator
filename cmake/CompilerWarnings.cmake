# SPDX-License-Identifier: GPL-3.0-or-later
# orbit_set_warnings(<target>)
#
# Turns on a strict set of compiler warnings for one of our own targets.
# Third-party and Qt headers are unaffected: CMake marks the include
# directories of imported targets as system headers.
#
# With ORBIT_WARNINGS_AS_ERRORS=ON (the "dev" preset), any warning fails the
# build. It is much easier to keep a codebase warning-free from day one than
# to clean it up later.
#
# On Windows it also makes the target UTF-8 throughout, as it is elsewhere:
# MSVC reads the sources as UTF-8 (/utf-8), and executables run with UTF-8 as
# their code page (utf8.manifest), so non-ASCII arguments and file names
# arrive intact. CTest passes test names such as "90° is east" that way.

set(ORBIT_UTF8_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/utf8.manifest")

function(orbit_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /utf-8
            $<$<BOOL:${ORBIT_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow                # a variable hides another with the same name
            -Wconversion            # implicit conversions that may lose data
            -Wsign-conversion
            -Wdouble-promotion      # float silently promoted to double
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wimplicit-fallthrough
            $<$<BOOL:${ORBIT_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()

    get_target_property(type ${target} TYPE)
    if(WIN32 AND type STREQUAL "EXECUTABLE")
        target_sources(${target} PRIVATE "${ORBIT_UTF8_MANIFEST}")
    endif()
endfunction()
