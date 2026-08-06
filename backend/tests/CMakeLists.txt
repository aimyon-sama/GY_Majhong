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
