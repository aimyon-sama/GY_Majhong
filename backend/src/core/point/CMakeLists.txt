add_library(gymj_core_point STATIC
    point_engine.cpp
)

add_library(gymj::core_point ALIAS gymj_core_point)

target_include_directories(gymj_core_point
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gymj_core_point
    PUBLIC
        gymj::common
)

target_compile_features(gymj_core_point
    PUBLIC
        cxx_std_20
)

set_target_properties(gymj_core_point PROPERTIES
    OUTPUT_NAME "gymj_core_point"
)
