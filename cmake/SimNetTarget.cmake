function(simnet_configure_target TARGET)
    target_compile_features(${TARGET} PUBLIC cxx_std_23)
    set_target_properties(${TARGET} PROPERTIES
        CXX_EXTENSIONS OFF
    )

    target_compile_options(${TARGET}
        PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
            $<$<AND:$<BOOL:${SIMNET_WARNINGS_AS_ERRORS}>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-Werror>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<AND:$<BOOL:${SIMNET_WARNINGS_AS_ERRORS}>,$<CXX_COMPILER_ID:MSVC>>:/WX>
    )

    target_enable_sanitizers(${TARGET})
endfunction()
