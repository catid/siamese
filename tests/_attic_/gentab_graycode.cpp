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
#include <stdint.h>
using namespace std;

#ifdef _WIN32
    #include <Windows.h>
    #include <intrin.h>
#endif

#include "../SiameseTools.h"

#define ENABLE_GENTAB_GRAYCODE

/*
    I did some exploration into different kinds of Gray codes to see if there was a better choice.
    The canonical Reflected Binary Code seems optimal for my use case, so I decided to implement that.
*/

static inline int only_one_bit_set_to_one(uint32_t b)
{
   return b && !(b & (b - 1));
}

static inline bool hamming1(uint32_t a, uint32_t b)
{
   return only_one_bit_set_to_one(a ^ b) != 0;
}

static const int kBits = 5;
static const int kTableSize = 1 << kBits;
static uint8_t Table[kTableSize] = {};

static uint8_t AccumulatedPopCount[kTableSize];
static uint8_t BackAccumulatedPopCount[kTableSize];

static bool bestPopCount()
{
    uint8_t popCount[kTableSize];
    uint8_t acc = 0;
    for (int i = 0; i < kTableSize; ++i)
    {
        acc |= Table[i];
        int pop = __popcnt(acc);
        if (pop > AccumulatedPopCount[i])
            return false;
        popCount[i] = pop;
    }
    for (int i = 0; i < kTableSize; ++i)
    {
        AccumulatedPopCount[i] = popCount[i];
    }
    return true;
}

static bool bestBackPopCount()
{
    uint8_t popCount[kTableSize];
    uint8_t acc = 0;
    for (int i = kTableSize - 1; i >= 0; --i)
    {
        acc |= Table[i];
        int pop = __popcnt(acc);
        if (pop > BackAccumulatedPopCount[i])
            return false;
        popCount[i] = pop;
    }
    for (int i = 0; i < kTableSize; ++i)
    {
        BackAccumulatedPopCount[i] = popCount[i];
    }
    return true;
}

static void analyzeTable()
{
    // Make sure just one bit is set at the end so we can concatenate the codes
    if (!only_one_bit_set_to_one(Table[kTableSize - 1]))
    {
        return;
    }

    if (!bestPopCount() || !bestBackPopCount())
    {
        return;
    }

    uint8_t flipIndices[kTableSize - 1] = {};
    for (int i = 1; i < kTableSize; ++i)
    {
        uint8_t delta = Table[i] ^ Table[i - 1];
        if (!only_one_bit_set_to_one(delta))
        {
            cout << "FAILURE" << endl;
        }
        unsigned long index = 0;
        if (1 != _BitScanForward(&index, delta))
        {
            cout << "FAILURE" << endl;
        }
        flipIndices[i - 1] = (uint8_t)index;
    }

    if (flipIndices[4] != 1)
        return;
    if (flipIndices[3] != 2)
        return;

    cout << "static const uint8_t GrayCode[kTableSize]            = { ";
    for (int i = 0; i < kTableSize; ++i)
    {
        cout << (int)Table[i];
        if (i != kTableSize - 1)
            cout << ", ";
    }
    cout << " };" << endl;
#if 1
    cout << "static const uint8_t AccumulatedPopCount[kTableSize] = { ";
    for (int i = 0; i < kTableSize; ++i)
    {
        cout << (int)AccumulatedPopCount[i];

        if (i != kTableSize - 1)
            cout << ", ";
    }
    cout << " };" << endl;
#endif
#if 1
    cout << "static const uint8_t FlipIndices[kTableSize]         = { ";
    for (int i = 1; i < kTableSize; ++i)
    {
        cout << (int)flipIndices[i - 1];

        if (i != kTableSize - 1)
            cout << ", ";
    }
    cout << " };" << endl;
    for (int i = 0; i < kBits; ++i)
    {
        cout << "Flip " << i << " at: ";
        for (int j = 0; j < kTableSize; ++j)
        {
            if (flipIndices[j] == i)
            {
                cout << j + 1 << ", ";
            }
        }
        cout << endl;
    }
#endif
    cout << endl;
}

static void shuffleTable(int offset)
{
    if (offset == kTableSize)
    {
        analyzeTable();
        return;
    }

    uint8_t y = Table[offset - 1];

    for (int i = offset; i < kTableSize; ++i)
    {
        uint8_t x = Table[i];

        if (hamming1(x, y))
        {
            Table[i] = Table[offset];
            Table[offset] = x;

            shuffleTable(offset + 1);

            Table[offset] = Table[i];
            Table[i] = x;
        }
    }
}

static void GenerateGrayCodes()
{
    for (int i = 0; i < kTableSize; ++i)
    {
        Table[i] = (uint8_t)i;
        AccumulatedPopCount[i] = 255;
        BackAccumulatedPopCount[i] = 255;
    }

    shuffleTable(2);
}

/*
    4 00000 <- 0
    0 00001 <- 1
    1 00011 <- 2
    0 00010 <- 3
    2 00110
    0 00111
    1 00101
    0 00100
    3 01100
    0 01101
    1 01111
    0 01110
    2 01010
    0 01011
    1 01001 <- 14
    0 01000 <- 15
    4 11000 <- 16
    0 11001
    1 11011
    0 11010
    2 11110
    0 11111
    1 11101
    0 11100
    3 10100
    0 10101
    1 10111
    0 10110
    2 10010
    0 10011
    1 10001
    0 10000 <- 31
*/

// Get the next bit to flip to produce the 8-bit Gray code at the provided index
// Precondition: index > 0 and index < 256
static int GetBitFlipForGrayCode8(int index)
{
    if (index & 1)
        return 0;

    if (index & 15)
        return (0x6764 >> (index & 14)) & 3;

    return ((0x12131210 >> (index >> 3)) & 3) + 4;
}

static int GetBitFlipForGrayCode8_ref(int index)
{
    int g0 = (index - 1) ^ ((index - 1) >> 1);
    int g1 = index ^ (index >> 1);
    int d = g1 ^ g0;
    unsigned long bit;
    if (1 != _BitScanForward(&bit, d))
    {
        return 0;
    }
    return (int)bit;
}

static void ReflectedBinaryGrayCodeTest()
{
    for (int index = 1; index < 256; ++index)
    {
        if (GetBitFlipForGrayCode8(index) != GetBitFlipForGrayCode8_ref(index))
        {
            cout << "ERROR at " << index << " : " << GetBitFlipForGrayCode8(index) << " != " << GetBitFlipForGrayCode8_ref(index) << endl;
        }
    }

    int x = 0;

    siamese::PCGRandom prng;

    for (int i = 0; i < 10; ++i)
    {
        prng.Seed(0);

        ::Sleep(100);

        uint32_t t0, t1;

        t0 = (uint32_t)__rdtsc();
        for (int trials = 0; trials < 10000; ++trials)
        {
            x ^= GetBitFlipForGrayCode8(prng.Next() % 255 + 1);
        }
        t1 = (uint32_t)__rdtsc();
        cout << "New method: " << t1 - t0 << endl;

        prng.Seed(0);

        ::Sleep(100);

        t0 = (uint32_t)__rdtsc();
        for (int trials = 0; trials < 10000; ++trials)
        {
            x ^= GetBitFlipForGrayCode8_ref(prng.Next() % 255 + 1);
        }
        t1 = (uint32_t)__rdtsc();
        cout << "Old method: " << t1 - t0 << endl;
    }
}

#ifdef ENABLE_GENTAB_GRAYCODE

int main()
{
    //GenerateGrayCodes();

    ReflectedBinaryGrayCodeTest();

    return 0;
}

#endif // ENABLE_GENTAB_GRAYCODE
