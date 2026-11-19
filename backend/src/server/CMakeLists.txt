add_subdirectory(gateway)

add_library(gymj_server_runtime STATIC
    session/session_manager.cpp
    lobby/room_manager.cpp
    gateway/router.cpp
)
add_library(gymj::server_runtime ALIAS gymj_server_runtime)
target_link_libraries(gymj_server_runtime PUBLIC gymj::server_gateway gymj::core_room)
target_compile_features(gymj_server_runtime PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(gymj_server_runtime PRIVATE bcrypt)
endif()
if(MSVC)
    target_compile_options(gymj_server_runtime PRIVATE /utf-8)
endif()
