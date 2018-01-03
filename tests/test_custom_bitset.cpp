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

#include "../SiameseDecoder.h"

#define ENABLE_TEST_BITFIELD

inline void Assert(unsigned N, bool condition)
{
    if (!condition)
    {
        cout << "**************************** FAILED for N = " << N << endl;
        SIAMESE_DEBUG_BREAK();
    }
}

template<unsigned BFSize>
void TestBitfield()
{
    pktalloc::CustomBitSet<BFSize> bf;

    // Set/Clear/Check:
    cout << "Testing Set/Clear/Check for N = " << BFSize << endl;

    bf.ClearAll();
    for (unsigned j = 0; j < BFSize; ++j)
    {
        Assert(BFSize, !bf.Check(j));
    }

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.ClearAll();
        bf.Set(i);
        for (unsigned j = 0; j < BFSize; ++j)
        {
            Assert(BFSize, bf.Check(j) == (j == i));
        }
    }

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.SetAll();
        bf.Clear(i);
        for (unsigned j = 0; j < BFSize; ++j)
        {
            Assert(BFSize, bf.Check(j) == (j != i));
        }
    }

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.Set(i);
    }
    for (unsigned j = 0; j < BFSize; ++j)
    {
        Assert(BFSize, bf.Check(j));
    }

    // SetRange:
    cout << "Testing SetRange/ClearRange for N = " << BFSize << endl;

    for (unsigned i = 0; i < BFSize; ++i)
    {
        for (unsigned j = i; j <= BFSize; ++j)
        {
            bf.ClearAll();
            bf.SetRange(i, j);
            for (unsigned k = 0; k < BFSize; ++k)
            {
                Assert(BFSize, bf.Check(k) == (k >= i && k < j));
            }
        }
    }

    // ClearRange:

    for (unsigned i = 0; i < BFSize; ++i)
    {
        for (unsigned j = i; j <= BFSize; ++j)
        {
            bf.SetAll();
            bf.ClearRange(i, j);
            for (unsigned k = 0; k < BFSize; ++k)
            {
                Assert(BFSize, bf.Check(k) != (k >= i && k < j));
            }
        }
    }

    // RangePopcount:

    static const unsigned kRandomTrials = 1000;
    for (unsigned i = 0; i < kRandomTrials; ++i)
    {
        if (i % 100 == 0)
        {
            cout << "Testing RangePopcount for N = " << BFSize << "..." << endl;
        }

        bf.ClearAll();
        if (i == 0)
        {
            // Empty
        }
        else if (i == 1)
        {
            // All on
            for (unsigned j = 0; j < BFSize; ++j)
            {
                bf.Set(j);
            }
        }
        else if (i == 2)
        {
            // Evens
            for (unsigned j = 0; j < BFSize; j += 2)
            {
                bf.Set(j);
            }
        }
        else if (i == 3)
        {
            // Odds
            for (unsigned j = 1; j < BFSize; j += 2)
            {
                bf.Set(j);
            }
        }
        else
        {
            siamese::PCGRandom prng;
            prng.Seed(i + BFSize * kRandomTrials);

            bf.ClearAll();
            for (unsigned j = 0; j < BFSize; ++j)
            {
                if ((prng.Next() & 4) != 0)
                    bf.Set(j);
            }
        }

        for (unsigned j = 0; j < BFSize; ++j)
        {
            for (unsigned k = j + 1; k < BFSize; ++k)
            {
                unsigned count = bf.RangePopcount(j, k);

                unsigned refCount = 0;
                for (unsigned t = j; t < k; ++t)
                {
                    if (bf.Check(t))
                        refCount++;
                }

                Assert(BFSize, count == refCount);
            }
        }
    }

    // FindFirstClear:
    cout << "Testing FindFirstClear for N = " << BFSize << endl;

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.ClearAll();
        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (i != j)
                bf.Set(j);
        }

        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j <= i)
            {
                Assert(BFSize, i == bf.FindFirstClear(j));
            }
            else
            {
                Assert(BFSize, bf.kValidBits == bf.FindFirstClear(j));
            }
        }
    }

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.ClearAll();
        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j >= i)
                bf.Set(j);
        }

        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j < i)
            {
                Assert(BFSize, j == bf.FindFirstClear(j));
            }
            else
            {
                Assert(BFSize, bf.kValidBits == bf.FindFirstClear(j));
            }
        }
    }

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.ClearAll();
        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j < i)
                bf.Set(j);
        }

        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j < i)
            {
                Assert(BFSize, i == bf.FindFirstClear(j));
            }
            else
            {
                Assert(BFSize, j == bf.FindFirstClear(j));
            }
        }
    }

    // FindFirstSet:
    cout << "Testing FindFirstSet for N = " << BFSize << endl;

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.SetAll();
        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (i != j)
                bf.Clear(j);
        }

        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j <= i)
            {
                Assert(BFSize, i == bf.FindFirstSet(j));
            }
            else
            {
                Assert(BFSize, bf.kValidBits == bf.FindFirstSet(j));
            }
        }
    }

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.SetAll();
        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j >= i)
                bf.Clear(j);
        }

        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j < i)
            {
                Assert(BFSize, j == bf.FindFirstSet(j));
            }
            else
            {
                Assert(BFSize, bf.kValidBits == bf.FindFirstSet(j));
            }
        }
    }

    for (unsigned i = 0; i < BFSize; ++i)
    {
        bf.SetAll();
        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j < i)
                bf.Clear(j);
        }

        for (unsigned j = 0; j < BFSize; ++j)
        {
            if (j < i)
            {
                Assert(BFSize, i == bf.FindFirstSet(j));
            }
            else
            {
                Assert(BFSize, j == bf.FindFirstSet(j));
            }
        }
    }
}


