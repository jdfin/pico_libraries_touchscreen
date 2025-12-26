add_library(touchscreen INTERFACE)

target_sources(touchscreen INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/touchscreen.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ft6336u.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/gt911.cpp
)

target_include_directories(touchscreen INTERFACE 
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(touchscreen INTERFACE
    pico_stdlib
    hardware_i2c
    misc
)

add_subdirectory(test)
