
#include <cstdint>
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
#include "gt911.h"

// Pico
//        +----------| USB |----------+
//        | 1  GPIO0      VBUS_OUT 40 |
//        | 2  GPIO1   VSYS_IN_OUT 39 |
//        | 3  GND             GND 38 |
//        | 4  GPIO2        3V3_EN 37 |
//        | 5  GPIO3       3V3_OUT 36 |
// TP_SDA | 6  I2C0_SDA/4     AREF 35 |
// TP_SCL | 7  I2C0_SCL/5   GPIO28 34 |
//        | 8  GND             GND 33 |
// TP_RST | 9  GPIO6        GPIO27 32 |
// TP_INT | 10 GPIO7        GPIO26 31 |
//        | 11 GPIO8           RUN 30 |
//        | 12 GPIO9        GPIO22 29 | LED  (fb)
//        | 13 GND             GND 28 |
//        | 14 GPIO10       GPIO21 27 | RST  (fb)
//        | 15 GPIO11       GPIO20 26 | CD   (fb)
//        | 16 GPIO12   SPI0_TX/19 25 | MOSI (fb)
//        | 17 GPIO13  SPI0_SCK/18 24 | SCK  (fb)
//        | 18 GND             GND 23 |
//        | 19 GPIO14  SPI0_CSn/17 22 | CS   (fb)
//        | 20 GPIO15   SPI0_RX/16 21 | MISO (fb)
//        +---------------------------+

static const int tp_sda_pin = 4;
static const int tp_scl_pin = 5;
static const int tp_rst_pin = 6;
static const int tp_int_pin = 7;

static const int i2c_freq = 400'000;

static const uint8_t tp_adrs = 0x14; // 0x14 or 0x5d

static void test_1(Gt911 &ts);
static void test_2(Gt911 &ts);
static void rotations(Gt911 &ts);


int main()
{
    stdio_init_all();
    SysLed::init();

    SysLed::pattern(50, 950);

#if 0
    while (!stdio_usb_connected()) {
        SysLed::loop();
        tight_loop_contents();
    }
    sleep_ms(10); // small delay needed or we lose the first prints
#endif

    SysLed::off();

    // Touchscreen

    Gt911 gt911(i2c0, tp_adrs, tp_sda_pin, tp_scl_pin, tp_rst_pin, tp_int_pin,
                i2c_freq);

    printf("Gt911: i2c running at %u Hz\n", gt911.i2c_freq());

    constexpr int verbosity = 2;
    if (!gt911.init(verbosity)) {
        printf("Gt911: ERROR initializing\n");
        xassert(false);
    }
    printf("Gt911: ready\n");

    gt911.rotation(Gt911::Rotation::left);

    sleep_ms(100);

    //gt911.dump();
    //sleep_ms(1000);

    do {
        //test_1(gt911); sleep_ms(1000);
        test_2(gt911); sleep_ms(10);
        sleep_ms(10);
        //rotations(gt911);
    } while (true);

    return 0;
}


static void poll_touch(Gt911 &ts)
{
    int x1, y1;
    int cnt = ts.get_touch(x1, y1);
    printf("cnt=%d", cnt);
    if (cnt >= 1)
        printf(" (%d,%d)", x1, y1);
    printf("\n");
}


// read and print touch info once
[[maybe_unused]]
static void test_1(Gt911 &ts)
{
    poll_touch(ts);
}


// read status and report touches only if something changed
[[maybe_unused]]
static void test_2(Gt911 &ts)
{
    constexpr int t_max = 5;

    static int x[t_max] = {-1, -1, -1, -1, -1};
    static int y[t_max] = {-1, -1, -1, -1, -1};
    static int cnt = -1;

    int new_x[t_max];
    int new_y[t_max];
    int new_cnt = ts.get_touches(new_x, new_y, t_max);

    bool changed = false;
    if (new_cnt != cnt) {
        changed = true;
    } else {
        for (int t = 0; t < new_cnt; t++) {
            if (new_x[t] != x[t] || new_y[t] != y[t]) {
                changed = true;
                break;
            }
        }
    }

    for (int t = 0; t < t_max; t++) {
        x[t] = new_x[t];
        y[t] = new_y[t];
    }
    cnt = new_cnt;

    if (changed) {
        printf("cnt=%d", cnt);
        for (int t = 0; t < cnt; t++) printf(" (%d,%d)", x[t], y[t]);
        printf("\n");
    }
}


[[maybe_unused]]
static void rotations(Gt911 &ts)
{
    ts.rotation(Gt911::Rotation::bottom);
    printf("rotation: bottom\n");
    for (int i = 0; i < 10; i++) {
        poll_touch(ts);
        sleep_ms(200);
    }

    ts.rotation(Gt911::Rotation::left);
    printf("rotation: left\n");
    for (int i = 0; i < 10; i++) {
        poll_touch(ts);
        sleep_ms(200);
    }

    ts.rotation(Gt911::Rotation::top);
    printf("rotation: top\n");
    for (int i = 0; i < 10; i++) {
        poll_touch(ts);
        sleep_ms(200);
    }

    ts.rotation(Gt911::Rotation::right);
    printf("rotation: right\n");
    for (int i = 0; i < 10; i++) {
        poll_touch(ts);
        sleep_ms(200);
    }
}
