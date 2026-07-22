add_library(gymj_common INTERFACE)

add_library(gymj::common ALIAS gymj_common)

target_include_directories(gymj_common
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(gymj_common
    INTERFACE
        cxx_std_20
)

