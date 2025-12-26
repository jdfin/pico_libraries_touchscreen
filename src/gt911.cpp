
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
//
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
//
#include "gt911.h"
#include "touchscreen.h"
#include "xassert.h"


Gt911::Gt911(i2c_inst_t *i2c, uint8_t i2c_adrs, int scl_pin, int sda_pin,
             int rst_pin, int int_pin, int i2c_freq) :
    Touchscreen(),
    _i2c(i2c),
    _i2c_adrs(i2c_adrs),
    _scl_pin(scl_pin),
    _sda_pin(sda_pin),
    _rst_pin(rst_pin),
    _int_pin(int_pin)
{
    xassert(_i2c_adrs == i2c_adrs_0 || _i2c_adrs == i2c_adrs_1);

    xassert(_i2c != nullptr);
    _i2c_freq = i2c_init(_i2c, i2c_freq);
    gpio_set_function(_scl_pin, GPIO_FUNC_I2C);
    gpio_set_function(_sda_pin, GPIO_FUNC_I2C);

    out_low(_rst_pin);
    out_low(_int_pin);
}


Gt911::~Gt911()
{
}


void Gt911::reset(uint8_t i2c_adrs)
{
    xassert(i2c_adrs == i2c_adrs_0 || i2c_adrs == i2c_adrs_1);

    // See datasheet: INT pin is temporarily an output around reset time, and
    // whether it is hi or lo determines the i2c address.
    //     ____            _____________
    // RST     \__________/
    // INT ZZZZ_____/XXXXXXXXX\____ZZZZ
    //         | T1 | T2 | T3 | T4 |
    //
    // X: INT is hi or lo to set i2c address
    // Z: INT is changed to input

    out_low(_rst_pin);
    out_low(_int_pin);

    sleep_us(reset_T1_us);

    if (i2c_adrs == i2c_adrs_1)
        gpio_put(_int_pin, gpio_hi);

    sleep_us(reset_T2_us);

    gpio_put(_rst_pin, gpio_hi);

    sleep_us(reset_T3_us);

    gpio_put(_int_pin, gpio_lo);

    sleep_us(reset_T4_us);

    gpio_set_dir(_int_pin, false); // in
}


// verbosity:
// 0 - never print anything
// 1 - print message on error
// 2 - print registers as read
bool Gt911::init([[maybe_unused]] int verbosity)
{
    reset(_i2c_adrs);

    // check vendor ID
    uint32_t vendor_id;
    if (!get_vendor_id(vendor_id, verbosity))
        return false;
    if (vendor_id != vendor_id_exp) {
        if (verbosity >= 1)
            printf("Gt911::init: ERROR: vendor id incorrect\n");
        return false;
    }

    // configure automatic sleep
#if 0
    // XXX Changing PWR_CTRL seems to have no effect. Measured current
    // always drops about 3 seconds after a touch. Setting this register
    // then resetting (to see if it sticks) shows that the register just
    // goes back to 3 after reset.
    xassert(sizeof(buf) >= 1);
    if (!read_checked(Register::PWR_CTRL, buf, 1, "pwr_ctrl", verbosity))
        return false;
    if (verbosity >= 2) {
        printf("Gt911::init: pwr_ctrl=0x%02x\n", buf[0]);
    }
    buf[0] = (buf[0] & 0xf0) | 15;
    if (write(Register::PWR_CTRL, buf, 1) != 1) {
        if (verbosity >= 1)
            printf("Gt911::init: ERROR: writing pwr_ctrl\n");
        return false;
    }
    if (!read_checked(Register::PWR_CTRL, buf, 1, "pwr_ctrl", verbosity))
        return false;
    if (verbosity >= 2) {
        printf("Gt911::init: pwr_ctrl=0x%02x\n", buf[0]);
    }
#endif

    // check resolution
    if (!read_resolution(verbosity))
        return false;
    if (verbosity >= 2)
        printf("Gt911::init: resolution = (%d, %d)\n", _x_res, _y_res);

    // check INT trigger mode, x/y reverse (0x804d)
    uint8_t switch_1;
    if (read_checked(Register::SWITCH_1, &switch_1, 1, "switch_1", verbosity) !=
        1)
        return false;
    if (verbosity >= 2) {
        char buf[64];
        printf("Gt911::init: %s\n", show_switch_1(switch_1, buf, sizeof(buf)));
    }

    // check screen touch/leave thresholds (0x8053-0x8054)
    uint8_t buf[2];
    if (!read_checked(Register::THRESH, buf, 2, "thresh", verbosity))
        return false;
    if (verbosity >= 2)
        printf("Gt911::init: touch=%d leave=%d\n", int(buf[0]), int(buf[1]));

    return true;
}


bool Gt911::get_vendor_id(uint32_t &vendor_id, int verbosity)
{
    uint8_t buf[4];
    if (!read_checked(Register::VENDOR_ID, buf, 4, "vendor_id", verbosity))
        return false;
    vendor_id = (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) |
                (uint32_t(buf[2]) << 8) | (uint32_t(buf[3]) << 0);
    return true;
}


