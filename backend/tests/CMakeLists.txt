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
