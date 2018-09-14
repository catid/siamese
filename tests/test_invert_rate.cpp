/*
    Copyright (c) 2016 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of Siamese nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include <iostream>
#include <iomanip>
#include <cassert>
#include <vector>
using namespace std;

#include "GF256Matrix.h"
#include "../SiameseTools.h"
#include "../SiameseCommon.h"
#include "../gf256.h"

#define ENABLE_TEST_INVERT_RATE

static uint8_t int2gray(uint8_t num)
{
    return num ^ (num >> 1);
}

static const int PrimesCount = 240;
static const uint8_t Primes[PrimesCount] = {
        0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11,
        0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
        0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
        0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
        0x56, 0x57, 0x58, 0x59, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
        0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
        0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
        0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9,
        0xba, 0xbb, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb,
        0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb,
        0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed,
        0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfc, 0xfd, 0xfe, 0xff,
};
static const uint8_t ShuffledPrimes[PrimesCount] = {
        0xcc, 0xfd, 0xe0, 0x34, 0xf9, 0xd3, 0x66, 0xc6, 0xce, 0x97, 0x85, 0xa8, 0xbe, 0x63, 0x0d, 0x7e,
        0xc3, 0xea, 0x96, 0x3c, 0x2f, 0x4e, 0x0f, 0xf4, 0x3e, 0x0c, 0xd1, 0xa7, 0xd4, 0xb3, 0x82, 0x61,
        0x9a, 0x29, 0x39, 0x54, 0x71, 0xa4, 0x12, 0xbb, 0xb0, 0xc9, 0xee, 0x57, 0xe8, 0xdc, 0x65, 0xda,
        0x80, 0xf1, 0x67, 0x77, 0xd8, 0x25, 0xd7, 0x08, 0x9c, 0x9f, 0xb8, 0xde, 0x02, 0x3f, 0xe3, 0xa6,
        0xaf, 0x23, 0x7c, 0xd6, 0x7a, 0x5e, 0xcd, 0xd2, 0x55, 0x2e, 0xb1, 0x11, 0x0b, 0x8c, 0x0e, 0x56,
        0x94, 0xf2, 0x8f, 0x9d, 0x16, 0xe9, 0x2a, 0x14, 0x31, 0x4a, 0x19, 0x8b, 0xad, 0x50, 0x04, 0x18,
        0x9e, 0x51, 0x3a, 0x92, 0xf8, 0x81, 0x5d, 0x26, 0x6a, 0xb9, 0x38, 0xff, 0xed, 0x68, 0xf0, 0x59,
        0xcf, 0xca, 0xc0, 0x72, 0xac, 0x99, 0x76, 0x1e, 0xb5, 0x75, 0x6f, 0xd5, 0x4b, 0xd9, 0x1f, 0x8d,
        0x1a, 0xdf, 0x35, 0x2b, 0x60, 0x42, 0x58, 0x5c, 0x0a, 0x15, 0x41, 0x07, 0x28, 0x93, 0x37, 0xba,
        0xcb, 0x7f, 0x90, 0xe4, 0x13, 0x78, 0x6b, 0x24, 0x89, 0xf3, 0xf5, 0xc1, 0x33, 0xa9, 0x05, 0x84,
        0x22, 0x4f, 0xdd, 0x8e, 0xa3, 0x30, 0x3d, 0x4d, 0xe5, 0xa5, 0xe2, 0x62, 0xe1, 0x83, 0xb2, 0x40,
        0x10, 0xeb, 0x64, 0x2c, 0x1b, 0x2d, 0x73, 0x32, 0x36, 0x3b, 0x48, 0x6d, 0x20, 0x27, 0xa2, 0x7d,
        0xae, 0x79, 0x91, 0xc4, 0xef, 0x7b, 0x4c, 0xfc, 0xb6, 0x88, 0xab, 0x49, 0x9b, 0x53, 0xbf, 0x95,
        0x03, 0xc7, 0x5f, 0x43, 0x06, 0x70, 0xc5, 0x98, 0x44, 0x52, 0x21, 0xfe, 0x69, 0x17, 0xc8, 0xf6,
        0xd0, 0xc2, 0x74, 0xdb, 0x45, 0xaa, 0x6e, 0xf7, 0x09, 0x8a, 0xec, 0x87, 0xb4, 0x6c, 0x86, 0xb7,
};

/*
    Given a PRNG, generate a deck of cards in a random order.
    The deck will contain elements with values between 0 and count - 1.
*/

static void ShuffleDeck16(siamese::PCGRandom& prng, uint16_t* GF256_RESTRICT deck, uint32_t count)
{
    deck[0] = 0;

    // If we can unroll 4 times,
    if (count <= 256)
    {
        for (uint32_t ii = 1;;)
        {
            uint32_t jj, rv = prng.Next();

            // 8-bit unroll
            switch (count - ii)
            {
            default:
                jj = (uint8_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 8) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint8_t)(rv >> 24) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                break;

            case 3:
                jj = (uint8_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
            case 2:
                jj = (uint8_t)(rv >> 8) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
            case 1:
                jj = (uint8_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
            case 0:
                return;
            }
        }
    }
    else
    {
        // For each deck entry,
        for (uint32_t ii = 1;;)
        {
            uint32_t jj, rv = prng.Next();

            // 16-bit unroll
            switch (count - ii)
            {
            default:
                jj = (uint16_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                jj = (uint16_t)(rv >> 16) % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
                ++ii;
                break;

            case 1:
                jj = (uint16_t)rv % ii;
                deck[ii] = deck[jj];
                deck[jj] = ii;
            case 0:
                return;
            }
        }
    }
}

static bool GenerateTestMatrix_Random(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    siamese::PCGRandom prng;
    prng.Seed(1000);

    for (int col = col_start; col <= col_end; ++col)
    {
        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);
            *val = 1 + (prng.Next() % 255);
        }
    }

    //matrix.Print();

    return true;
}

