#pragma once

#include <cassert>
#include <cstdint>
// pico
#include "pico/stdlib.h"
// misc
#include "i2c_dev.h"
// touchscreen
#include "touchscreen.h"


class Gt911 : public Touchscreen
{

public:

    Gt911(I2cDev &i2c, uint8_t i2c_addr, int rst_pin, int int_pin);

    virtual ~Gt911() = default;

    bool init(int verbosity = 0);

    // get up to touch_cnt_max touches
    virtual int get_touches(int col[], int row[], int touch_cnt_max,
                            int verbosity = 0) override;

    // Event state machine
    // This always returns very quickly (no blocking on i2c). It will
    // usually see that a bus operation is in progress and just return.
    // When something finishes, it will process results, possibly returning
    // an event, and start another operation.
    virtual Event get_event() override;

    void dump();

    const char *show_switch_1(uint8_t switch_1, char *buf, int buf_len) const;

private:

    static constexpr uint8_t i2c_addr_0 = 0x5d; // if INT is 0 at reset
    static constexpr uint8_t i2c_addr_1 = 0x14; // if INT is 1 at reset

    I2cDev &_i2c;
    uint8_t _i2c_addr; // i2c_addr_0 or i2c_addr_1

    const int _rst_pin;
    const int _int_pin;

    int _x_res;
    int _y_res;

    static constexpr bool gpio_lo = false;
    static constexpr bool gpio_hi = true;

    // reset timing (see datasheet)
    static constexpr uint32_t reset_T1_us = 100;
    static constexpr uint32_t reset_T2_us = 100;
    static constexpr uint32_t reset_T3_us = 5'000;
    static constexpr uint32_t reset_T4_us = 50'000;

    enum Reg : uint16_t {
        // 0x8040 - 0x8046 are command-related.
        // 0x8047 - 0x80fe are checksum-protected, so changes require a
        //                 checksum update at 0x80ff to have any effect.
        SWITCH_1 = 0x804d, // 1 byte
        THRESH = 0x8053,   // 2 bytes: touch, leave
        PWR_CTRL = 0x8055, // 1 byte
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

    friend Reg &operator+=(Reg &lhs, int rhs)
    {
        lhs = static_cast<Reg>(static_cast<uint16_t>(lhs) + rhs);
        return lhs;
    }

    friend Reg operator+(Reg lhs, int rhs)
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

    void reset(uint8_t i2c_addr);

    int read(Reg reg, uint8_t *buf, int buf_len);

    int write(Reg reg, const uint8_t *buf, int buf_len);

    bool read_checked(Reg reg, uint8_t *buf, int buf_len, //
                      const char *label = nullptr, int verbosity = 0);

    bool write_checked(Reg reg, uint8_t *buf, int buf_len, //
                       const char *label = nullptr, int verbosity = 0);

    bool get_vendor_id(uint32_t &vendor_id, int verbosity = 0);
    bool read_resolution(int verbosity = 0);

    void rotate(int x, int y, int &col, int &row) const;

    // Event State Machine

    // Last event emitted
    Event _last_event;

    // Next time we might poll the status register.
    // According to the "GT911 Programming Guide v0.1", we're supposed to wait
    // at least 1 msec between polls, although before this delay was added it
    // seemed to work fine without the extra delay.
    uint32_t _poll_us;

    enum class I2cState {
        idle,
        status_read,
        touch_read,
        status_write,
    } _i2c_state;

    // for async reads
    uint8_t _status[1];
    uint8_t _touch[4];

    // The start_* and check_* functions are called by get_event() to
    // implement the event state machine. The start_* functions start an i2c
    // operation and set the state accordingly. The check_* functions retrieve
    // results of an i2c operation and process them, always starting another
    // i2c operation.

    void start_status_read();
    void start_status_write();
    void start_touch_read();

    void check_status_read(Event &event);
    void check_touch_read(Event &event);

}; // class Gt911
