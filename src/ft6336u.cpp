
#include <cstdint>
#include <cstdio>
//
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
//
#include "ft6336u.h"
#include "touchscreen.h"
#include "xassert.h"


Ft6336u::Ft6336u(i2c_inst_t *i2c, int scl_pin, int sda_pin, int rst_pin,
                 int int_pin, int i2c_freq) :
    Touchscreen(),
    _i2c(i2c),
    _scl_pin(scl_pin),
    _sda_pin(sda_pin),
    _rst_pin(rst_pin),
    _int_pin(int_pin)
{
    xassert(_i2c != nullptr);
    _i2c_freq = i2c_init(_i2c, i2c_freq);

    // Just drive the I2C signals low for now. The reset() method will switch
    // them back to I2C.
    xassert(_scl_pin >= 0);
    out_low(_scl_pin);

    xassert(_sda_pin >= 0);
    out_low(_sda_pin);

    xassert(_rst_pin >= 0);
    out_low(_rst_pin);

    if (_int_pin >= 0) {
        gpio_init(_int_pin);
        gpio_set_dir(_int_pin, false); // in
        // Sometimes the FT6336U is not driving INT and it is an input to the
        // chip; in those cases it should see it low.
        gpio_pull_down(_int_pin);
    }
}


Ft6336u::~Ft6336u()
{
}


void Ft6336u::reset()
{
    // When coming out of reset, the data sheet says INT and the I2C lines
    // should be low. INT is an input pulled low (constructor above) so it
    // shoule bo okay. Set the I2C lines to be low outputs while driving
    // reset, then switch them back to being I2C lines.

    gpio_put(_rst_pin, false); // low

    sleep_ms(1);

    // I2C signals low
    out_low(_scl_pin);
    out_low(_sda_pin);

    sleep_ms(TRST_ms);

    gpio_put(_rst_pin, true); // high

    // The data sheet doesn't say what the timing is from releasing reset to
    // having INT not driven and enabling I2C (letting the I2C lines go high).

    sleep_ms(1);

    // INT should already be an input, pulled down (constructor).

    // Enable I2C. The internal GPIO pull-ups are only 50K - 80K, so let's
    // just require external ones and not bother with the internal ones.
    gpio_set_function(_scl_pin, GPIO_FUNC_I2C);
    gpio_set_function(_sda_pin, GPIO_FUNC_I2C);

    // We are pulling INT low until the FT6336 drives it high.
    // Wait for INT to go high, then the first low pulse.

    uint32_t start_us;
    constexpr uint32_t max_wait_us = 1'000'000; // no idea

    // wait for INT high
    // measured to be ~125 msec
    start_us = time_us_32();
    while (!gpio_get(_int_pin) && (time_us_32() - start_us) <= max_wait_us);
    xassert(gpio_get(_int_pin));
    printf("INT high %lu usec after reset\n", time_us_32() - start_us);

#if 0
    // wait for INT low
    start_us = time_us_32();
    while (gpio_get(_int_pin) && (time_us_32() - start_us) <= max_wait_us)
        ;
    xassert(!gpio_get(_int_pin));
    printf("INT low %lu usec after INT high\n", time_us_32() - start_us);
#else
    // sleep more after INT goes high
    sleep_ms(600);
#endif
}


// verbosity:
// 0 - never print anything
// 1 - print message on error
// 2 - print registers as read
bool Ft6336u::init(int verbosity)
{
    reset();

    uint8_t buf[5];

    if (read(Register::FOCALTECH_ID, buf, 1) != 1) {
        if (verbosity >= 1)
            printf("Ft6336u: ERROR: reading 0x%02x\n",
                   int(Register::FOCALTECH_ID));
        return false; // error reading ID
    }
    if (verbosity >= 2) {
        printf("Ft6336u: register 0x%02x = 0x%02x\n",
               int(Register::FOCALTECH_ID), int(buf[0]));
    }
    if (buf[0] != 0x11) {
        if (verbosity >= 1)
            printf(
                "Ft6336u: ERROR: register 0x%02x = 0x%02x, expected 0x%02x\n",
                int(Register::FOCALTECH_ID), int(buf[0]), 0x11);
        return false; // incorrect ID
    }

    if (read(Register::CIPHER_MID, buf, 5) != 5) {
        if (verbosity >= 1)
            printf("Ft6336u: ERROR: reading 0x%02x..0x%02x\n",
                   int(Register::CIPHER_MID), int(Register::CIPHER_MID) + 4);
        return false; // error reading IDs
    }
    if (verbosity >= 2) {
        printf("Ft6336u: registers 0x%02x..0x%02x =",
               int(Register::FOCALTECH_ID), int(Register::FOCALTECH_ID) + 4);
        for (int i = 0; i < 5; i++) printf(" 0x%02x", int(buf[i]));
        printf("\n");
    }
    if (buf[0] != 0x26) {
        if (verbosity >= 1)
            printf(
                "Ft6336u: ERROR: register 0x%02x = 0x%02x, expected 0x%02x\n",
                int(Register::CIPHER_MID), int(buf[0]), 0x26);
        return false; // incorrect CIPHER_MID
    }
    if (buf[1] != 0x00 && buf[1] != 0x01 && buf[1] != 0x02) {
        if (verbosity >= 1)
            printf(
                "Ft6336u: ERROR: register 0x%02x = 0x%02x,"
                " expected 0x%02x, 0x%02x, or 0x%02x\n",
                int(Register::CIPHER_MID) + 1, int(buf[1]), 0x00, 0x01, 0x02);
        return false; // incorrect CIPHER_LOW
    }
    if (buf[4] != 0x64) {
        if (verbosity >= 1)
            printf(
                "Ft6336u: ERROR: register 0x%02x = 0x%02x, expected 0x%02x\n",
                int(Register::CIPHER_MID) + 4, int(buf[4]), 0x64);
        return false; // incorrect CIPHER_HIGH
    }
    return true;
}


