add_executable(tests
        test_rules.cpp
        test_integration.cpp
        net/test_net_buffer.cpp
        net/test_connection_handler.cpp
        net/test_net_manager.cpp
        net/test_net_message.cpp
        net/test_peer_state.cpp
        net/test_pipeline.cpp
        net/test_snapshot.cpp
)

find_package(Catch2 CONFIG REQUIRED)
target_link_libraries(tests PRIVATE core Catch2::Catch2WithMain)

add_test(NAME RulesUnits COMMAND tests)
