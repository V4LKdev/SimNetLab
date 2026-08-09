find_package(Python3 3.11 COMPONENTS Interpreter REQUIRED)

add_custom_target(simnet_format_check
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/simnet_format_check.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    USES_TERMINAL
)

add_custom_target(simnet_text_check
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/simnet_text_policy.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    USES_TERMINAL
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_custom_target(simnet_clang_tidy
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/simnet_clang_tidy.py
            --build-dir ${PROJECT_BINARY_DIR}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        USES_TERMINAL
    )
endif ()
