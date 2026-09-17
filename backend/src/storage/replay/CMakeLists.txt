find_package(nlohmann_json 3.12.0 QUIET GLOBAL)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(gymj_storage_replay STATIC heplay.cpp)
add_library(gymj::storage_replay ALIAS gymj_storage_replay)
target_link_libraries(gymj_storage_replay PUBLIC gymj::common PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(gymj_storage_replay PUBLIC cxx_std_20)
