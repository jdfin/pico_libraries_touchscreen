#pragma once

#include <cassert>


class Touchscreen
{
public:

    Touchscreen(int width, int height) :
        _phys_wid(width),
        _phys_hgt(height),
        _width(width),
        _height(height),
        _rotation(Rotation::landscape)
    {
        // Initialization of width, height, and rotation assume we
        // start out in landscape mode and _phys_wid >= _phys_hgt.
        assert(_rotation == Rotation::landscape ||
                _rotation == Rotation::landscape2);
        assert(_phys_wid >= _phys_hgt);
    }

    virtual ~Touchscreen() = default;

    int width() const
    {
        return _width;
    }

    int height() const
    {
        return _height;
    }

    // orientation
    enum class Rotation {
        portrait,   // portrait
        landscape,  // landscape, 90 degrees clockwise
        portrait2,  // portrait, 180 degrees from portrait
        landscape2, // landscape, 180 degrees from landscape
    };

    virtual void set_rotation(Rotation r)
    {
        _rotation = r;
        if (_rotation == Rotation::landscape ||
            _rotation == Rotation::landscape2) {
            _width = _phys_wid;
            _height = _phys_hgt;
            assert(_width >= _height);
        } else {
            assert(_rotation == Rotation::portrait ||
                    _rotation == Rotation::portrait2);
            _width = _phys_hgt;
            _height = _phys_wid;
            assert(_width <= _height);
        }
    }

    Rotation get_rotation() const
    {
        return _rotation;
    }

    // get up to touch_cnt_max touches
    virtual int get_touches(int col[], int row[], int touch_cnt_max,
                            int verbosity = 0) = 0;

    // get one touch
    int get_touch(int &col, int &row, int verbosity = 0)
    {
        return get_touches(&col, &row, 1, verbosity);
    }

    // clang-format off
    struct Event {
        enum class Type { none, down, up, move, } type;
        int col, row;
        Event() : type(Type::none), col(0), row(0) { }
        Event(Type t, int c, int r) : type(t), col(c), row(r) { }
        void reset()
        {
            type = Type::none;
            col = 0;
            row = 0;
        }
        const char *type_name() const
        {
            switch (type) {
                case Type::none: return "none";
                case Type::down: return "down";
                case Type::up:   return "up";
                case Type::move: return "move";
                default:         return "unknown";
            }
        }
    };
    // clang-format on

    // event state machine
    virtual Event get_event() = 0;

private:

    const int _phys_wid;
    const int _phys_hgt;

    // these two depend on rotation
    int _width;
    int _height;

    Rotation _rotation;
};
