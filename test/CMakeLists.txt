
add_compile_options(-Wall -Wextra -Werror)

# gt911_test

add_executable(gt911_test
    gt911_test.cpp
)

pico_enable_stdio_uart(gt911_test 0)
pico_enable_stdio_usb(gt911_test 1)

target_link_libraries(gt911_test PRIVATE
    pico_stdlib
    pico_stdio_usb
    touchscreen
    misc
)

pico_add_extra_outputs(gt911_test)

# ft6336u_test

add_executable(ft6336u_test
    ft6336u_test.cpp
)

pico_enable_stdio_uart(ft6336u_test 0)
pico_enable_stdio_usb(ft6336u_test 1)

target_link_libraries(ft6336u_test PRIVATE
    pico_stdlib
    pico_stdio_usb
    touchscreen
    misc
)

pico_add_extra_outputs(ft6336u_test)