bool Gt911::read_resolution(int verbosity)
{
    uint8_t buf[4];
    if (!read_checked(Register::XY_RES, buf, 4, "xy_res", verbosity))
        return false;
    _x_res = (int(buf[1]) << 8) | buf[0];
    _y_res = (int(buf[3]) << 8) | buf[2];
    return true;
}


int Gt911::get_touches(int *x, int *y, int touch_cnt_max, int verbosity)
{
    uint8_t status;
    if (read(Register::TOUCH_STAT, &status, sizeof(status)) != sizeof(status)) {
        if (verbosity >= 1)
            printf("Gt911::get_touches: ERROR: reading status register\n");
        return -1;
    }
    if (verbosity >= 2)
        printf("Gt911::get_touches: status=0x%02x", int(status));

    int touch_cnt = status & 0x0f;

    constexpr int buf_len = 4;
    uint8_t buf[buf_len];

    Register base = Register::TOUCH_1;

    for (int t = 0; t < touch_cnt && t < touch_cnt_max; t++) {

        if (read(base, buf, buf_len) != buf_len) {
            if (verbosity >= 1)
                printf("Gt911::get_touches: ERROR: reading point %d\n", t + 1);
            return -1;
        }
        x[t] = (int(buf[1]) << 8) | buf[0];
        y[t] = (int(buf[3]) << 8) | buf[2];
        rotate(x[t], y[t]);
        if (verbosity >= 2)
            printf(" {%02x %02x %02x %02x}", int(buf[0]), int(buf[1]),
                   int(buf[2]), int(buf[3]));

        base += 8;

    } // for (int t...)

    if (verbosity >= 2)
        printf("\n");

    status = 0x00;
    if (write(Register::TOUCH_STAT, &status, sizeof(status)) !=
        sizeof(status)) {
        if (verbosity >= 1)
            printf("Gt911::get_touches: ERROR: writing status register\n");
    }

    return touch_cnt;
}

// Both i2c_write and i2c_read return:
//   number of bytes on success
//   PICO_ERROR_GENERIC if no ack
//   PICO_ERROR_TIMEOUT if timeout
int Gt911::read(Gt911::Register reg, uint8_t *buf, int buf_len)
{
    static_assert(sizeof(reg) == 2);

    // don't assume an endian
    constexpr int xbuf_len = sizeof(reg);
    const uint8_t xbuf[xbuf_len] = {uint8_t(reg >> 8), uint8_t(reg)};

    constexpr uint timeout_us = 10'000;
    int err =
        i2c_write_timeout_us(_i2c, _i2c_adrs, xbuf, xbuf_len, true, timeout_us);
    if (err != xbuf_len)
        return err;
    return i2c_read_timeout_us(_i2c, _i2c_adrs, buf, buf_len, false,
                               timeout_us);
}


int Gt911::write(Gt911::Register reg, const uint8_t *buf, int buf_len)
{
    static_assert(sizeof(reg) == 2);

    constexpr int xbuf_len = 32;
    uint8_t xbuf[xbuf_len];

    xassert(buf_len < (xbuf_len - static_cast<int>(sizeof(reg))));

    xbuf[0] = uint8_t(reg >> 8); // hi byte
    xbuf[1] = uint8_t(reg);      // lo byte
    for (int i = 0; i < buf_len; i++) xbuf[sizeof(reg) + i] = buf[i];

    constexpr uint timeout_us = 10'000;
    int ret = 0;
    ret = i2c_write_timeout_us(_i2c, _i2c_adrs, xbuf, sizeof(reg) + buf_len,
                               false, timeout_us);
    if (ret >= 0)
        return ret - sizeof(reg); // return buf_len if everything went ok
    else
        return ret; // negative, error code
}


bool Gt911::read_checked(Register reg, uint8_t *buf, int buf_len, //
                         const char *label, int verbosity)
{
    xassert(verbosity == 0 || label != nullptr);
    if (read(reg, buf, buf_len) != buf_len) {
        if (verbosity >= 1)
            printf("Gt911: ERROR: reading %s\n", label);
        return false;
    }
    if (verbosity >= 2) {
        printf("Gt911: %s = {", label);
        for (int i = 0; i < buf_len; i++) //
            printf(" %02x", buf[i]);
        printf(" }\n");
    }
    return true;
}


bool Gt911::write_checked(Register reg, uint8_t *buf, int buf_len, //
                          const char *label, int verbosity)
{
    xassert(verbosity == 0 || label != nullptr);
    if (write(reg, buf, buf_len) != buf_len) {
        if (verbosity >= 1)
            printf("Gt911: ERROR: writing %s\n", label);
        return false;
    }
    if (verbosity >= 2) {
        printf("Gt911: %s = {", label);
        for (int i = 0; i < buf_len; i++) //
            printf(" %02x", buf[i]);
        printf(" }\n");
    }
    return true;
}


