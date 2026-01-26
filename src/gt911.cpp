
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
// pico
#include "hardware/gpio.h"
#include "pico/stdlib.h"
// misc
#include "i2c_dev.h"
// touchscreen
#include "gt911.h"
#include "touchscreen.h"


Gt911::Gt911(I2cDev &i2c, uint8_t i2c_addr, int rst_pin, int int_pin) :
    Touchscreen(480, 320),
    _i2c(i2c),
    _i2c_addr(i2c_addr),
    _rst_pin(rst_pin),
    _int_pin(int_pin),
    _poll_us(0),
    _i2c_state(I2cState::idle)
{
    assert(_i2c_addr == i2c_addr_0 || _i2c_addr == i2c_addr_1);
    out_low(_rst_pin);
    out_low(_int_pin);
}


void Gt911::reset(uint8_t i2c_addr)
{
    // See datasheet: INT pin is temporarily an output around reset time, and
    // whether it is hi or lo determines the i2c address.
    //     ____            _____________
    // RST     \__________/
    // INT ZZZZ_____/XXXXXXXXX\____ZZZZ
    //         | T1 | T2 | T3 | T4 |
    //
    // X: INT is hi or lo to set i2c address
    // Z: INT is changed to input
    assert(i2c_addr == i2c_addr_0 || i2c_addr == i2c_addr_1);
    out_low(_rst_pin);
    out_low(_int_pin);
    sleep_us(reset_T1_us);
    if (i2c_addr == i2c_addr_1)
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
bool Gt911::init(int verbosity)
{
    reset(_i2c_addr);

    // check vendor ID
    uint32_t vendor_id;
    if (!get_vendor_id(vendor_id, verbosity))
        return false;
    if (vendor_id != vendor_id_exp) {
        if (verbosity >= 1)
            printf("Gt911::init: ERROR: vendor id incorrect\n");
        return false;
    }

    // check resolution
    if (!read_resolution(verbosity))
        return false;
    if (verbosity >= 2)
        printf("Gt911::init: resolution = (x_res=%d, y_res=%d)\n", _x_res,
               _y_res);

    // check INT trigger mode, x/y reverse (0x804d)
    uint8_t switch_1;
    if (read_checked(Reg::SWITCH_1, &switch_1, 1, "switch_1", verbosity) != 1)
        return false;
    if (verbosity >= 2) {
        char buf[64];
        printf("Gt911::init: %s\n", show_switch_1(switch_1, buf, sizeof(buf)));
    }

    assert(_x_res == 320 && _y_res == 480);
    assert((switch_1 & 0xc0) == 0x80); // y2y=1, x2x=0

    // The following interpretations of (x,y) could be generalized.
    //
    // We can look at the display in landscape mode:
    //     y=479               y=0
    //     +---------------------+ x=0
    //     |                     |
    //     |                     |
    // conn|                     |
    //     |                     |
    //     |                     |
    //     +---------------------+ x=319
    //
    // Or in portrait mode:
    //   x=0        x=319
    //   +--------------+ y=0
    //   |              |
    //   |              |
    //   |              |
    //   |              |
    //   |              |
    //   |              |
    //   |              |
    //   |              |
    //   +--------------+ y=479
    //         conn
    //
    // Default rotation in class Touchscreen is landscape, and (0,0) is
    // always at the top-left. A straightforward mapping from (x,y) from
    // the GT911 to reported coordinates is to use y as the column (0..419)
    // but reverse it horizontally, and x as the row, with no reversal.
    //
    //             col      row
    // landscape:  479-y    x
    // landscape2: y        319-x
    // portrait:   x        y
    // portrait2:  319-x    479-y

    // check screen touch/leave thresholds (0x8053-0x8054)
    uint8_t buf[2];
    if (!read_checked(Reg::THRESH, buf, 2, "thresh", verbosity))
        return false;
    if (verbosity >= 2)
        printf("Gt911::init: touch=%d leave=%d\n", int(buf[0]), int(buf[1]));

    return true;
}


bool Gt911::get_vendor_id(uint32_t &vendor_id, int verbosity)
{
    uint8_t buf[4];
    if (!read_checked(Reg::VENDOR_ID, buf, 4, "vendor_id", verbosity))
        return false;
    vendor_id = (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) |
                (uint32_t(buf[2]) << 8) | (uint32_t(buf[3]) << 0);
    return true;
}


bool Gt911::read_resolution(int verbosity)
{
    uint8_t buf[4];
    if (!read_checked(Reg::XY_RES, buf, 4, "xy_res", verbosity))
        return false;
    _x_res = (int(buf[1]) << 8) | buf[0];
    _y_res = (int(buf[3]) << 8) | buf[2];
    return true;
}


// Theoretical timing:
//   read status: 122.5 usec
//   read one touch point: 190.0 usec
//   write status: 95.0 usec
// At the very least (no touches), this takes 122.5 usec.
// With 1 touch, 407.5 usec; with 2 touches, 597.5 usec; etc.
int Gt911::get_touches(int col[], int row[], int touch_cnt_max, int verbosity)
{
    // Status register indicates whether there are any touches to read.
    uint8_t status;
    if (read(Reg::TOUCH_STAT, &status, sizeof(status)) != sizeof(status)) {
        if (verbosity >= 1)
            printf("Gt911::get_touches: ERROR: reading status register\n");
        return -1;
    }
    if (verbosity >= 2)
        printf("Gt911::get_touches: status=0x%02x", int(status));

    // MSB of status is 1 if the lower nibble contains the number of touches
    // to read. It is unclear whether the number of touches is valid if MSB
    // is 0. Perhaps there is a race condition with the updating of the touch
    // data and the different fields of the status register, so let's be
    // pedantic about it.
    int touch_cnt = 0;
    if ((status & 0x80) != 0)
        touch_cnt = status & 0x0f; // touch_cnt can still be 0

    constexpr int buf_len = 4;
    uint8_t buf[buf_len];

    Reg base = Reg::TOUCH_1;

    // Read touch points up to the number reported in status or the size of
    // the col[] and row[] arrays, whichever is smaller.
    for (int t = 0; t < touch_cnt && t < touch_cnt_max; t++) {

        if (read(base, buf, buf_len) != buf_len) {
            if (verbosity >= 1)
                printf("Gt911::get_touches: ERROR: reading point %d\n", t + 1);
            return -1;
        }
        int x = (int(buf[1]) << 8) | buf[0];
        int y = (int(buf[3]) << 8) | buf[2];
        rotate(x, y, col[t], row[t]);
        if (verbosity >= 2)
            printf(" {%02x %02x %02x %02x}", int(buf[0]), int(buf[1]),
                   int(buf[2]), int(buf[3]));

        base += 8;

    } // for (int t...)

    if (verbosity >= 2)
        printf("\n");

    // Clear status if we read any touches. It is possible this is what tells
    // the chip it is free to update its touch data again.
    if ((status & 0x80) != 0) {
        status = 0x00;
        if (write(Reg::TOUCH_STAT, &status, sizeof(status)) != sizeof(status)) {
            if (verbosity >= 1)
                printf("Gt911::get_touches: ERROR: writing status register\n");
        }
    }

    // Return the number of touches reported by the chip, even if we did not
    // read all of them.
    return touch_cnt;
}


// Both i2c_write and i2c_read return:
//   number of bytes on success
//   PICO_ERROR_GENERIC if no ack
//   PICO_ERROR_TIMEOUT if timeout
//
// Theoretical timing @ 400 KHz (2.5 usec/bit):
//   write N bytes:
//      S, A+W, A, RHI, A, RLO, A, { DAT, A }n, S = 29 + 9n bits = 72.5 + 22.5n usec
//   read N bytes:
//      S, A+W, A, RHI, A, RLO, A, S = 29 bits
//      S, A+R, A, { DAT, A }n, S = 11 + 9n bits
//      total = 40 + 9n bits = 100 + 22.5n usec

int Gt911::read(Gt911::Reg reg, uint8_t *buf, int buf_len)
{
    static_assert(sizeof(reg) == 2);

    // don't assume an endian
    constexpr int xbuf_len = sizeof(reg);
    const uint8_t xbuf[xbuf_len] = {uint8_t(reg >> 8), uint8_t(reg)};

    constexpr uint timeout_us = 10'000;
    int err = _i2c.write_sync(_i2c_addr, xbuf, xbuf_len, true, timeout_us);
    if (err != xbuf_len)
        return err;
    return _i2c.read_sync(_i2c_addr, buf, buf_len, false, timeout_us);
}


int Gt911::write(Gt911::Reg reg, const uint8_t *buf, int buf_len)
{
    static_assert(sizeof(reg) == 2);

    constexpr int xbuf_len = 32;
    uint8_t xbuf[xbuf_len];

    assert(buf_len < (xbuf_len - static_cast<int>(sizeof(reg))));

    xbuf[0] = uint8_t(reg >> 8); // hi byte
    xbuf[1] = uint8_t(reg);      // lo byte
    for (int i = 0; i < buf_len; i++)
        xbuf[sizeof(reg) + i] = buf[i];

    constexpr uint timeout_us = 10'000;
    int ret = _i2c.write_sync(_i2c_addr, xbuf, sizeof(reg) + buf_len, false,
                              timeout_us);
    if (ret >= 0)
        return ret - sizeof(reg); // return buf_len if everything went ok
    else
        return ret; // negative, error code
}


bool Gt911::read_checked(Reg reg, uint8_t *buf, int buf_len, //
                         const char *label, int verbosity)
{
    assert(verbosity == 0 || label != nullptr);
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


bool Gt911::write_checked(Reg reg, uint8_t *buf, int buf_len, //
                          const char *label, int verbosity)
{
    assert(verbosity == 0 || label != nullptr);
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


// Event State Machine


Touchscreen::Event Gt911::get_event()
{
    Touchscreen::Event event; // default: type=none

    if (_i2c.busy())
        return event; // nothing new

    uint32_t now_us;
    int32_t late_us;

    switch (_i2c_state) {

        case I2cState::idle:
            // Initial state, and where we delay for 1 msec between polls of
            // the status register (per "GT911 Programming Guide v0.1").
            now_us = time_us_32();
            // when now_us reaches _poll_us, we can poll
            late_us = now_us - _poll_us; // rollover-safe
            if (late_us >= 0) {
                start_status_read();
                _poll_us = now_us + 1'000; // next poll time
            }
            break;

        case I2cState::status_read:
            check_status_read(event);
            break;

        case I2cState::touch_read:
            check_touch_read(event);
            break;

        case I2cState::status_write:
            start_status_read();
            break;

        default:
            assert(false);
            break;

    } // switch (_i2c_state)

    return event;
}


void Gt911::start_status_read()
{
    const uint8_t wr_buf[] = {uint8_t(Reg::TOUCH_STAT >> 8),
                              uint8_t(Reg::TOUCH_STAT)};
    _i2c.write_read_async_start(_i2c_addr, wr_buf, sizeof(wr_buf), //
                                _status, sizeof(_status));
    _i2c_state = I2cState::status_read;
}


void Gt911::start_status_write()
{
    const uint8_t wr_buf[] = {uint8_t(Reg::TOUCH_STAT >> 8),
                              uint8_t(Reg::TOUCH_STAT), 0};
    _i2c.write_read_async_start(_i2c_addr, wr_buf, sizeof(wr_buf));
    _i2c_state = I2cState::status_write;
}


void Gt911::start_touch_read()
{
    uint8_t wr_buf[] = {uint8_t(Reg::TOUCH_1 >> 8), uint8_t(Reg::TOUCH_1)};
    _i2c.write_read_async_start(_i2c_addr, wr_buf, sizeof(wr_buf), //
                                _touch, sizeof(_touch));
    _i2c_state = I2cState::touch_read;
}


void Gt911::check_status_read(Event &event)
{
    if (_i2c.write_read_async_check() == sizeof(_status)) {
        // got the status byte
        bool touch_count_valid = (_status[0] & 0x80) != 0;
        if (touch_count_valid) {
            int touch_count = _status[0] & 0x0f;
            if (touch_count > 0) {
                start_touch_read(); // go get the touch
            } else {
                // no touches
                if (_last_event.type == Event::Type::down ||
                    _last_event.type == Event::Type::move) {
                    _last_event.type = Event::Type::up;
                    // leave col, row unchanged from down or move
                } else {
                    _last_event.reset(); // type=none, col=0, row=0
                }
                event = _last_event;
                start_status_write(); // clear status
            }
            return;
        }
    }
    // One of:
    //   did not get exactly one byte back from the status read; or
    //   touch count not valid.
    // In either case, delay and continue polling status read.
    _i2c_state = I2cState::idle;
}

void Gt911::check_touch_read(Event &event)
{
    if (_i2c.write_read_async_check() == sizeof(_touch)) {
        // got a touch
        int x = (int(_touch[1]) << 8) | _touch[0];
        int y = (int(_touch[3]) << 8) | _touch[2];
        int col, row;
        rotate(x, y, col, row);
        // _last_event.type is none only on the first call;
        // thereafter it is up, down, or move
        if (_last_event.type == Event::Type::none ||
            _last_event.type == Event::Type::up) {
            _last_event.type = Event::Type::down;
            _last_event.col = col;
            _last_event.row = row;
            event = _last_event;
        } else {
            assert(_last_event.type == Event::Type::down ||
                   _last_event.type == Event::Type::move);
            // only report a move if the touch actually moved
            if (_last_event.col != col || _last_event.row != row) {
                _last_event.type = Event::Type::move;
                _last_event.col = col;
                _last_event.row = row;
                event = _last_event;
            }
        }
    }
    // in either case, go clear status and continue polling
    start_status_write();
}


// Given a reading (x, y) from the chip, use its physical x_res and y_res
// along with the touchscreen's rotation to adjust (x, y) to the correct
// coordinates.
void Gt911::rotate(int x, int y, int &col, int &row) const
{
    // Need trickier code if this is not true or if the assert on switch_1 in
    // init() fails.
    assert(_x_res == 320 && _y_res == 480);

    // clamp inputs (have not observed out of range, but it's a cheap check)
    if (x < 0)
        x = 0;
    if (x >= _x_res)
        x = _x_res - 1;

    if (y < 0)
        y = 0;
    if (y >= _y_res)
        y = _y_res - 1;

    switch (get_rotation()) {

        case Rotation::landscape:
            col = (_y_res - 1) - y;
            row = x;
            break;

        case Rotation::portrait:
            col = x;
            row = y;
            break;

        case Rotation::landscape2:
            col = y;
            row = (_x_res - 1) - x;
            break;

        default:
            assert(get_rotation() == Rotation::portrait2);
            col = (_x_res - 1) - x;
            row = (_y_res - 1) - y;
            break;

    } // switch
}


void Gt911::dump()
{
    constexpr int buf_len = 16;
    uint8_t buf[buf_len];

    uint16_t base;

    base = 0x8000;
    for (int i = 0; i < 16; i++) {
        printf("%04x:", base + i * 16);
        if (read(Reg(base + i * 16), buf, buf_len) == buf_len) {
            for (int j = 0; j < buf_len; j++)
                printf(" %02x", buf[j]);
        } else {
            printf(" ERROR reading");
        }
        printf("\n");
    }

    base = 0x8100;
    for (int i = 0; i < 16; i++) {
        printf("%04x:", base + i * 16);
        if (read(Reg(base + i * 16), buf, buf_len) == buf_len) {
            for (int j = 0; j < buf_len; j++)
                printf(" %02x", buf[j]);
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
