
#include <cstdio>
//
#include "hardware/i2c.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
//
#include "sys_led.h"
#include "touchscreen.h"
#include "xassert.h"
//
#include "ft6336u.h"


// Pico:
//
// Signal Pin
//
//         1  GPIO0
//         2  GPIO1
//         3  GND
//         4  GPIO2
//         5  GPIO3
// TP_SDA  6  GPIO4/I2C0_SDA
// TP_SCL  7  GPIO5/I2C0_SCL
//         8  GND
// TP_RST  9  GPIO6
// TP_INT 10  GPIO7
//        11  GPIO8
//        12  GPIO9
//        13  GND
//        14  GPIO10
//        15  GPIO11
//        16  GPIO12
//        17  GPIO13
//        18  GND
//        19  GPIO14
//        20  GPIO15

static const int tp_sda_pin = 4;
static const int tp_scl_pin = 5;
static const int tp_rst_pin = 6;
static const int tp_int_pin = 7;

static const int i2c_freq = 100'000;

static void test_1(Ft6336u &ts);


int main()
{
    stdio_init_all();
    SysLed::init();

    SysLed::pattern(50, 950);

#if 1
    while (!stdio_usb_connected()) {
        SysLed::loop();
        tight_loop_contents();
    }
    sleep_ms(10); // small delay needed or we lose the first prints
#endif

    SysLed::off();

    Ft6336u ft6336u(i2c0, tp_sda_pin, tp_scl_pin, tp_rst_pin, tp_int_pin,
                    i2c_freq);

    printf("Ft6336u: i2c running at %u Hz\n", ft6336u.i2c_freq());

    if (!ft6336u.init(2)) {
        printf("Ft6336u: ERROR initializing\n");
        xassert(false);
    }
    printf("Ft6336u: ready\n");

    sleep_ms(100);

    ft6336u.dump();

    sleep_ms(1000);

    test_1(ft6336u);

    sleep_ms(100); // let prints finish

    return 0;
}


[[maybe_unused]]
static void test_1([[maybe_unused]] Ft6336u &ts)
{
    while (true) {
        int e1, x1, y1, e2, x2, y2;
        int cnt = ts.get_touch(e1, x1, y1, e2, x2, y2, 2);
        printf("cnt=%d", cnt);
        if (cnt >= 1) {
            printf(" %d:(%d,%d)", e1, x1, y1);
            if (cnt >= 2) {
                printf(" %d:(%d,%d)", e2, x2, y2);
            }
        }
        printf("\n");
        sleep_ms(1000);
    }
}