int Ft6336u::get_touch(int &e1, int &x1, int &y1, int &e2, int &x2, int &y2,
                       int verbosity)
{
    constexpr int buf_len = 13;
    uint8_t buf[buf_len];

    // Read:
    //   TD_STATUS,
    //   P1_XH, P1_XL, P1_YH, P1_YL, P1_WEIGHT, P1_MISC,
    //   P2_XH, P2_XL, P2_YH, P2_YL, P2_WEIGHT, P2_MISC
    // Each read takes [adrs/w, reg/w, adrs/r, data/r, data/r...] on i2c,
    // or 3 + n_bytes. Doing three reads (TD_STATUS, P1_*, P2_*) where we
    // ignore WEIGHT and MISC takes 4 + 7 + 7 = 18 i2c bytes. Just reading
    // everything at once takes 16 i2c bytes and is more fun.
    // But we usually only need the status register (touch count = 0) or it
    // plus four more (touch count = 1).
    if (read(Register::TD_STATUS, buf, buf_len) != buf_len) {
        if (verbosity >= 1)
            printf("Ft6336::get_touch: ERROR: reading registers\n");
        return -1;
    }
    if (verbosity >= 2) {
        printf("Ft6336u::get_touch:");
        for (int i = 0; i < buf_len; i++) printf(" 0x%02x", int(buf[i]));
        printf("\n");
    }

    int cnt = buf[0] & 0x0f;
    // should be 0, 1, or 2
    if (cnt < 0 || cnt > 2) {
        if (verbosity >= 1)
            printf("Ft6336::get_touch: ERROR: TD_STATUS=0x%02x invalid\n",
                   int(buf[0]));
        return -1;
    }

    if (cnt >= 1) {
        e1 = (buf[1] >> 6) & 0x03;
        x1 = (int(buf[1] & 0x0f) << 8) | buf[2];
        y1 = (int(buf[3] & 0x0f) << 8) | buf[4];
        // buf[5], buf[6] not yet used
        if (cnt >= 2) {
            e2 = (buf[7] >> 6) & 0x03;
            x2 = (int(buf[7] & 0x0f) << 8) | buf[8];
            y2 = (int(buf[9] & 0x0f) << 8) | buf[10];
            // buf[11], buf[12] not yet used
        }
    }

    return cnt;
}

// Both i2c_write and i2c_read return:
//   number of bytes on success
//   PICO_ERROR_GENERIC if no ack
//   PICO_ERROR_TIMEOUT if timeout
int Ft6336u::read(Ft6336u::Register reg, uint8_t *buf, int buf_len)
{
    constexpr uint timeout_us = 10'000;
    int err = i2c_write_timeout_us(_i2c, i2c_adrs, (const uint8_t *)(&reg), 1,
                                   true, timeout_us);
    if (err != 1)
        return err;
    return i2c_read_timeout_us(_i2c, i2c_adrs, buf, buf_len, false, timeout_us);
}


int Ft6336u::write(Ft6336u::Register reg, const uint8_t *buf, int buf_len)
{
    constexpr int xbuf_len = 32;
    uint8_t xbuf[xbuf_len];

    xassert(buf_len < (xbuf_len - 1));

    xbuf[0] = reg;
    for (int i = 0; i < buf_len; i++) {
        xbuf[i + 1] = buf[i];
    }

    constexpr uint timeout_us = 10'000;
    int ret = 0;
    ret = i2c_write_timeout_us(_i2c, i2c_adrs, xbuf, buf_len + 1, false,
                               timeout_us);
    if (ret >= 0)
        return ret + 1;
    else
        return ret; // negative, error code
}


void Ft6336u::dump()
{
    constexpr int buf_len = 16;
    uint8_t buf[buf_len];

    for (int i = 0; i < 16; i++) {
        printf("%02x:", i * 16);
        if (read(Register(i * 16), buf, buf_len) == buf_len) {
            for (int j = 0; j < buf_len; j++) printf(" %02x", buf[j]);
        } else {
            printf(" ERROR reading");
        }
        printf("\n");
    }
}


#if 0
Hosyond Display:

00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff ff
10: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
20: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
30: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
40: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
50: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
60: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
70: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
80: 0f 00 00 00 00 a0 01 1e 0a 28 00 00 00 00 00 00
90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 26
a0: 02 05 01 64 01 00 a3 00 11 0f 00 00 00 00 00 01
b0: 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00
c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
d0: 00 ff ff 00 00 ff ff ff ff ff 00 00 ff ff ff ff
e0: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff
f0: ff ff ff ff ff ff ff ff ff ff ff ff 01 ff ff ff

#endif