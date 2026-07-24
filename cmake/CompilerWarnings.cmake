# SPDX-License-Identifier: GPL-3.0-or-later
# Project-wide warning contract. Attached to revenant_project_options so every
# target inherits an aggressive, consistent warning set across MSVC and GCC/Clang.

function(revenant_set_project_warnings target)
    set(msvc_warnings
        /W4
        /permissive-      # standards conformance
        /w14242 /w14254 /w14263 /w14265 /w14287
        /w14296 /w14311 /w14545 /w14546 /w14547
        /w14549 /w14555 /w14619 /w14640 /w14826
        /w14905 /w14906 /w14928
    )

    set(clang_gcc_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )

    set(gcc_only_warnings
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
    )

    if(MSVC)
        set(warnings ${msvc_warnings})
        if(REVENANT_WARNINGS_AS_ERRORS)
            list(APPEND warnings /WX)
        endif()
    else()
        set(warnings ${clang_gcc_warnings})
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(APPEND warnings ${gcc_only_warnings})
        endif()
        if(REVENANT_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    endif()

    target_compile_options(${target} INTERFACE ${warnings})
endfunction()
