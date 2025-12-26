#pragma once

#include <cstdint>
//
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
//
#include "touchscreen.h"
#include "xassert.h"


class Gt911 : public Touchscreen
{

public:

    Gt911(i2c_inst_t *i2c, uint8_t i2c_adrs, int scl_pin, int sda_pin,
          int rst_pin, int int_pin, int i2c_freq = 400'000);
    virtual ~Gt911();

    bool init(int verbosity = 0);

    uint i2c_freq() const { return _i2c_freq; }

    // orientation (where the connector is)
    enum class Rotation {
        bottom, // portrait, connector at bottom
        left,   // landscape, connector to left
        top,    // portrait, connecter at top
        right,  // landscape, connector to right
    };

    void rotation(enum Rotation rot) { _rotation = rot; }
    enum Rotation rotation() const { return _rotation; }

    // get up to touch_cnt_max touches
    int get_touches(int *x, int *y, int touch_cnt_max, int verbosity = 0);

    // get one touch
    int get_touch(int &x, int &y, int verbosity = 0)
    {
        return get_touches(&x, &y, 1, verbosity);
    }

    void dump();

    const char *show_switch_1(uint8_t switch_1, char *buf, int buf_len) const;

private:

    static constexpr uint8_t i2c_adrs_0 = 0x5d; // if INT is 0 at reset
    static constexpr uint8_t i2c_adrs_1 = 0x14; // if INT is 1 at reset

    i2c_inst_t *_i2c;
    uint8_t _i2c_adrs; // i2c_adrs_0 or i2c_adrs_1
    const int _scl_pin;
    const int _sda_pin;
    uint _i2c_freq;

    const int _rst_pin;
    const int _int_pin;

    enum Rotation _rotation;

    int _x_res;
    int _y_res;

    static constexpr bool gpio_lo = false;
    static constexpr bool gpio_hi = true;

    // reset timing (see datasheet)
    static constexpr uint32_t reset_T1_us = 100;
    static constexpr uint32_t reset_T2_us = 100;
    static constexpr uint32_t reset_T3_us = 5'000;
    static constexpr uint32_t reset_T4_us = 50'000;

    enum Register : uint16_t {
        // 0x8040 - 0x8046 are command-related.
        // 0x8047 - 0x80fe are checksum-protected, so changes require a
        //                 checksum update at 0x80ff to have any effect.
        SWITCH_1 = 0x804d,   // 1 byte
        THRESH = 0x8053,     // 2 bytes: touch, leave
        PWR_CTRL = 0x8055,   // 1 byte
        // Most of 0x81xx is read-only
        VENDOR_ID = 0x8140,  // 4 bytes: '9', '1', '1', '\0'
        XY_RES = 0x8146,     // 4 bytes: x_lo, x_hi, y_lo, y_hi
        TOUCH_STAT = 0x814e, // 1 byte: writable to clear status
        TOUCH_1 = 0x8150,    // 4 bytes: x_lo, x_hi, y_lo, y_hi
        // TOUCH_2 = TOUCH_1 + 8
        // TOUCH_3 = TOUCH_2 + 8
        // TOUCH_4 = TOUCH_3 + 8
        // TOUCH_5 = TOUCH_4 + 8
    };

    // expected vendor ID
    static constexpr uint32_t vendor_id_exp = 0x39313100; // '9' '1' '1' '\0'

    friend Register &operator+=(Register &lhs, int rhs)
    {
        lhs = static_cast<Register>(static_cast<uint16_t>(lhs) + rhs);
        return lhs;
    }

    friend Register operator+(Register lhs, int rhs)
    {
        lhs += rhs;
        return lhs;
    }

    void out_low(int gpio_num)
    {
        gpio_init(gpio_num);
        gpio_put(gpio_num, false);    // low
        gpio_set_dir(gpio_num, true); // out
    }

    void reset(uint8_t i2c_adrs);

    int read(Register reg, uint8_t *buf, int buf_len);

    int write(Register reg, const uint8_t *buf, int buf_len);

    bool read_checked(Register reg, uint8_t *buf, int buf_len, //
                      const char *label = nullptr, int verbosity = 0);

    bool write_checked(Register reg, uint8_t *buf, int buf_len, //
                       const char *label = nullptr, int verbosity = 0);

    bool get_vendor_id(uint32_t &vendor_id, int verbosity = 0);
    bool read_resolution(int verbosity = 0);

    void rotate(int &x, int &y) const;
};
