add_executable(gymj_game_engine_tests
    game_engine_tests.cpp
)

target_link_libraries(gymj_game_engine_tests
    PRIVATE
        gymj::core_rules
)

target_compile_features(gymj_game_engine_tests
    PRIVATE
        cxx_std_20
)

add_test(
    NAME gymj_game_engine_tests
    COMMAND gymj_game_engine_tests
)

add_executable(gymj_rule_engine_tests
    rule_engine_tests.cpp
)

target_link_libraries(gymj_rule_engine_tests
    PRIVATE
        gymj::core_rules
)

target_compile_features(gymj_rule_engine_tests
    PRIVATE
        cxx_std_20
)

add_test(
    NAME gymj_rule_engine_tests
    COMMAND gymj_rule_engine_tests
)

add_executable(gymj_point_engine_tests
    point_engine_tests.cpp
)

target_link_libraries(gymj_point_engine_tests
    PRIVATE
        gymj::core_point
)

target_compile_features(gymj_point_engine_tests
    PRIVATE
        cxx_std_20
)

add_test(
    NAME gymj_point_engine_tests
    COMMAND gymj_point_engine_tests
)

add_executable(gymj_wall_tests
    wall_tests.cpp
)

target_link_libraries(gymj_wall_tests
    PRIVATE
        gymj::core_room
)

target_compile_features(gymj_wall_tests
    PRIVATE
        cxx_std_20
)

add_test(
    NAME gymj_wall_tests
    COMMAND gymj_wall_tests
)

add_executable(gymj_round_tests
    round_tests.cpp
)

target_link_libraries(gymj_round_tests
    PRIVATE
        gymj::core_room
)

target_compile_features(gymj_round_tests
    PRIVATE
        cxx_std_20
)

add_test(
    NAME gymj_round_tests
    COMMAND gymj_round_tests
)

add_executable(gymj_table_tests table_tests.cpp)
target_link_libraries(gymj_table_tests PRIVATE gymj::core_room nlohmann_json::nlohmann_json)
target_compile_features(gymj_table_tests PRIVATE cxx_std_20)
add_test(NAME gymj_table_tests COMMAND gymj_table_tests)

add_executable(gymj_protocol_tests protocol_tests.cpp)
target_link_libraries(gymj_protocol_tests PRIVATE gymj::server_gateway)
target_compile_features(gymj_protocol_tests PRIVATE cxx_std_20)
add_test(NAME gymj_protocol_tests COMMAND gymj_protocol_tests)

add_executable(gymj_session_manager_tests session_manager_tests.cpp)
target_link_libraries(gymj_session_manager_tests PRIVATE gymj::server_runtime)
add_test(NAME gymj_session_manager_tests COMMAND gymj_session_manager_tests)

# 网络测试使用 Node 内置 WebSocket，不依赖 npm；版本不足时保留 C++ 测试。
find_program(GYMJ_NODE_EXECUTABLE NAMES node)
if(GYMJ_NODE_EXECUTABLE)
    execute_process(COMMAND "${GYMJ_NODE_EXECUTABLE}" -p
        "Number(process.versions.node.split('.')[0]) >= 22 && typeof WebSocket !== 'undefined'"
        OUTPUT_VARIABLE GYMJ_NODE_SUPPORTED OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(GYMJ_NODE_SUPPORTED STREQUAL "true")
        add_test(NAME gymj_web_transport_tests COMMAND "${GYMJ_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/../scripts/test-web.mjs" "$<TARGET_FILE:gymj_server>")
        add_test(NAME gymj_web_game_tests COMMAND "${GYMJ_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/../scripts/test-game.mjs" "$<TARGET_FILE:gymj_server>")
        add_test(NAME gymj_web_timeout_tests COMMAND "${GYMJ_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/../scripts/test-timeout.mjs" "$<TARGET_FILE:gymj_server>")
        add_test(NAME gymj_web_leave_tests COMMAND "${GYMJ_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/../scripts/test-leave.mjs" "$<TARGET_FILE:gymj_server>")
        set_tests_properties(gymj_web_transport_tests gymj_web_game_tests gymj_web_timeout_tests gymj_web_leave_tests PROPERTIES TIMEOUT 60)
    else()
        message(STATUS "Node.js 22+ required: skipping WebSocket integration tests")
    endif()
else()
    message(STATUS "Node.js not found: skipping WebSocket integration tests")
endif()
