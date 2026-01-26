
#include <cassert>
#include <cstdint>
#include <cstdio>
// pico
#include "hardware/i2c.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
// misc
#include "argv.h"
#include "i2c_dev.h"
#include "str_ops.h"
#include "sys_led.h"
// touchscreen
#include "gt911.h"
#include "touchscreen.h"

// Pico
//              +------| USB |------+
//            1 | D0       VBUS_OUT | 40
//            2 | D1        VSYS_IO | 39
//            3 | GND           GND | 38
//            4 | D2         3V3_EN | 37
//            5 | D3        3V3_OUT | 36
// (ts) SDA   6 | D4           AREF | 35
// (ts) SCL   7 | D5            D28 | 34
//            8 | GND           GND | 33
// (ts) RST   9 | D6            D27 | 32
// (ts) INT  10 | D7            D26 | 31
//           11 | D8            RUN | 30
//           12 | D9            D22 | 29  LED  (fb)
//           13 | GND           GND | 28
//           14 | D10           D21 | 27  RST  (fb)
//           15 | D11           D20 | 26  CD   (fb)
//           16 | D12           D19 | 25  MOSI (fb)
//           17 | D13           D18 | 24  SCK  (fb)
//           18 | GND           GND | 23
//           19 | D14           D17 | 22  CS   (fb)
//           20 | D15           D16 | 21  MISO (fb)
//              +-------------------+

static const int tp_sda_pin = 4;
static const int tp_scl_pin = 5;
static const int tp_rst_pin = 6;
static const int tp_int_pin = 7;
static const int tp_i2c_baud = 400'000;

static const uint8_t tp_addr = 0x14; // 0x14 or 0x5d

static void touches(Touchscreen &ts);
static void rotations(Touchscreen &ts);
static void poll_events(Touchscreen &ts);

static struct {
    const char *name;
    void (*func)(Touchscreen &);
} tests[] = {
    {"touches", touches},
    {"rotations", rotations},
    {"poll_events", poll_events},
};
static const int num_tests = sizeof(tests) / sizeof(tests[0]);


static void help()
{
    printf("\n");
    printf("Usage: enter test number (0..%d)\n", num_tests - 1);
    for (int i = 0; i < num_tests; i++)
        printf("%2d: %s\n", i, tests[i].name);
    printf("\n");
}


int main()
{
    stdio_init_all();

    SysLed::init();
    SysLed::pattern(50, 950);

    while (!stdio_usb_connected()) {
        SysLed::loop();
        tight_loop_contents();
    }

    sleep_ms(10);

    SysLed::off();

    printf("\n");
    printf("gt911_test\n");
    printf("\n");

    Argv argv(1); // verbosity == 1 means echo

    I2cDev i2c_dev(i2c0, tp_scl_pin, tp_sda_pin, tp_i2c_baud);

    printf("Gt911: i2c running at %u Hz\n", i2c_dev.baud());

    Gt911 gt911(i2c_dev, tp_addr, tp_rst_pin, tp_int_pin);

    constexpr int verbosity = 2;
    if (!gt911.init(verbosity)) {
        printf("Gt911: ERROR initializing\n");
        assert(false);
    }
    printf("Gt911: ready\n");

    gt911.set_rotation(Gt911::Rotation::landscape);

    sleep_ms(100);

    help();
    printf("> ");

    while (true) {
        int c = stdio_getchar_timeout_us(0);
        if (0 <= c && c <= 255) {
            if (argv.add_char(char(c))) {
                int test_num = -1;
                if (argv.argc() != 1) {
                    printf("\n");
                    printf("One integer only (got %d)\n", argv.argc());
                    help();
                } else if (!str_to_int(argv[0], &test_num)) {
                    printf("\n");
                    printf("Invalid test number: \"%s\"\n", argv[0]);
                    help();
                } else if (test_num < 0 || test_num >= num_tests) {
                    printf("\n");
                    printf("Test number out of range: %d\n", test_num);
                    help();
                } else {
                    printf("\n");
                    printf("Running \"%s\"\n", tests[test_num].name);
                    printf("\n");
                    tests[test_num].func(gt911);
                    printf("> ");
                }
                argv.reset();
            }
        }
    }

    sleep_ms(100);
    return 0;
}


// read status and report touches only if something changed
static void touches(Touchscreen &ts)
{
    constexpr int t_max = 5;

    static int col[t_max] = {-1, -1, -1, -1, -1};
    static int row[t_max] = {-1, -1, -1, -1, -1};
    static int cnt = -1;

    while (true) {
        int new_col[t_max];
        int new_row[t_max];
        int new_cnt = ts.get_touches(new_col, new_row, t_max);

        bool changed = false;
        if (new_cnt != cnt) {
            changed = true;
        } else {
            for (int t = 0; t < new_cnt; t++) {
                if (new_col[t] != col[t] || new_row[t] != row[t]) {
                    changed = true;
                    break;
                }
            }
        }

        for (int t = 0; t < t_max; t++) {
            col[t] = new_col[t];
            row[t] = new_row[t];
        }
        cnt = new_cnt;

        if (changed) {
            printf("cnt=%d", cnt);
            for (int t = 0; t < cnt; t++)
                printf(" (%d,%d)", col[t], row[t]);
            printf("\n");
        }

        sleep_ms(100);
    }
}


static void do_rotation(Touchscreen &ts, Touchscreen::Rotation r)
{
    ts.set_rotation(r);
    int w_13 = ts.width() / 3;
    int w_23 = (ts.width() * 2) / 3;
    int h_13 = ts.height() / 3;
    int h_23 = (ts.height() * 2) / 3;

    sleep_ms(1000);

    int cnt, col, row;

    // purge
    while (ts.get_touches(&col, &row, 1) > 0)
        ;

    while (true) {
        sleep_ms(200);
        cnt = ts.get_touches(&col, &row, 1);
        // return on touch near center
        // print other touches
        if (cnt == 0)
            continue;
        printf("(%d,%d)\n", col, row);
        if (col > w_13 && col < w_23 && row > h_13 && row < h_23)
            break;
    }
}


static void rotations(Touchscreen &ts)
{
    // test by touching near the corners, move on by touching near the center

    ts.set_rotation(Gt911::Rotation::landscape);
    printf("Rotation::landscape: width=%d height=%d\n", ts.width(),
           ts.height());
    do_rotation(ts, Gt911::Rotation::landscape);

    ts.set_rotation(Gt911::Rotation::portrait);
    printf("Rotation::portrait: width=%d height=%d\n", ts.width(), ts.height());
    do_rotation(ts, Gt911::Rotation::portrait);

    ts.set_rotation(Gt911::Rotation::landscape2);
    printf("Rotation::landscape2: width=%d height=%d\n", ts.width(),
           ts.height());
    do_rotation(ts, Gt911::Rotation::landscape2);

    ts.set_rotation(Gt911::Rotation::portrait2);
    printf("Rotation::portrait2: width=%d height=%d\n", ts.width(),
           ts.height());
    do_rotation(ts, Gt911::Rotation::portrait2);
}


static void poll_events(Touchscreen &ts)
{
    while (true) {
        Touchscreen::Event event(ts.get_event());
        if (event.type != Touchscreen::Event::Type::none)
            printf("poll_events: type=%s (%d, %d)\n", //
                   event.type_name(), event.col, event.row);
    }
}