static bool GenerateTestMatrix_RandomGF16Matrix(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    for (int col = col_start; col <= col_end; ++col)
    {
        // (Prime table modulus) * (255) == max number of columns we can support
        // We can reduce this to 4 or 8 practically if that helps.
        //uint8_t x = ShuffledPrimes[col / 255];
        uint8_t x = 2; // Using a constant 2 here works fine

        uint8_t basis[4];
        basis[0] = 1;
        basis[1] = x;
        for (int j = 2; j < 4; ++j)
        {
            basis[j] = gf256_mul(basis[j - 1], x);
        }
        // Only calculates 3 GF256 multiplications, and these maybe can be done faster?

        // Stretch the multiplied values, requiring ~12 XORs per column if they're all used
        uint8_t table[16];
        table[0] = basis[0] ^ basis[1];
        table[1] = basis[1];
        table[2] = basis[0];
        table[3] = basis[2];
        table[4] = basis[1] ^ basis[3]; // Repeated an element
        table[5] = basis[0] ^ basis[2];
        table[6] = basis[1] ^ basis[2];
        table[7] = table[0] ^ basis[2];
        table[8] = basis[3];
        table[9] = basis[0] ^ basis[3];
        table[10] = basis[1] ^ basis[3];
        table[11] = basis[2] ^ basis[3];
        table[12] = table[5] ^ basis[3];
        table[13] = table[0] ^ basis[3];
        table[14] = table[6] ^ basis[3];
        table[15] = table[7] ^ basis[3];

        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            siamese::PCGRandom prng;
            prng.Seed(row, col);

            *val = 0;

            // Punctured code?  Nope does very poorly.
            //if ((prng.Next() % 2) != 0) continue;

            uint8_t g = (uint8_t)prng.Next();

            // One XOR per column per row
            *val ^= table[g % 16];
        }
    }

    // Total cost: MULS = 3, XORS = 12*N + N^2

    //matrix.Print();

    return true;
}

static bool GenerateTestMatrix_GrayCodedColumns(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    /*
        This code requires a small table for each column because there's no modulus on the column count,
        but it is interesting in that it doesn't require any LDPC to do somewhat okay across the board.

        It starts failing above about 1000 columns for some reason.  With LDPC added it goes much further.

        It currently does 4 GFmuls per column but maybe we can do with 1-3 instead with table stretching?
    */
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    for (int col = col_start; col <= col_end; ++col)
    {
        // (Prime table modulus) * (255) == max number of columns we can support
        // We can reduce this to 4 or 8 practically if that helps.
        uint8_t x = ShuffledPrimes[col % PrimesCount];
        //uint8_t x = 2;

        uint8_t basis[4];
        basis[0] = 1;
        basis[1] = x;
        for (int j = 2; j < 4; ++j)
        {
            basis[j] = gf256_mul(basis[j - 1], x);
        }

        uint8_t table[16];
        table[0] = basis[0];
        table[1] = basis[1];
        table[2] = basis[2];
        table[3] = basis[3];
        table[4] = basis[0] ^ basis[1];
        table[5] = basis[1] ^ basis[2];
        table[6] = basis[2] ^ basis[3];
        table[7] = basis[0] ^ basis[3];

        table[8] = basis[1] ^ basis[3]; // Repeated an element
        table[9] = basis[0] ^ basis[1] ^ basis[2];
        table[10] = basis[1] ^ basis[3];
        table[11] = basis[2] ^ basis[3];
        table[12] = basis[0] ^ basis[2] ^ basis[3];
        table[13] = basis[0] ^ basis[1] ^ basis[3];
        table[14] = basis[1] ^ basis[2] ^ basis[3];
        table[15] = basis[0] ^ basis[1] ^ basis[2] ^ basis[3];

        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            // If I multiply column by 211, then it behaves well when rows are lost at random.
            // But it behaves poorly when columns are lost at random.
            // My theory is that since the rows are simple linear combinations, they're less useful.
            int gray_index = 1 + (row + col * 211) % 255;

            // If I multiply row by 211, then it behaves well when columns are lost at random.
            // But it behaves poorly when rows are lost at random and the first K columns are lost:
            //int gray_index = 1 + (row * 211 + col) % 255;

            // This one does poorly for randomly chosen columns but does well for burst errors when
            // all rows are received in order.
            // It also does poorly for randomly chosen rows.
            //int gray_index = 1 + (row + col) % 255;

            // This is the only one that seems to do well in all cases, which is basically just
            // a random combination of the table elements.
            //int gray_index = 1 + (row * 31 + col * 211) % 255;

            // Using a Gray code seems to make a huge difference here, probably because it makes
            // it even more random...
            uint8_t g = int2gray((uint8_t)gray_index);
            //uint8_t g = (uint8_t)gray_index;

            // So it turns out this evolves into a GF(16) random matrix!
            //PCGRandom prng;
            //prng.Seed(row, col);
            //uint8_t g = (uint8_t)prng.Next();

            // For burst losses:
            // I found that reducing the table size
            // to a smaller number of elements is still pretty good:
            // 3: Overall overhead score = 3636 (lower is better)
            // 4: Overall overhead score = 301 (lower is better)
            // 5: Overall overhead score = 261 (lower is better)
            // 8: Overall overhead score = 160 (lower is better)
            // Selecting table size = 4 and
            // Reducing the number of primes to 4:
            // Overall overhead score = 307 (lower is better)

            // It looks like as the number of rows for a given column
            // increases, it starts paying off to increase the table size.
            // But smaller tables for random column losses experience
            // bad recovery rates 1+ for larger numbers of symbols.
            // One option would be to increase the table size based
            // on the number of symbols encoded.

#if 1
            if (g <= 0)
            {
                SIAMESE_DEBUG_BREAK();
            }

            *val = 0;

            uint8_t mask = 1;
            for (int k = 0; k < 6; ++k)
            {
                if (0 != (g & mask))
                {
                    //*val ^= table[(k + (col/64)) % 8];
                    *val ^= table[k];
                }

                mask <<= 1;
            }
#else
            *val ^= table[g % 16];
#endif
        }
    }