#ifdef ENABLE_TEST_BITFIELD

int main()
{
    TestBitfield<1>();
    TestBitfield<2>();
    TestBitfield<3>();
    TestBitfield<4>();
    TestBitfield<5>();
    TestBitfield<6>();
    TestBitfield<7>();
    TestBitfield<8>();
    TestBitfield<9>();
    TestBitfield<10>();
    TestBitfield<11>();
    TestBitfield<12>();
    TestBitfield<13>();
    TestBitfield<14>();
    TestBitfield<15>();
    TestBitfield<16>();
    TestBitfield<17>();
    TestBitfield<18>();
    TestBitfield<19>();
    TestBitfield<20>();
    TestBitfield<21>();
    TestBitfield<22>();
    TestBitfield<23>();
    TestBitfield<24>();
    TestBitfield<25>();
    TestBitfield<26>();
    TestBitfield<27>();
    TestBitfield<28>();
    TestBitfield<29>();
    TestBitfield<30>();
    TestBitfield<31>();
    TestBitfield<32>();
    TestBitfield<33>();
    TestBitfield<34>();
    TestBitfield<35>();
    TestBitfield<36>();
    TestBitfield<37>();
    TestBitfield<38>();
    TestBitfield<39>();
    TestBitfield<40>();
    TestBitfield<41>();
    TestBitfield<42>();
    TestBitfield<43>();
    TestBitfield<44>();
    TestBitfield<45>();
    TestBitfield<46>();
    TestBitfield<47>();
    TestBitfield<48>();
    TestBitfield<49>();
    TestBitfield<50>();
    TestBitfield<51>();
    TestBitfield<52>();
    TestBitfield<53>();
    TestBitfield<54>();
    TestBitfield<55>();
    TestBitfield<56>();
    TestBitfield<57>();
    TestBitfield<58>();
    TestBitfield<59>();
    TestBitfield<60>();
    TestBitfield<61>();
    TestBitfield<62>();
    TestBitfield<63>();
    TestBitfield<64>();
    TestBitfield<65>();
    TestBitfield<66>();
    TestBitfield<67>();
    TestBitfield<68>();
    TestBitfield<69>();

    TestBitfield<126>();
    TestBitfield<127>();
    TestBitfield<128>();
    TestBitfield<129>();
    TestBitfield<130>();

    TestBitfield<254>();
    TestBitfield<255>();
    TestBitfield<256>();
    TestBitfield<257>();
    TestBitfield<258>();

    TestBitfield<1022>();
    TestBitfield<1023>();
    TestBitfield<1024>();
    TestBitfield<1025>();
    TestBitfield<1026>();

    TestBitfield<1235>();

    return 0;
}

#endif // ENABLE_UNIT_TEST
