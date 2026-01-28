
#include <cassert>
#include <cstdio>
// pico
#include "hardware/i2c.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
// misc
#include "sys_led.h"
// touchscreen
#include "ft6336u.h"
#include "touchscreen.h"
//
#include "ts_gpio_cfg.h"

static const int ts_i2c_freq = 100'000;

static void test_1(Touchscreen &ts);


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

    Ft6336u ft6336u(i2c0, ts_i2c_sda_gpio, ts_i2c_scl_gpio, ts_rst_gpio,
                    ts_int_gpio, ts_i2c_freq);

    printf("Ft6336u: i2c running at %u Hz\n", ft6336u.i2c_freq());

    if (!ft6336u.init(2)) {
        printf("Ft6336u: ERROR initializing\n");
        assert(false);
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
static void test_1(Touchscreen &ts)
{
    while (true) {
        int x[2], y[2];
        int cnt = ts.get_touches(x, y, 2);
        printf("cnt=%d", cnt);
        if (cnt >= 1) {
            printf(" (%d,%d)", x[0], y[0]);
            if (cnt >= 2) {
                printf(" (%d,%d)", x[1], y[1]);
            }
        }
        printf("\n");
        sleep_ms(1000);
    }
}
