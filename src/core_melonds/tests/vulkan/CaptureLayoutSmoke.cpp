/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace
{

using Pixel = std::array<unsigned char, 4>;

struct Point
{
    int X;
    int Y;
};

Point CompactToCanonical(int x, int y, int scale)
{
    const int logicalX = x / scale;
    const int logicalY = y / scale;
    const int subpixelX = x % scale;
    const int subpixelY = y % scale;
    return
    {
        (logicalX + (logicalY & 1) * 128) * scale + subpixelX,
        (logicalY >> 1) * scale + subpixelY,
    };
}

Point CanonicalToCompact(int x, int y, int scale)
{
    const int logicalX = x / scale;
    const int logicalY = y / scale;
    const int subpixelX = x % scale;
    const int subpixelY = y % scale;
    const int half = logicalX / 128;
    return
    {
        (logicalX & 127) * scale + subpixelX,
        (logicalY * 2 + half) * scale + subpixelY,
    };
}

Pixel Pattern(int x, int y, int scale, int salt)
{
    return
    {
        static_cast<unsigned char>((x * 17 + y * 3 + salt) & 0xFF),
        static_cast<unsigned char>((x * 5 + y * 29 + scale) & 0xFF),
        static_cast<unsigned char>((x ^ y ^ salt) & 0xFF),
        static_cast<unsigned char>(0x80 | ((x + y + salt) & 0x7F)),
    };
}

bool CheckScale(int scale)
{
    const int compactWidth = 128 * scale;
    const int compactHeight = 128 * scale;
    const int canonicalWidth = 256 * scale;
    const int canonicalHeight = 64 * scale;
    const size_t pixelCount = static_cast<size_t>(compactWidth) * compactHeight;

    std::vector<Pixel> compact(pixelCount);
    std::vector<Pixel> canonical(pixelCount);
    std::vector<bool> canonicalWritten(pixelCount);
    for (int y = 0; y < compactHeight; y++)
    {
        for (int x = 0; x < compactWidth; x++)
        {
            const size_t compactIndex = static_cast<size_t>(y) * compactWidth + x;
            compact[compactIndex] = Pattern(x, y, scale, 11);
            const Point mapped = CompactToCanonical(x, y, scale);
            const size_t canonicalIndex =
                static_cast<size_t>(mapped.Y) * canonicalWidth + mapped.X;
            if (mapped.X < 0 || mapped.X >= canonicalWidth ||
                mapped.Y < 0 || mapped.Y >= canonicalHeight ||
                canonicalWritten[canonicalIndex])
                return false;
            canonical[canonicalIndex] = compact[compactIndex];
            canonicalWritten[canonicalIndex] = true;
        }
    }

    for (int y = 0; y < canonicalHeight; y++)
    {
        for (int x = 0; x < canonicalWidth; x++)
        {
            const size_t canonicalIndex = static_cast<size_t>(y) * canonicalWidth + x;
            const Point mapped = CanonicalToCompact(x, y, scale);
            if (mapped.X < 0 || mapped.X >= compactWidth ||
                mapped.Y < 0 || mapped.Y >= compactHeight)
                return false;
            const size_t compactIndex =
                static_cast<size_t>(mapped.Y) * compactWidth + mapped.X;
            if (!canonicalWritten[canonicalIndex] ||
                canonical[canonicalIndex] != compact[compactIndex])
                return false;
        }
    }

    std::fill(canonicalWritten.begin(), canonicalWritten.end(), false);
    for (int y = 0; y < canonicalHeight; y++)
    {
        for (int x = 0; x < canonicalWidth; x++)
        {
            const size_t canonicalIndex = static_cast<size_t>(y) * canonicalWidth + x;
            canonical[canonicalIndex] = Pattern(x, y, scale, 73);
            const Point mapped = CanonicalToCompact(x, y, scale);
            if (mapped.X < 0 || mapped.X >= compactWidth ||
                mapped.Y < 0 || mapped.Y >= compactHeight)
                return false;
            const size_t compactIndex =
                static_cast<size_t>(mapped.Y) * compactWidth + mapped.X;
            if (canonicalWritten[compactIndex])
                return false;
            compact[compactIndex] = canonical[canonicalIndex];
            canonicalWritten[compactIndex] = true;
        }
    }

    for (int y = 0; y < compactHeight; y++)
    {
        for (int x = 0; x < compactWidth; x++)
        {
            const size_t compactIndex = static_cast<size_t>(y) * compactWidth + x;
            const Point mapped = CompactToCanonical(x, y, scale);
            if (mapped.X < 0 || mapped.X >= canonicalWidth ||
                mapped.Y < 0 || mapped.Y >= canonicalHeight)
                return false;
            const size_t canonicalIndex =
                static_cast<size_t>(mapped.Y) * canonicalWidth + mapped.X;
            if (!canonicalWritten[compactIndex] ||
                compact[compactIndex] != canonical[canonicalIndex])
                return false;
        }
    }

    return true;
}

}

int main()
{
    for (int scale : {1, 2, 4})
    {
        if (!CheckScale(scale))
        {
            fprintf(stderr, "Capture layout mapping failed at %dx scale\n", scale);
            return 1;
        }
    }

    puts("Capture layout mapping passed at 1x, 2x, and 4x");
    return 0;
}
