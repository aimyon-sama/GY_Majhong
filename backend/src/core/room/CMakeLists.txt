add_library(gymj_core_room STATIC
    round.cpp
    table.cpp
    wall.cpp
)

add_library(gymj::core_room ALIAS gymj_core_room)

target_include_directories(gymj_core_room
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gymj_core_room
    PUBLIC
        gymj::core_rules
        gymj::common
)

target_compile_features(gymj_core_room
    PUBLIC
        cxx_std_20
)

set_target_properties(gymj_core_room PROPERTIES
    OUTPUT_NAME "gymj_core_room"
)
