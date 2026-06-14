add_executable(tests
        test_rules.cpp
        test_integration.cpp
)

find_package(Catch2 CONFIG REQUIRED)

target_link_libraries(tests
        PRIVATE
        core
        Catch2::Catch2WithMain
)

set_target_properties(tests PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
)

# Tell CTest about it
add_test(NAME RulesUnits COMMAND tests)