#if 0
    for (int col = col_start; col <= col_end; ++col)
    {
        for (int row = row_start + 1; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);
            uint8_t* prev = matrix.Get(row - row_start - 1, col - col_start);
            *val ^= *prev;
        }
    }
#endif

#if 1
    for (int row = row_start; row <= row_end; ++row)
    {
        siamese::PCGRandom prng;
        prng.Seed(row + 1000);

        int randomCols = cols / 8;

        for (int colIndex = 0; colIndex < randomCols; ++colIndex)
        {
            int col = col_start + (prng.Next() % cols);
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            *val ^= 1;
        }
    }
#endif

    //matrix.Print();

    return true;
}

static bool GenerateTestMatrix_LinearCombos(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    for (int col = col_start; col <= col_end; ++col)
    {
        uint8_t x = ShuffledPrimes[col % PrimesCount];
        //uint8_t x = Primes[col % PrimesCount];
        //uint8_t x = 1 + (col % 255);

        uint8_t table[8];
        table[0] = 1;
        table[1] = x;
        for (int j = 2; j < 8; ++j)
        {
            table[j] = gf256_mul(table[j - 1], x);
        }

        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            int gray_index = 1 + (row) % 255;
            uint8_t g = int2gray((uint8_t)gray_index);

            if (g == 0)
            {
                SIAMESE_DEBUG_BREAK();
            }

            uint8_t mask = 1;
            for (int k = 0; k < 8; ++k)
            {
                if (0 != (g & mask))
                {
                    *val ^= table[k];
                }

                mask <<= 1;
            }
        }
    }

    //matrix.Print();

    return true;
}

static bool GenerateTestMatrix_SRLC(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    uint16_t* sort = new uint16_t[cols];

    for (int row = row_start; row <= row_end; ++row)
    {
        siamese::PCGRandom prng;
        prng.Seed(row + 1000);

        for (int i = 0; i < cols; ++i)
            sort[i] = (uint16_t)i;
        ShuffleDeck16(prng, sort, cols);

        for (int col = col_start; col <= col_end; ++col)
        {
            uint8_t z = 1;

            uint8_t* val = matrix.Get(row - row_start, col - col_start);
            *val = z;
        }

        for (int col = 1; col < cols && col <= cols / 4; ++col)
        {
            uint8_t z = 0;

            uint8_t* val = matrix.Get(row - row_start, sort[col]);
            *val = z;
        }

        for (int col = 0; col < cols; col += 4)
        {
            uint8_t z = (uint8_t)(prng.Next() % 254 + 2);

            uint8_t* val = matrix.Get(row - row_start, col);
            *val = z;
        }
    }

    delete[] sort;

    //matrix.Print();

    return true;
}

static inline int only_one_bit_set_to_one(uint32_t b)
{
    return b && !(b & (b - 1));
}

// Get the next bit to flip to produce the Gray code at the provided index
// going from the Gray code at (index - 1).
// Precondition: index > 0 and index < (kGrayPeriod-1)
static inline unsigned getBitFlipForGrayCode(unsigned index)
{
    if (index & 1)
        return 0;

    if (index & 15)
        return (0x6764 >> (index & 14)) & 3;

    return ((0x12131210 >> (index >> 3)) & 3) + 4;
}

