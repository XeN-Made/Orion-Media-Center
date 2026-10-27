// ORION Media Center - QR codes: a short text as a square of dark and light modules.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/qr.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <utility>

namespace hui
{

namespace
{

constexpr int kMaxVersion = 9; // a byte count of 8 bits: versions 1 to 9
// Error correction level M, by version (0 unused): codewords of correction
// in each block, and how many blocks the codewords are split into.
constexpr int kCorrection[kMaxVersion + 1] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22};
constexpr int kBlocks[kMaxVersion + 1] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5};

// The modules left for codewords once the fixed patterns are drawn.
int raw_modules(int version)
{
    int modules = (16 * version + 128) * version + 64;
    if (version >= 2)
    {
        const int align = version / 7 + 2;
        modules -= (25 * align - 10) * align - 55;
        if (version >= 7)
            modules -= 36; // the two version blocks
    }
    return modules;
}

int data_codewords(int version)
{
    return raw_modules(version) / 8 - kCorrection[version] * kBlocks[version];
}

// GF(256) over QR's polynomial, x^8 + x^4 + x^3 + x^2 + 1.
std::uint8_t multiply(std::uint8_t a, std::uint8_t b)
{
    int product = 0;
    for (int i = 7; i >= 0; --i)
    {
        product = (product << 1) ^ ((product >> 7) * 0x11d);
        product ^= ((b >> i) & 1) * a;
    }
    return static_cast<std::uint8_t>(product);
}

// Reed-Solomon: the generator of a degree (its leading 1 left out), and
// what is left of a block divided by it, the block's correction codewords.
std::vector<std::uint8_t> generator(int degree)
{
    std::vector<std::uint8_t> out(static_cast<std::size_t>(degree), 0);
    out.back() = 1;
    std::uint8_t root = 1;
    for (int i = 0; i < degree; ++i)
    {
        for (std::size_t j = 0; j < out.size(); ++j)
        {
            out[j] = multiply(out[j], root);
            if (j + 1 < out.size())
                out[j] ^= out[j + 1];
        }
        root = multiply(root, 0x02);
    }
    return out;
}

std::vector<std::uint8_t> remainder(const std::vector<std::uint8_t> &block,
                                    const std::vector<std::uint8_t> &divisor)
{
    std::vector<std::uint8_t> out(divisor.size(), 0);
    for (const std::uint8_t byte : block)
    {
        const std::uint8_t factor = byte ^ out.front();
        out.erase(out.begin());
        out.push_back(0);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] ^= multiply(divisor[i], factor);
    }
    return out;
}

// The data codewords split into blocks, each followed by its correction,
// then read across the blocks a codeword at a time. The last blocks may be
// one data codeword longer than the first: a short block's gap is skipped.
std::vector<std::uint8_t> interleaved(const std::vector<std::uint8_t> &data, int version)
{
    const int blocks = kBlocks[version];
    const int correction = kCorrection[version];
    const int raw = raw_modules(version) / 8;
    const int short_blocks = blocks - raw % blocks;
    const int short_length = raw / blocks;
    const std::vector<std::uint8_t> divisor = generator(correction);
    std::vector<std::vector<std::uint8_t>> parts;
    std::size_t at = 0;
    for (int i = 0; i < blocks; ++i)
    {
        const auto length =
            static_cast<std::size_t>(short_length - correction + (i < short_blocks ? 0 : 1));
        std::vector<std::uint8_t> block(data.begin() + static_cast<std::ptrdiff_t>(at),
                                        data.begin() + static_cast<std::ptrdiff_t>(at + length));
        at += length;
        const std::vector<std::uint8_t> tail = remainder(block, divisor);
        if (i < short_blocks)
            block.push_back(0);
        block.insert(block.end(), tail.begin(), tail.end());
        parts.push_back(std::move(block));
    }
    const auto gap = static_cast<std::size_t>(short_length - correction);
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < parts.front().size(); ++i)
    {
        for (std::size_t j = 0; j < parts.size(); ++j)
        {
            if (i != gap || j >= static_cast<std::size_t>(short_blocks))
                out.push_back(parts[j][i]);
        }
    }
    return out;
}

// The square being drawn: each module's colour, and whether a fixed
// pattern owns it (the data and the mask leave those alone).
struct Grid
{
    int size = 0;
    std::vector<std::uint8_t> dark;
    std::vector<std::uint8_t> fixed;

    std::size_t at(int x, int y) const
    {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
               static_cast<std::size_t>(x);
    }
    void set(int x, int y, bool on)
    {
        dark[at(x, y)] = on ? 1 : 0;
        fixed[at(x, y)] = 1;
    }
};

