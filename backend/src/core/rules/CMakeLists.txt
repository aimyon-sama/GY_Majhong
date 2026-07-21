add_library(gymj_core_rules STATIC
    rule_engine.cpp
)

add_library(gymj::core_rules ALIAS gymj_core_rules)

target_include_directories(gymj_core_rules
    PUBLIC
        ${PROJECT_SOURCE_DIR}/backend/include
)

target_compile_features(gymj_core_rules
    PUBLIC
        cxx_std_20
)

set_target_properties(gymj_core_rules PROPERTIES
    OUTPUT_NAME "gymj_core_rules"
)