static bool GenerateTestMatrix_LinearCombos_WithPerturbations(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    for (int col = col_start; col <= col_end; ++col)
    {
        uint8_t x = ShuffledPrimes[col % PrimesCount];

        uint8_t table[8];
#if 1
        table[0] = 1;
        table[1] = x;
        for (int j = 2; j < 8; ++j)
        {
            table[j] = gf256_mul(table[j - 1], x);
        }
#else
        table[0] = x;
        for (int j = 1; j < 8; ++j)
        {
            table[j] = gf256_mul(table[j - 1], x);
        }
#endif

        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            /*
                int gray_index = 1 + ((row + (col % 8) * 8) % 255);
                Overall overhead score = 8492 (lower is better)

                int gray_index = 1 + ((row + (col % 2)) % 255);
                Overall overhead score = 2134 (lower is better)

                int gray_index = 1 + ((row + (col % 4)) % 255);
                Overall overhead score = 534 (lower is better)

                int gray_index = 1 + ((row + (col % 4) * 63) % 255);
                int gray_index = 1 + ((row + (col % 4) * 31) % 255);
                Overall overhead score = 465 (lower is better)

                int gray_index = 1 + ((row + (col % 8) * 3) % 255);
                Overall overhead score = 388 (lower is better)

                int gray_index = 1 + ((row + (col % 8) * 7) % 255);
                Overall overhead score = 372 (lower is better)

                int gray_index = 1 + ((row + (col % 8)) % 255);
                Overall overhead score = 348 (lower is better)

                int gray_index = 1 + ((row + (col % 8) * 31) % 255);
                Overall overhead score = 350 (lower is better)

                int gray_index = 1 + ((row + (col % 8) * 63) % 255);
                Overall overhead score = 276 (lower is better)

                int gray_index = 1 + ((row + col) % 255);
                Overall overhead score = 267 (lower is better)

                For reference, random codes get:
                Overall overhead score = 198 (lower is better)
            */
            //int gray_index = 1 + ((row + (col % 2)) / 2 + (col % 4) * 63) % 255;
            //int gray_index = 1 + ((row + (col % 8)) % 255);
            int gray_index = 1 + ((row + (col % 8) * 63) % 255);
            //int gray_index = 1 + ((row + col) % 255);
            uint8_t g = int2gray((uint8_t)gray_index);
            //int gray_index = 1 + (row * 2 + (col % 4) * 77) % 255;
            //uint8_t g = (uint8_t)gray_index;

            *val = 0;

#if 0
            unsigned bitFlip = getBitFlipForGrayCode(gray_index);
            assert(bitFlip >= 0 && bitFlip <= 7);
            *val ^= table[bitFlip];
#else
            assert(g != 0);

            uint8_t mask = 1;
            // truncating the table at 6 gets similar performance as a table of 8 values
            for (int k = 0; k < 6; ++k)
            {
                if (0 != (g & mask))
                {
                    *val ^= table[k];
                }

                mask <<= 1;
            }
#endif
        }
    }

#if 0
    for (int row = row_start; row <= row_end; ++row)
    {
        for (int col = col_start; col <= col_end; ++col)
        {
            if (((row + col) % 8) == 0)
            {
                uint8_t* val = matrix.Get(row - row_start, col - col_start);

                *val ^= 1;
            }
        }
    }
#endif

#if 0
    for (int row = row_start; row <= row_end; ++row)
    {
        for (int col = col_start; col <= col_end; ++col)
        {
            if (((row * 51 + col) % 4) == 0)
            {
                uint8_t* val = matrix.Get(row - row_start, col - col_start);

                *val ^= 1;
            }
        }
    }
#endif

#if 0
    for (int row = row_start; row <= row_end; ++row)
    {
        for (int col = col_start; col <= col_end; ++col)
        {
            if (((row * 37 + col) % 2) == 0)
            {
                uint8_t* val = matrix.Get(row - row_start, col - col_start);

                *val ^= 1;
            }
        }
    }
#endif

#if 1
    for (int row = row_start; row <= row_end; ++row)
    {
        siamese::PCGRandom prng;
        prng.Seed(row + 1000);

        int randomCols = cols / 8;

        for (int colIndex = 0; colIndex < randomCols; ++colIndex)
        {
            int col = col_start + (prng.Next() % cols);
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            *val ^= 1;
        }
    }
#endif

    /*
        These tweaks reveal a lot of the sparse matrix structure that was produced above,
        and will be useful during decoding to reduce the amount of work to do.
    */
#if 0
    for (int col = col_start; col <= col_end; ++col)
    {
        for (int row = row_end; row > row_start; --row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);
            uint8_t* prev = matrix.Get(row - row_start - 1, col - col_start);
            *val ^= *prev;
        }
    }
#endif
#if 0
    for (int col = col_start; col <= col_end; ++col)
    {
        for (int row = row_end; row > row_start + 8; row -= 8)
        {
            for (int i = 0; i < 8; ++i)
            {
                uint8_t* val = matrix.Get(row - row_start - i, col - col_start);
                uint8_t* prev = matrix.Get(row - row_start - 8 - i, col - col_start);
                *val ^= *prev;
            }
        }
    }
#endif

    //matrix.Print();

    return true;
}

