/* Standalone C++17 regression test; no renderer or optional dependencies. */
#include "math/math.h"

constexpr bool wrappedIndicesStayInRange()
{
    for(int size = 1; size <= 64; size *= 2)
        for(int coordinate = -256; coordinate <= 256; ++coordinate)
        {
            const int wrapped = yafaray::math::mod(coordinate, size);
            if(wrapped < 0 || wrapped >= size) return false;
            if((wrapped - coordinate) % size != 0) return false;
        }
    return true;
}

static_assert(yafaray::math::mod(-16, 16) == 0, "Negative exact multiples must wrap to zero");
static_assert(yafaray::math::mod(-1, 1) == 0, "A 1x1 mipmap has only texel zero");
static_assert(yafaray::math::mod(-17, 16) == 15, "Negative nonmultiples still wrap correctly");
static_assert(yafaray::math::mod(16, 16) == 0, "Positive multiples still wrap correctly");
static_assert(wrappedIndicesStayInRange(), "Wrapped texture coordinates must stay in bounds");

int main() { return 0; }
