#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
using std::min;
using std::max;
constexpr int HEX=16,DEC=10,BIN=2;
#define F(value) value
struct SilentSerial {
    template<class... T> void print(T...) {}
    template<class... T> void println(T...) {}
};
inline SilentSerial Serial;