void Gt911::rotate(int &x, int &y) const
{
    if (x < 0) x = 0;
    if (x >= _x_res) x = _x_res - 1;
    if (y < 0) y = 0;
    if (y >= _y_res) y = _y_res - 1;
    switch (_rotation) {
        case Rotation::top:
            x = _x_res - x - 1; // 0..._x_res-1 -> _x_res-1...0
            y = _y_res - y - 1; // 0..._y_res-1 -> _y_res-1...0
            break;
        case Rotation::right:
            std::swap(x, y);
            y = _x_res - y - 1; // 0..._x_res-1 -> _x_res-1...0
            break;
        case Rotation::bottom:
            // do nothing
            break;
        case Rotation::left:
            std::swap(x, y);
            x = _y_res - x - 1; // 0..._y_res-1 -> _y_res-1...0
            break;
    }
}


void Gt911::dump()
{
    constexpr int buf_len = 16;
    uint8_t buf[buf_len];

    uint16_t base;

    base = 0x8000;
    for (int i = 0; i < 16; i++) {
        printf("%04x:", base + i * 16);
        if (read(Register(base + i * 16), buf, buf_len) == buf_len) {
            for (int j = 0; j < buf_len; j++) printf(" %02x", buf[j]);
        } else {
            printf(" ERROR reading");
        }
        printf("\n");
    }

    base = 0x8100;
    for (int i = 0; i < 16; i++) {
        printf("%04x:", base + i * 16);
        if (read(Register(base + i * 16), buf, buf_len) == buf_len) {
            for (int j = 0; j < buf_len; j++) printf(" %02x", buf[j]);
        } else {
            printf(" ERROR reading");
        }
        printf("\n");
    }
}


const char *Gt911::show_switch_1(uint8_t switch_1, char *buf, int buf_len) const
{
    memset(buf, '\0', buf_len);
    char *b = buf;
    char *e = buf + buf_len;
    b += snprintf(b, e - b, "switch_1=0x%02x", int(switch_1));
    b += snprintf(b, e - b, " y2y=%d", (switch_1 >> 7) & 1);
    b += snprintf(b, e - b, " x2x=%d", (switch_1 >> 6) & 1);
    b += snprintf(b, e - b, " x2y=%d", (switch_1 >> 3) & 1);
    const char *int_mode[] = {"rising", "falling", "low", "high"};
    b += snprintf(b, e - b, " int=%s", int_mode[switch_1 & 3]);
    return buf;
}


#if 0

Hosyond Display:

00: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 ff ff
10: ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff
20: ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff
30: ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff
40: ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff
50: ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff
60: ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff
70: ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff
80: 0f 00 00 00 00 a0 01 1e  0a 28 00 00 00 00 00 00
90: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 26
a0: 02 05 01 64 01 00 a3 00  11 0f 00 00 00 00 00 01
b0: 00 00 00 00 00 00 00 00  00 00 00 00 01 00 00 00
c0: 00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
d0: 00 ff ff 00 00 ff ff ff  ff ff 00 00 ff ff ff ff
e0: ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff
f0: ff ff ff ff ff ff ff ff  ff ff ff ff 01 ff ff ff

Waveshare Display:

8000: 47 4f 4f 44  49 58 5f 47  54 39 30 30  5f 31 30 35
8010: 38 00 81 49  9f 45 8a 5c  b7 3f 82 00  e7 07 fc a8
8020: cf 3d 0e 89  cc 1b 0b 10  ac 79 08 28  f9 dd 64 04
8030: 06 43 55 53  aa 55 fa 15  06 43 55 53  aa 55 fa 15
8040: ff 00 00 00  00 00 00 ff  40 01 e0 01  05 81 00 08
8050: ff 1e 0f 5a  3c 03 05 00  00 00 00 00  00 00 00 00
8060: 00 00 89 20  06 37 35 43  06 00 00 01  b9 03 1c 63
8070: 00 00 00 00  03 64 32 00  00 00 28 64  94 c5 02 07
8080: 00 00 04 99  2c 00 84 34  00 71 3f 00  5e 4c 00 4f
8090: 5b 00 4f 00  00 00 00 00  00 00 00 00  00 00 00 00
80a0: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
80b0: 00 00 00 00  00 00 00 0c  0a 08 06 04  02 ff ff ff
80c0: ff ff ff ff  ff 00 00 00  00 00 00 00 00 00 00 00
80d0: 00 00 00 00  00 0a 0c 0f  10 08 06 04  02 00 ff ff
80e0: ff ff ff ff  ff ff ff ff  ff ff ff ff  ff ff ff 00
80f0: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 32

8100: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
8110: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
8120: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
8130: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
8140: 39 31 31 00  60 10 40 01  e0 01 00 00  00 00 80 00
8150: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
8160: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
8170: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
8180: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
8190: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
81a0: 00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00
81b0: 00 00 00 00  00 00 00 00  00 00 40 00  01 00 00 bf
81c0: 07 b8 07 f9  08 2b 08 65  08 72 08 68  07 01 07 53
81d0: 07 4c 07 5f  07 60 07 52  06 ff 07 53  07 4a 07 5f
81e0: 07 60 07 56  06 fd 07 4f  07 46 07 5a  07 5b 07 5a
81f0: 07 54 07 59  07 4d 07 5e  07 5a 07 0f  07 55 07 56

#endif
