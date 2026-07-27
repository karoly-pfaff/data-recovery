# SPDX-License-Identifier: GPL-3.0-or-later
# Developer convenience targets that mirror the CI quality gates locally:
#   format        - reformat all sources in place (clang-format)
#   format-check  - verify formatting, fail if anything would change
#   tidy          - run clang-tidy over the compile database
#   guard-limits  - enforce the file-length limit (warn 200 / hard fail 250)

function(revenant_add_dev_targets)
    file(GLOB_RECURSE revenant_sources CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_SOURCE_DIR}/tests/*.hpp"
        "${CMAKE_SOURCE_DIR}/tools/*.cpp"
        "${CMAKE_SOURCE_DIR}/tools/*.hpp"
    )

    find_program(REVENANT_CLANG_FORMAT NAMES clang-format)
    find_program(REVENANT_CLANG_TIDY NAMES clang-tidy)
    find_program(REVENANT_PYTHON NAMES python3 python)

    if(REVENANT_CLANG_FORMAT)
        add_custom_target(format
            COMMAND ${REVENANT_CLANG_FORMAT} -i --style=file ${revenant_sources}
            COMMENT "clang-format: reformatting sources in place"
            VERBATIM)
        add_custom_target(format-check
            COMMAND ${REVENANT_CLANG_FORMAT} --dry-run --Werror --style=file
                    ${revenant_sources}
            COMMENT "clang-format: verifying formatting"
            VERBATIM)
    endif()

    if(REVENANT_CLANG_TIDY)
        # clang-tidy needs a compilable TU: exclude the off-platform implementation
        # (it is tidied on its own platform; format/guard targets still cover it).
        set(revenant_tidy_sources ${revenant_sources})
        if(WIN32)
            list(FILTER revenant_tidy_sources EXCLUDE REGEX "Posix\\.cpp$")
        else()
            list(FILTER revenant_tidy_sources EXCLUDE REGEX "Windows\\.cpp$")
        endif()
        # One stamp-backed step per file: the build tool runs them in parallel
        # and skips files unchanged since their last clean run. Same check set,
        # same failure semantics — only the scheduling differs; CI still runs
        # the full target. Local caveat: a stamp depends on its own source and
        # the tidy configs, not on included headers — cross-TU header impact
        # is caught by CI's from-scratch run (and by deleting tidy-stamps/).
        set(revenant_tidy_stamps "")
        foreach(tidy_source IN LISTS revenant_tidy_sources)
            file(RELATIVE_PATH tidy_rel "${CMAKE_SOURCE_DIR}" "${tidy_source}")
            string(REGEX REPLACE "[/\\:]" "_" tidy_stamp_name "${tidy_rel}")
            set(tidy_stamp "${CMAKE_BINARY_DIR}/tidy-stamps/${tidy_stamp_name}.stamp")
            add_custom_command(
                OUTPUT "${tidy_stamp}"
                COMMAND ${REVENANT_CLANG_TIDY} -p ${CMAKE_BINARY_DIR} "${tidy_source}"
                COMMAND ${CMAKE_COMMAND} -E touch "${tidy_stamp}"
                DEPENDS "${tidy_source}"
                        "${CMAKE_SOURCE_DIR}/.clang-tidy"
                        "${CMAKE_SOURCE_DIR}/tests/.clang-tidy"
                COMMENT "clang-tidy: ${tidy_rel}"
                VERBATIM)
            list(APPEND revenant_tidy_stamps "${tidy_stamp}")
        endforeach()
        add_custom_target(tidy DEPENDS ${revenant_tidy_stamps})
    endif()

    if(REVENANT_PYTHON)
        add_custom_target(guard-limits
            COMMAND ${REVENANT_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_file_length.py
                    --warn 200 --max 250 ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_SOURCE_DIR}/tools
            COMMENT "guard-limits: enforcing the 250-line file ceiling"
            VERBATIM)
    endif()
endfunction()
