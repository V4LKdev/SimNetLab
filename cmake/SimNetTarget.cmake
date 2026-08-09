function(simnet_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)

        if (SIMNET_ENABLE_STRICT_WARNINGS)
            target_compile_options(${target} PRIVATE
                -Wconversion
                -Wsign-conversion
                -Wshadow
                -Wnon-virtual-dtor
                -Wold-style-cast
                -Wcast-align
                -Woverloaded-virtual
                -Wnull-dereference
                -Wdouble-promotion
                -Wformat=2
                -Wimplicit-fallthrough
                -Wmisleading-indentation
            )

            if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                target_compile_options(${target} PRIVATE
                    -Wduplicated-cond
                    -Wduplicated-branches
                    -Wlogical-op
                    -Wuseless-cast
                )
            elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                target_compile_options(${target} PRIVATE
                    -Wextra-semi
                    -Wnewline-eof
                )
            endif ()
        endif ()

        if (SIMNET_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif ()

        if (SIMNET_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address)
        endif ()

        if (SIMNET_ENABLE_UBSAN)
            target_compile_options(${target} PRIVATE -fsanitize=undefined)
            target_link_options(${target} PRIVATE -fsanitize=undefined)
        endif ()
    elseif (MSVC)
        target_compile_options(${target} PRIVATE /W4)

        if (SIMNET_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif ()
    endif ()
endfunction()