static bool GenerateTestMatrix_LinearCombosSmallerTable_WithPerturbations(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    for (int col = col_start; col <= col_end; ++col)
    {
        uint8_t x = ShuffledPrimes[col % PrimesCount];

        uint8_t basis[4];
        basis[0] = 1;
        basis[1] = x;
        for (int j = 2; j < 4; ++j)
        {
            basis[j] = gf256_mul(basis[j - 1], x);
        }
        // 3 GF muls

        uint8_t table[8];
#if 0
        table[0] = basis[0]; // original
        table[1] = basis[1]; // 1 GFmul
        table[2] = basis[2]; // 1 GFmul
        table[3] = basis[3]; // 1 GFmul
        table[4] = basis[0] ^ basis[1]; // 1 xor
        table[5] = basis[1] ^ basis[2]; // 1 xor
        table[6] = basis[2] ^ basis[3]; // 1 xor
        table[7] = basis[0] ^ basis[3]; // 1 xor
#elif 1
        table[0] = basis[0]; // original
        table[1] = basis[1]; // 1 GFmul
        table[2] = basis[2]; // 1 GFmul
        table[3] = basis[0] ^ basis[1]; // 1 xor
        table[4] = basis[1] ^ basis[2]; // 1 xor
        table[5] = basis[0] ^ basis[2]; // 1 xor
        table[6] = basis[0] ^ basis[1] ^ basis[2]; // 1 xor
        table[7] = table[4]; // repeated
#else
        table[0] = basis[0]; // original
        table[1] = basis[1]; // 1 GFmul
        table[2] = basis[0] ^ basis[1];
        table[3] = basis[1];
        table[4] = basis[0] ^ basis[1];
        table[5] = basis[0];
        table[6] = basis[0] ^ basis[1];
        table[7] = table[1];
#endif
        // stretched across more elements
        // note the xors are computed across many columns in one operation by
        // combining the basis vector product sums, so the only per-element
        // cost is the two GFmuls.

        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            int gray_index = 1 + ((row + (col % 8) * 63) % 255);
            uint8_t g = int2gray((uint8_t)gray_index);

            *val = 0;

#if 0
            unsigned bitFlip = getBitFlipForGrayCode(gray_index);
            assert(bitFlip >= 0 && bitFlip <= 7);
            *val ^= table[bitFlip];
#elif 0
            // This does not do as well as the Gray code anyway
            *val ^= table[(row + (col % 4) * 63) % 16];
#else
            assert(g != 0);

            uint8_t mask = 1;
            for (int k = 0; k < 8; ++k)
            {
                if (0 != (g & mask))
                {
                    *val ^= table[k];
                }

                mask <<= 1;
            }
#endif
        }
    }

#if 0
    for (int row = row_start; row <= row_end; ++row)
    {
        for (int col = col_start; col <= col_end; ++col)
        {
            if (((row + col) % 8) == 0)
            {
                uint8_t* val = matrix.Get(row - row_start, col - col_start);

                *val ^= 1;
            }
        }
    }
#endif

#if 0
    for (int row = row_start; row <= row_end; ++row)
    {
        for (int col = col_start; col <= col_end; ++col)
        {
            if (((row * 51 + col) % 4) == 0)
            {
                uint8_t* val = matrix.Get(row - row_start, col - col_start);

                *val ^= 1;
            }
        }
    }
#endif

#if 0
    for (int row = row_start; row <= row_end; ++row)
    {
        for (int col = col_start; col <= col_end; ++col)
        {
            if (((row * 37 + col) % 2) == 0)
            {
                uint8_t* val = matrix.Get(row - row_start, col - col_start);

                *val ^= 1;
            }
        }
    }
#endif

#if 1
    for (int row = row_start; row <= row_end; ++row)
    {
        siamese::PCGRandom prng;
        prng.Seed(row + 1000);

        int randomCols = (cols + 7) / 8;

        for (int colIndex = 0; colIndex < randomCols; ++colIndex)
        {
            int col = col_start + (prng.Next() % cols);
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            *val ^= 1;
        }
    }
#endif

    /*
    These tweaks reveal a lot of the sparse matrix structure that was produced above,
    and will be useful during decoding to reduce the amount of work to do.
    */
#if 0
    for (int col = col_start; col <= col_end; ++col)
    {
        for (int row = row_end; row > row_start; --row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);
            uint8_t* prev = matrix.Get(row - row_start - 1, col - col_start);
            *val ^= *prev;
        }
    }
#endif
#if 0
    for (int col = col_start; col <= col_end; ++col)
    {
        for (int row = row_end; row > row_start + 8; row -= 8)
        {
            for (int i = 0; i < 8; ++i)
            {
                uint8_t* val = matrix.Get(row - row_start - i, col - col_start);
                uint8_t* prev = matrix.Get(row - row_start - 8 - i, col - col_start);
                *val ^= *prev;
            }
        }
    }
#endif

    //matrix.Print();

    return true;
}

static bool GenerateTestMatrix_RowColMultiplies(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    for (int col = col_start; col <= col_end; ++col)
    {
        uint8_t x = ShuffledPrimes[col % PrimesCount];

        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            uint8_t y = Primes[(row + (col % 8) * 63) % PrimesCount];

            *val = gf256_mul(x, y);
        }
    }

    //matrix.Print();

    return true;
}

static const int kSiameseLaneCount = 8;

static uint8_t SiameseGetRX(unsigned row)
{
#if 0
    uint8_t RX = ShuffledPrimes[row % PrimesCount];
#elif 0
    uint8_t RX = 3 + (uint8_t)((row * 211) % 253);
#else
    uint8_t RX = 1 + (uint8_t)((row + 1) % 255);
#endif
    return RX;
}

