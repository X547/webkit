# Enable ccache by default, if installed. To disable it you can:
# if using script build-webkit: pass --no-use-ccache
# if using cmake: set environment variable WK_USE_CCACHE=NO
if (NOT "$ENV{WK_USE_CCACHE}" STREQUAL "NO")
    find_program(CCACHE_FOUND ccache)
    if (CCACHE_FOUND)
        if (PORT STREQUAL "Mac")
            set(CCACHE ${CMAKE_SOURCE_DIR}/Tools/ccache/ccache-wrapper)
            set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE_FOUND})
        else ()
            if (NOT DEFINED ENV{CCACHE_SLOPPINESS})
                set(ENV{CCACHE_SLOPPINESS} time_macros)
            endif ()
            # FIXME: readlink -f isn't supported on macOS. https://bugs.webkit.org/show_bug.cgi?id=208379
            execute_process(COMMAND readlink -f ${CMAKE_CXX_COMPILER} RESULT_VARIABLE READLINK_RETCODE OUTPUT_VARIABLE REAL_CXX_PATH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            execute_process(COMMAND which ${CCACHE_FOUND} RESULT_VARIABLE WHICH_RETCODE OUTPUT_VARIABLE REAL_CCACHE_PATH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if (${WHICH_RETCODE} EQUAL 0 AND ${READLINK_RETCODE} EQUAL 0 AND "${REAL_CXX_PATH}" STREQUAL "${REAL_CCACHE_PATH}")
                message(STATUS "Enabling ccache: Compiler path already pointing to ccache. Not setting ccache prefix.")
            else ()
                message(STATUS "Enabling ccache: Setting ccache prefix for compiler.")
                set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE_FOUND})
            endif ()
        endif ()
    else ()
        message(STATUS "Enabling ccache: Couldn't find ccache program. Not enabling it.")
    endif ()
endif ()

if ("$ENV{WEBKIT_USE_SCCACHE}" STREQUAL "YES")
    find_program(SCCACHE_FOUND sccache)
    if (SCCACHE_FOUND)
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${SCCACHE_FOUND})
    endif ()
endif ()
