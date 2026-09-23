add_library(gymj_server_gateway STATIC
    ws_session.cpp
    listener.cpp
)
add_library(gymj::server_gateway ALIAS gymj_server_gateway)

target_include_directories(gymj_server_gateway
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gymj_server_gateway
    PUBLIC gymj::boost
)

target_compile_features(gymj_server_gateway PUBLIC cxx_std_20)
