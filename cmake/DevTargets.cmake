# SPDX-License-Identifier: GPL-3.0-or-later
# Developer convenience targets that mirror the CI quality gates locally:
#   format        - reformat all sources in place (clang-format)
#   format-check  - verify formatting, fail if anything would change
#   tidy          - run clang-tidy over the compile database
#   guard-limits  - enforce the file-length limit (warn 200 / hard fail 250)
#   duplication   - enforce the DRY threshold (docs/testing/quality-gates.md)

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

    # story-0607: the file set is discovered at run time by the driver and
    # delivered to clang-format in bounded batches — expanding it here once
    # outgrew Windows' 32,767-character command-line limit.
    if(REVENANT_CLANG_FORMAT AND REVENANT_PYTHON)
        set(revenant_format_driver
            ${REVENANT_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_format.py
            --clang-format ${REVENANT_CLANG_FORMAT})
        set(revenant_format_roots
            ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/tools)
        add_custom_target(format
            COMMAND ${revenant_format_driver} --fix ${revenant_format_roots}
            COMMENT "clang-format: reformatting sources in place"
            VERBATIM)
        add_custom_target(format-check
            COMMAND ${revenant_format_driver} ${revenant_format_roots}
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
        #
        # A build tool's parallelism stops at the machine's cores, and the tree
        # has more files than a CI runner can chew through in a reasonable
        # wall-clock. So the file list can also be split across *builds*: CI
        # runs one job per shard and every file still lands in exactly one of
        # them. Round-robin by index rather than by directory, so the shards
        # stay balanced as files are added. The default is one shard — locally
        # `tidy` still means all of it.
        set(REVENANT_TIDY_SHARDS 1 CACHE STRING "How many builds to split the tidy target across")
        set(REVENANT_TIDY_SHARD 0 CACHE STRING "Which shard this build runs (0-based)")
        # Checked here rather than trusted, because the failure is silent and
        # the wrong way round: a shard index that matches nothing leaves `tidy`
        # with no work, and a gate that checks nothing *passes*. A typo in a CI
        # matrix must stop the configure, not quietly disarm the lint.
        if(NOT REVENANT_TIDY_SHARDS MATCHES "^[0-9]+$" OR REVENANT_TIDY_SHARDS LESS 1)
            message(FATAL_ERROR
                "REVENANT_TIDY_SHARDS must be a positive integer; got '${REVENANT_TIDY_SHARDS}'")
        endif()
        if(NOT REVENANT_TIDY_SHARD MATCHES "^[0-9]+$"
           OR NOT REVENANT_TIDY_SHARD LESS REVENANT_TIDY_SHARDS)
            message(FATAL_ERROR
                "REVENANT_TIDY_SHARD must be in [0, ${REVENANT_TIDY_SHARDS}); "
                "got '${REVENANT_TIDY_SHARD}'")
        endif()
        set(revenant_tidy_stamps "")
        set(revenant_tidy_index 0)
        foreach(tidy_source IN LISTS revenant_tidy_sources)
            math(EXPR revenant_tidy_owner "${revenant_tidy_index} % ${REVENANT_TIDY_SHARDS}")
            math(EXPR revenant_tidy_index "${revenant_tidy_index} + 1")
            if(NOT revenant_tidy_owner EQUAL REVENANT_TIDY_SHARD)
                continue()
            endif()
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
        # story-0602: the DRY detector, in the same Python as the rest. It needs
        # `lizard` importable by this interpreter; a missing one fails the target
        # rather than skipping it, because a gate that quietly does not run is
        # the failure this repository keeps finding.
        add_custom_target(duplication
            COMMAND ${REVENANT_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_duplication.py
                    --min-tokens 60 ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_SOURCE_DIR}/tools
            COMMENT "duplication: enforcing the 60-token DRY threshold"
            VERBATIM)
        # story-0609: clang rejects a byte MSVC and GCC compile in silence, so
        # a stray cp1252 character reaches you as a red CI run. Reading each
        # file once locally is cheaper. Covers `tests` too — the sources there
        # are compiled by the same three toolchains.
        add_custom_target(encoding
            COMMAND ${REVENANT_PYTHON} ${CMAKE_SOURCE_DIR}/tools/lint/check_encoding.py
                    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include
                    ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/tools
            COMMENT "encoding: enforcing plain UTF-8 in every source file"
            VERBATIM)
    endif()
endfunction()
