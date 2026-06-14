add_executable(tests
        test_rules.cpp
        test_integration.cpp
)

find_package(Catch2 CONFIG REQUIRED)
target_link_libraries(tests PRIVATE core Catch2::Catch2WithMain)

add_test(NAME RulesUnits COMMAND tests)
