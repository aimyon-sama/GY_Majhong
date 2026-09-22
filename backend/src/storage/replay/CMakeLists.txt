add_library(gymj_storage_replay STATIC replay.cpp)
add_library(gymj::storage_replay ALIAS gymj_storage_replay)
target_link_libraries(gymj_storage_replay
    PUBLIC gymj::common
    PRIVATE nlohmann_json::nlohmann_json
)

target_compile_features(gymj_storage_replay PUBLIC cxx_std_20)