std::vector<int> alignment_centres(int version)
{
    if (version == 1)
        return {};
    const int count = version / 7 + 2;
    const int step = (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
    std::vector<int> out(static_cast<std::size_t>(count));
    out.front() = 6;
    int position = version * 4 + 17 - 7;
    for (int i = count - 1; i >= 1; --i, position -= step)
        out[static_cast<std::size_t>(i)] = position;
    return out;
}

// The format bits: the correction level (M is 00) and the mask, with their
// own correction, in their two copies around the finders.
void draw_format(Grid &grid, int mask)
{
    const int data = mask;
    int rest = data;
    for (int i = 0; i < 10; ++i)
        rest = (rest << 1) ^ ((rest >> 9) * 0x537);
    const int bits = ((data << 10) | rest) ^ 0x5412;
    const auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
    const int size = grid.size;
    for (int i = 0; i <= 5; ++i)
        grid.set(8, i, bit(i));
    grid.set(8, 7, bit(6));
    grid.set(8, 8, bit(7));
    grid.set(7, 8, bit(8));
    for (int i = 9; i < 15; ++i)
        grid.set(14 - i, 8, bit(i));
    for (int i = 0; i < 8; ++i)
        grid.set(size - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i)
        grid.set(8, size - 15 + i, bit(i));
    grid.set(8, size - 8, true); // always dark
}

void draw_fixed(Grid &grid, int version)
{
    const int size = grid.size;
    for (int i = 0; i < size; ++i)
    {
        grid.set(6, i, i % 2 == 0);
        grid.set(i, 6, i % 2 == 0);
    }
    // The three finders, each with its light separator.
    for (const auto &[cx, cy] : {std::pair{3, 3}, std::pair{size - 4, 3}, std::pair{3, size - 4}})
    {
        for (int dy = -4; dy <= 4; ++dy)
        {
            for (int dx = -4; dx <= 4; ++dx)
            {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x < 0 || x >= size || y < 0 || y >= size)
                    continue;
                const int ring = std::max(std::abs(dx), std::abs(dy));
                grid.set(x, y, ring != 2 && ring != 4);
            }
        }
    }
    const std::vector<int> centres = alignment_centres(version);
    const std::size_t last = centres.empty() ? 0 : centres.size() - 1;
    for (std::size_t i = 0; i < centres.size(); ++i)
    {
        for (std::size_t j = 0; j < centres.size(); ++j)
        {
            // Not over the finders.
            if ((i == 0 && j == 0) || (i == 0 && j == last) || (i == last && j == 0))
                continue;
            for (int dy = -2; dy <= 2; ++dy)
            {
                for (int dx = -2; dx <= 2; ++dx)
                    grid.set(centres[i] + dx, centres[j] + dy,
                             std::max(std::abs(dx), std::abs(dy)) != 1);
            }
        }
    }
    draw_format(grid, 0); // its place, for now
    if (version >= 7)
    {
        int rest = version;
        for (int i = 0; i < 12; ++i)
            rest = (rest << 1) ^ ((rest >> 11) * 0x1f25);
        const long bits = (static_cast<long>(version) << 12) | rest;
        for (int i = 0; i < 18; ++i)
        {
            const bool on = ((bits >> i) & 1) != 0;
            const int a = size - 11 + i % 3;
            const int b = i / 3;
            grid.set(a, b, on);
            grid.set(b, a, on);
        }
    }
}

// The codewords in two-module columns from the right, up then down, past
// the vertical timing pattern.
void draw_codewords(Grid &grid, const std::vector<std::uint8_t> &codewords)
{
    const int size = grid.size;
    std::size_t bit = 0;
    for (int right = size - 1; right >= 1; right -= 2)
    {
        if (right == 6)
            right = 5;
        const bool upward = ((right + 1) & 2) == 0;
        for (int step = 0; step < size; ++step)
        {
            const int y = upward ? size - 1 - step : step;
            for (int j = 0; j < 2; ++j)
            {
                const int x = right - j;
                if (grid.fixed[grid.at(x, y)] != 0 || bit >= codewords.size() * 8)
                    continue;
                grid.dark[grid.at(x, y)] = (codewords[bit >> 3] >> (7 - (bit & 7))) & 1;
                ++bit;
            }
        }
    }
}