static uint8_t SiameseGetCX(unsigned col)
{
#if 0
    uint8_t CX = ShuffledPrimes[col % PrimesCount];
#elif 0
    uint8_t CX = 3 + (Int32Hash(col * 17) % 253);
#elif 0
    uint8_t CX = 1 + (uint8_t)((col * 199) % 255);
#elif 1
    uint8_t CX = 3 + (uint8_t)((col * 199) % 253);
#else
    uint8_t CX = 2 + (uint8_t)((col * 211) % 253);
#endif
    return CX;
}

// Calculate 4-bit operation code for the given row and lane
inline unsigned Test_GetRowOpcode(unsigned lane, unsigned row)
{
#if 1
    uint32_t opcode = siamese::Int32Hash(lane + row * kSiameseLaneCount) & 15;
#else
    static const int kGrayPeriod = 255;
    const int kLaneMult = 63;
    int gray_index = 1 + ((row + lane * kLaneMult) % kGrayPeriod);

    uint8_t g = int2gray((uint8_t)gray_index);

    static const uint8_t s_table[8] = {
        1, 2, 4, 3, 6, 5, 7, 8
    };

    uint8_t opcode = 0;

    // Calculate it directly:
    uint8_t mask = 1;
    for (int k = 0; k < 8; ++k)
    {
        if (0 != (g & mask))
            opcode ^= s_table[k];
        mask <<= 1;
    }
#endif
    return (opcode == 0) ? 8 : (unsigned)opcode;
}

// Calculate 1-bit xor swap for the given column
inline unsigned GetColumnXorSwap(unsigned column)
{
    return (siamese::Int32Hash(column) >> 2) & 1;
}

static bool GenerateTestMatrix_Siamese(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    for (int col = col_start; col <= col_end; ++col)
    {
        uint8_t CX = SiameseGetCX(col);
        SIAMESE_DEBUG_ASSERT(CX != 0);

        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            uint8_t RX = SiameseGetRX(row);
            SIAMESE_DEBUG_ASSERT(RX != 0);

            const uint8_t RCX = gf256_mul(RX, CX);
            SIAMESE_DEBUG_ASSERT(RCX != 0);
            // We keep a running sum S0 = Data[0] + Data[8] + Data[16] + ...
            // We keep a running sum S1 = Data[0]*CX[0] + Data[8]*CX[8] + Data[16]*CX[16] + ...
            // So RCX is = S1 * RCX, which can be calculated once per output row, rather than
            // once per column as we do here in this code.  Similarly, RX = S0 * RX.
            // RX and RCX are very cheap compared to S1 and S0 since we have only a few output
            // rows and lots of input data.

            *val = 0;

            const unsigned lane = (col % kSiameseLaneCount);
            uint32_t opcode = siamese::Int32Hash(lane + row * (kSiameseLaneCount * 2)) & 15;
            if (opcode == 0)
                opcode = 8;

            if (opcode & 1)
                *val ^= 1;
            if (opcode & 2)
                *val ^= CX;
            if (opcode & 4)
                *val ^= RX;
            if (opcode & 8)
                *val ^= RCX;
        }
    }

#if 1
    for (int row = row_start; row <= row_end; ++row)
    {
        uint8_t RX = SiameseGetRX(row);

        siamese::PCGRandom prng;
        prng.Seed(row, col_start);

        const unsigned Count = col_end - col_start + 1;
        const unsigned ColumnStart = col_start;
        const unsigned bundleCount = (Count + 15) / 16;
        for (unsigned i = 0; i < bundleCount; ++i)
        {
            {
                const unsigned targetColumn = prng.Next() % Count;
                uint8_t* val = matrix.Get(row - row_start, targetColumn);
                *val ^= 1;
            }
            {
                const unsigned targetColumn = prng.Next() % Count;
                uint8_t* val = matrix.Get(row - row_start, targetColumn);
                *val ^= RX;
            }
        }
    }
#endif

    //matrix.Print();

    return true;
}

static bool GenerateTestMatrix_Siamese_MoreRX(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    for (int col = col_start; col <= col_end; ++col)
    {
        uint8_t CX = SiameseGetCX(col);
        SIAMESE_DEBUG_ASSERT(CX != 0);

        for (int row = row_start; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            uint8_t RX = SiameseGetRX(row);
            SIAMESE_DEBUG_ASSERT(RX != 0);
            uint8_t RX2 = gf256_sqr(RX);
            SIAMESE_DEBUG_ASSERT(RX2 != 0);

            const uint8_t RCX = gf256_mul(RX, CX);
            SIAMESE_DEBUG_ASSERT(RCX != 0);
            const uint8_t RCX2 = gf256_mul(RX2, CX);
            SIAMESE_DEBUG_ASSERT(RCX2 != 0);

            *val = 0;

            const unsigned lane = (col % kSiameseLaneCount);
            uint32_t opcode = siamese::Int32Hash(lane + row * kSiameseLaneCount) & 31;

            if (opcode == 0)
                opcode = 16;

            if (opcode & 1)
                *val ^= 1;
            if (opcode & 2)
                *val ^= CX;

            if (opcode & 4)
                *val ^= RX;
            if (opcode & 8)
                *val ^= RCX;
            if (opcode & 16)
                *val ^= RCX2;
        }
    }

#if 1
    for (int row = row_start; row <= row_end; ++row)
    {
        uint8_t RX = SiameseGetRX(row);

        siamese::PCGRandom prng;
        prng.Seed(row, col_start);

        const unsigned Count = col_end - col_start + 1;
        const unsigned ColumnStart = col_start;
        const unsigned bundleCount = (Count + 15) / 16;
        for (unsigned i = 0; i < bundleCount; ++i)
        {
            {
                const unsigned targetColumn = prng.Next() % Count;
                uint8_t* val = matrix.Get(row - row_start, targetColumn);
                *val ^= 1;
            }
            {
                const unsigned targetColumn = prng.Next() % Count;
                uint8_t* val = matrix.Get(row - row_start, targetColumn);
                *val ^= RX;
            }
        }
    }
#endif

    //matrix.Print();

    return true;
}

