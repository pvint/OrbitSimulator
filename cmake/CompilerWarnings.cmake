# orbit_set_warnings(<target>)
#
# Turns on a strict set of compiler warnings for one of our own targets.
# Third-party and Qt headers are unaffected: CMake marks the include
# directories of imported targets as system headers.
#
# With ORBIT_WARNINGS_AS_ERRORS=ON (the "dev" preset), any warning fails the
# build. It is much easier to keep a codebase warning-free from day one than
# to clean it up later.

function(orbit_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
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
endfunction()
