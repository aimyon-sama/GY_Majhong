set(GYMJ_VENDOR_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(GYMJ_VENDOR_BOOST "${GYMJ_VENDOR_ROOT}/boost_1_90_0")
set(GYMJ_VENDOR_JSON "${GYMJ_VENDOR_ROOT}/nlohmann_json_3_12_0/include")

foreach(required_header IN ITEMS
 "${GYMJ_VENDOR_BOOST}/boost/asio.hpp"
 "${GYMJ_VENDOR_BOOST}/boost/beast/websocket.hpp"
 "${GYMJ_VENDOR_JSON}/nlohmann/json.hpp"
)
    if(NOT EXISTS "${required_header}")
        message(FATAL_ERROR "Missing vendored dependency: ${required_header}.")
    endif()
endforeach()

find_package(Threads REQUIRED)
add_library(gymj_boost INTERFACE)
add_library(gymj::boost ALIAS gymj_boost)
target_include_directories(gymj_boost SYSTEM INTERFACE "${GYMJ_VENDOR_BOOST}")
target_link_libraries(gymj_boost INTERFACE Threads::Threads)
if(WIN32)
    target_compile_definitions(gymj_boost INTERFACE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
    )
    target_link_libraries(gymj_boost INTERFACE ws2_32 mswsock)
endif()
if(MSVC)
    target_compile_options(gymj_boost INTERFACE /bigobj)
endif()
add_library(gymj_nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS gymj_nlohmann_json)
target_include_directories(gymj_nlohmann_json SYSTEM INTERFACE
"${GYMJ_VENDOR_JSON}")
