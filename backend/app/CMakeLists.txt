add_executable(gymj_server web_main.cpp)
target_compile_features(gymj_server PRIVATE cxx_std_20)
target_link_libraries(gymj_server PRIVATE
    gymj::core_room
    gymj::server_gateway
    nlohmann_json::nlohmann_json
)
if(MSVC)
    target_compile_options(gymj_server PRIVATE /utf-8)
endif()
