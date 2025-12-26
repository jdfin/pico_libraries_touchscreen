#pragma once

#include <cstdint>
//
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
//
#include "touchscreen.h"
#include "xassert.h"


class Ft6336u : public Touchscreen
{

public:

    Ft6336u(i2c_inst_t *i2c, int scl_pin, int sda_pin, int rst_pin, int int_pin,
            int i2c_freq = 400'000);
    virtual ~Ft6336u();

    bool init(int verbosity = 0);

    uint i2c_freq() const { return _i2c_freq; }

    int get_touch(int &e1, int &x1, int &y1, int &e2, int &x2, int &y2,
                  int verbosity = 0);

    void dump();

private:

    static constexpr uint8_t i2c_adrs = 0x38;

    i2c_inst_t *_i2c;
    const int _scl_pin;
    const int _sda_pin;

    const int _rst_pin;
    static constexpr bool rst_assert = false; // assert low
    static constexpr bool rst_deassert = true;

    const int _int_pin;
    static constexpr bool int_assert = false; // assert low
    static constexpr bool int_deassert = true;

    uint _i2c_freq;

    static constexpr uint32_t TRST_ms = 5;

    enum Register : uint8_t {
        DEV_MODE = 0x00, // Device Mode
        //GEST_ID = 0x01,   // Gesture ID
        TD_STATUS = 0x02, // Number of touch points
        // 1st touch
        P1_XH = 0x03,     // [7:6] Event Flag [3:0] X Position [11:8]
        P1_XL = 0x04,     // [7:0] X Position
        P1_YH = 0x05,     // [7:4] ID [3:0] Y Position [11:8]
        P1_YL = 0x06,     // [7:0] Y Position
        P1_WEIGHT = 0x07, // [7:0] Weight
        P1_MISC = 0x08,   // [7:4] Area
        // 2nd touch
        P2_XH = 0x09,     // [7:6] Event Flag [3:0] X Position [11:8]
        P2_XL = 0x0a,     // [7:0] X Position
        P2_YH = 0x0b,     // [7:4] ID [3:0] Y Position [11:8]
        P2_YL = 0x0c,     // [7:0] Y Position
        P2_WEIGHT = 0x0d, // [7:0] Weight
        P2_MISC = 0x0e,   // [7:4] Area
        //
        TH_GROUP = 0x80, // threshold for touch detection
        PEAK_TH = 0x81,
        TH_DIFF = 0x85,          // filter function coefficient
        CTRL = 0x86,             // 0: keep active when no touching
                                 // 1: switch to monitor mode when no touching
        TIMEENTERMONITOR = 0x87, // time delay switching active to monitor
        PERIODACTIVE = 0x88,     // report rate in active mode
        PERIODMONITOR = 0x89,    // report rate in monitor mode
        FRQHOPFLG = 0x8a,
        FREQ_HOPPING_EN = 0x8b,
        CURFREQIDX = 0x8c,
        //RADIAN_VALUE = 0x91,      // min allowed angle for rotating gesture mode
        //OFFSET_LEFT_RIGHT = 0x92, // max offset for left/right gesture
        //OFFSET_UP_DOWN = 0x93,    // max offset for up/down gesture
        //DISTANCE_LEFT_RIGHT = 0x94, // min distance for left/right gesture
        //DISTANCE_UP_DOWN = 0x95,    // min distance for up/down gesture
        //DISTANCE_ZOOM = 0x96,       // max distance for zoom in/zoom out gesture
        TEST_MODE_FILTER = 0x96,
        CIPHER_MID = 0x9f,
        CIPHER_LOW = 0xa0,
        LIB_VER_H = 0xa1,       // lib version msb
        LIB_VER_L = 0xa2,       // lib version lsb
        CIPHER_HIGH = 0xa3,     // chip selecting
        G_MODE = 0xa4,          // 0: int polling mode; 1: int trigger mode
        PWR_MODE = 0xa5,        // current power mode
        FIRMID = 0xa6,          // firmware version
        FOCALTECH_ID = 0xa8,    // FocalTech panel id
        RELEASE_CODE_ID = 0xaf, // release code version
        STATE = 0xbc,           // current operating mode
    };

    void out_low(int gpio_num)
    {
        gpio_init(gpio_num);
        gpio_put(gpio_num, false); // low
        gpio_set_dir(gpio_num, true); // out
    }

    void reset();

    int read(Register reg, uint8_t *buf, int buf_len);

    int write(Register reg, const uint8_t *buf, int buf_len);
};