static bool GenerateTestMatrix_Siamese_MoreCX(GF256Matrix& matrix, int row_start, int row_end, int col_start, int col_end)
{
    const int rows = row_end - row_start + 1;
    const int cols = col_end - col_start + 1;

    if (!matrix.Initialize(rows, cols))
        return false;

    unsigned rowOffset = 0;

#ifdef SIAMESE_ENABLE_CAUCHY
    // Set first row to all ones
    for (int col = col_start; col <= col_end; ++col)
    {
        uint8_t* val = matrix.Get(0, col - col_start);
        *val = 1;
    }

    if (cols <= 64)
    {
        for (int row = row_start + 1; row <= row_end; ++row)
        {
            for (int col = col_start; col <= col_end; ++col)
            {
                unsigned matrixRow = row - row_start;
                unsigned matrixCol = col - col_start;

                uint8_t* val = matrix.Get(matrixRow, matrixCol);
                if (matrixRow >= siamese::kCauchyMaxRows || matrixCol >= siamese::kCauchyMaxColumns)
                {
                    *val = 0;
                }
                else
                {
                    *val = siamese::CauchyElement(matrixRow, matrixCol);
                }
            }
        }

        rowOffset = 192;
    }
#endif // SIAMESE_ENABLE_CAUCHY

    for (int col = col_start; col <= col_end; ++col)
    {
        uint8_t CX = SiameseGetCX(col);
        SIAMESE_DEBUG_ASSERT(CX != 0);
        uint8_t CX2 = gf256_sqr(CX);
        SIAMESE_DEBUG_ASSERT(CX2 != 0);

        for (int row = row_start + rowOffset; row <= row_end; ++row)
        {
            uint8_t* val = matrix.Get(row - row_start, col - col_start);

            uint8_t RX = SiameseGetRX(row);
            SIAMESE_DEBUG_ASSERT(RX != 0);

            *val = 0;

            static const unsigned kLaneCount = 8;
            const unsigned lane = (col % kLaneCount);
            uint32_t opcode = siamese::Int32Hash(lane + (row + 3) * kLaneCount) & 63;

            if (opcode == 0)
                opcode = 16;

            if (opcode & 1)
                *val ^= 1;
            if (opcode & 2)
                *val ^= CX;
            if (opcode & 4)
                *val ^= CX2;

            if (opcode & 8)
                *val ^= gf256_mul(RX, 1);
            if (opcode & 16)
                *val ^= gf256_mul(RX, CX);
            if (opcode & 32)
                *val ^= gf256_mul(RX, CX2);
        }
    }

    static const int kPairRate = 16;
    for (int row = row_start + rowOffset; row <= row_end; ++row)
    {
        uint8_t RX = SiameseGetRX(row);

        siamese::PCGRandom prng;
        prng.Seed(row, col_start);

        const unsigned Count = col_end - col_start + 1;
        const unsigned ColumnStart = col_start;
        const unsigned bundleCount = (Count + kPairRate - 1) / kPairRate;
        for (unsigned i = 0; i < bundleCount; ++i)
        {
            {
                const unsigned targetColumn = prng.Next() % Count;
                uint8_t* val = matrix.Get(row - row_start, targetColumn);
                *val ^= 1;
            }
            {
                const unsigned targetColumn = prng.Next() % Count;
                uint8_t* val = matrix.Get(row - row_start, targetColumn);
                *val ^= RX;
            }
        }
    }

    //matrix.Print();

    return true;
}