bool masked(int mask, int x, int y)
{
    switch (mask)
    {
    case 0:
        return (x + y) % 2 == 0;
    case 1:
        return y % 2 == 0;
    case 2:
        return x % 3 == 0;
    case 3:
        return (x + y) % 3 == 0;
    case 4:
        return (x / 3 + y / 2) % 2 == 0;
    case 5:
        return x * y % 2 + x * y % 3 == 0;
    case 6:
        return (x * y % 2 + x * y % 3) % 2 == 0;
    default:
        return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

void apply_mask(Grid &grid, int mask)
{
    for (int y = 0; y < grid.size; ++y)
    {
        for (int x = 0; x < grid.size; ++x)
        {
            if (grid.fixed[grid.at(x, y)] == 0 && masked(mask, x, y))
                grid.dark[grid.at(x, y)] ^= 1;
        }
    }
}

// How hard a code is to read (the standard's penalty rules): runs of one
// colour, 2x2 blocks, finder look-alikes, and dark and light out of balance.
// The mask that scores least is the one used.
long penalty(const Grid &grid)
{
    const int size = grid.size;
    long score = 0;
    const auto line = [&](bool rows)
    {
        for (int a = 0; a < size; ++a)
        {
            int run = 0;
            int colour = -1;
            std::uint32_t window = 0; // the last 11 modules, for finder look-alikes
            for (int b = 0; b < size; ++b)
            {
                const int on = rows ? grid.dark[grid.at(b, a)] : grid.dark[grid.at(a, b)];
                if (on == colour)
                    ++run;
                else
                {
                    if (run >= 5)
                        score += 3 + (run - 5);
                    colour = on;
                    run = 1;
                }
                window = ((window << 1) | static_cast<std::uint32_t>(on)) & 0x7ffu;
                if (b >= 10 && (window == 0x5d0u || window == 0x05du)) // 10111010000, 00001011101
                    score += 40;
            }
            if (run >= 5)
                score += 3 + (run - 5);
        }
    };
    line(true);
    line(false);
    int dark = 0;
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const int on = grid.dark[grid.at(x, y)];
            dark += on;
            if (x + 1 < size && y + 1 < size && on == grid.dark[grid.at(x + 1, y)] &&
                on == grid.dark[grid.at(x, y + 1)] && on == grid.dark[grid.at(x + 1, y + 1)])
                score += 3;
        }
    }
    const int total = size * size;
    const int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
    score += 10L * k;
    return score;
}

} // namespace

bool make_qr(std::string_view text, QrCode *code)
{
    *code = QrCode();
    int version = 1;
    const auto bits_needed = static_cast<long>(4 + 8 + 8 * text.size());
    while (version <= kMaxVersion && bits_needed > 8L * data_codewords(version))
        ++version;
    if (version > kMaxVersion)
        return false;

    // Byte mode (0100), the length, the bytes; then up to four zero bits,
    // zeros to the byte, and the two pad codewords taking turns.
    std::vector<std::uint8_t> data;
    std::uint32_t pending = 0;
    int pending_bits = 0;
    const auto put = [&](std::uint32_t value, int count)
    {
        for (int i = count - 1; i >= 0; --i)
        {
            pending = (pending << 1) | ((value >> i) & 1u);
            if (++pending_bits == 8)
            {
                data.push_back(static_cast<std::uint8_t>(pending));
                pending = 0;
                pending_bits = 0;
            }
        }
    };
    put(0x4, 4);
    put(static_cast<std::uint32_t>(text.size()), 8);
    for (const char c : text)
        put(static_cast<unsigned char>(c), 8);
    const long capacity = 8L * data_codewords(version);
    const long used = static_cast<long>(data.size()) * 8 + pending_bits;
    put(0, static_cast<int>(std::min(4L, capacity - used)));
    if (pending_bits > 0)
        put(0, 8 - pending_bits);
    for (std::uint8_t pad = 0xec; static_cast<int>(data.size()) < data_codewords(version);
         pad ^= 0xec ^ 0x11)
        data.push_back(pad);

    Grid grid;
    grid.size = version * 4 + 17;
    grid.dark.assign(static_cast<std::size_t>(grid.size * grid.size), 0);
    grid.fixed.assign(grid.dark.size(), 0);
    draw_fixed(grid, version);
    draw_codewords(grid, interleaved(data, version));

    Grid best;
    long best_score = std::numeric_limits<long>::max();
    for (int mask = 0; mask < 8; ++mask)
    {
        Grid trial = grid;
        apply_mask(trial, mask);
        draw_format(trial, mask);
        const long score = penalty(trial);
        if (score < best_score)
        {
            best_score = score;
            best = std::move(trial);
        }
    }
    code->size = best.size;
    code->modules = std::move(best.dark);
    return true;
}

} // namespace hui
