# SPDX-License-Identifier: GPL-3.0-or-later
# Address/UB/Thread sanitizer wiring. Enabled via REVENANT_ENABLE_SANITIZERS.
# ASan and UBSan run together in the debug preset; TSan is separate (Linux CI job).

function(revenant_set_sanitizers target)
    if(NOT REVENANT_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        # MSVC supports AddressSanitizer; UBSan/TSan are not available.
        target_compile_options(${target} INTERFACE /fsanitize=address)
        return()
    endif()

    set(flags -fsanitize=address,undefined -fno-sanitize-recover=all
              -fno-omit-frame-pointer)
    target_compile_options(${target} INTERFACE ${flags})
    target_link_options(${target} INTERFACE -fsanitize=address,undefined)
endfunction()