static void SelectRandomSubmatrix(int seed, GF256Matrix& matrix, GF256Matrix& submatrix, int rows, int cols, bool printCols = false)
{
    siamese::PCGRandom prng;
    prng.Seed(seed + 1000);

    submatrix.Initialize(rows, cols);

    std::vector<uint16_t> selectedRows(matrix.GetRows());
    std::vector<uint16_t> selectedCols(matrix.GetCols());
#if 0
    // Use random rows for recovery
    ShuffleDeck16(prng, &selectedRows[0], matrix.GetRows());
#else
    unsigned rowOffset = prng.Next();
    for (int i = 0; i < matrix.GetRows(); ++i)
    {
        selectedRows[i] = (uint16_t)((i + rowOffset) % matrix.GetRows());
    }
#endif
#if 1
    // Random column losses
    ShuffleDeck16(prng, &selectedCols[0], matrix.GetCols());
#elif 0
    unsigned offset = prng.Next();
    // Burst column losses
    for (int i = 0; i < matrix.GetCols(); ++i)
        selectedCols[i] = (uint16_t)((i + offset) % matrix.GetCols());
#else
    for (int i = 0; i < matrix.GetCols(); ++i)
        selectedCols[i] = (uint16_t)i;
#endif
    if (printCols)
    {
        cout << "Columns: ";
        for (int j = 0; j < cols; ++j)
        {
            cout << selectedCols[j] << " (L=" << (selectedCols[j] % 8) << ", CX=" << (int)SiameseGetCX(selectedCols[j]) << "), ";
        }
        cout << endl;
#if 1
        cout << "Rows: ";
        for (int j = 0; j < rows; ++j)
        {
            cout << selectedRows[j] << "(RX=" << (int)SiameseGetRX(selectedRows[j]) << ") ";
        }
        cout << endl;
#endif
    }

    for (int i = 0; i < rows; ++i)
    {
        for (int j = 0; j < cols; ++j)
        {
            *submatrix.Get(i, j) = *matrix.Get(selectedRows[i], selectedCols[j]);
        }
    }

    //submatrix.Print();
}

static void TestMatrixInvertibilityRate()
{
    GF256Matrix m;

    int NValues[] = {
        2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 30, 40, 50, 64, 100, 400, 500, 1000, 2000, 4000, 8000, 0
        //1000, 0
    };

    int OverallOverhead = 0;
    float HighestOverheadRate = 0.f;

    for (int NIndex = 0; NValues[NIndex] != 0; ++NIndex)
    {
        int N = NValues[NIndex];
        static const int K = 255;

        //GenerateTestMatrix_Random(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_SRLC(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_RandomGF16Matrix(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_GrayCodedColumns(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_RowColMultiplies(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_LinearCombos(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_LinearCombos_WithPerturbations(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_LinearCombosSmallerTable_WithPerturbations(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_Siamese(m, 0, K - 1, 0, N - 1);
        //GenerateTestMatrix_Siamese_MoreRX(m, 0, K - 1, 0, N - 1);
        GenerateTestMatrix_Siamese_MoreCX(m, 0, K - 1, 0, N - 1);

        // These codes all perform pretty well when the number of losses is higher,
        // so the simulation can be made a lot faster by only running the hard cases
        // where the number of losses is small.

        //for (int losses = 32; losses <= K && losses <= N; losses = (losses <= 64) ? (losses + 1) : (losses * 3) / 2)
        for (int losses = 2; losses <= K && losses <= N && losses <= 21; losses = (losses <= 64) ? (losses + 1) : (losses * 3) / 2)
        //for (int losses = 2; losses <= K && losses <= N; losses = (losses <= 64) ? (losses + 1) : (losses * 3) / 2)
        {
            int success = 0;
            int fail = 0;
            int overhead = 0;
            int resultsOverTwo = 0;

            static const int kTrials = 200;

            for (int seed = 0; seed < kTrials; ++seed)
            {
                GF256Matrix submatrix;
                SelectRandomSubmatrix(seed, m, submatrix, K, losses);

                int solveResult = submatrix.Solve();
                if (solveResult < 0)
                {
                    ++fail;
                    overhead += 255;
                    //submatrix.Print();
                }
                else
                {
                    ++success;
                    overhead += solveResult;
                    if (solveResult > 2)
                    {
                        ++resultsOverTwo;
                    }
#if 0
                    if (solveResult > 0)
                    {
                        cout << solveResult << endl;
                        cout << "Matrix of calculated GF256 multiplications:" << endl;
                        submatrix.Print(losses + 4);
                        SelectRandomSubmatrix(seed, m, submatrix, K, losses, true);
                        submatrix.Print(losses + 4);
                    }
#endif
                }
            }

            if (fail > 0)
            {
                float rate = fail / (float)(success + fail);
                cout << "COMPLETE FAILURE: For N=" << N << " and " << losses << " losses: Recovery failed " << fail << " / " << (success + fail) << " = " << rate * 100.f << "% with average overhead = " << (overhead / (float)success) << " extra recovery packets" << endl;
            }
            else
            {
                float overheadRate = (overhead / (float)success);
                cout << "For N=" << N << " and " << losses << " losses: Average overhead = " << overheadRate << " extra recovery packets.  Over two: " << resultsOverTwo << endl;
                if (HighestOverheadRate < overheadRate)
                    HighestOverheadRate = overheadRate;
            }
            OverallOverhead += overhead;
        }
    }

    cout << "Overall overhead score = " << OverallOverhead << " (lower is better)" << endl;
    cout << "Highest overhead rate = " << HighestOverheadRate << " (lower is better)" << endl;
}


#ifdef ENABLE_TEST_INVERT_RATE

int main()
{
    if (0 != gf256_init())
    {
        cout << "Failed to initialize gf256" << endl;
        return -1;
    }

    TestMatrixInvertibilityRate();

    int x;
    cin >> x;

    return 0;
}

#endif // ENABLE_TEST_INVERT_RATE
