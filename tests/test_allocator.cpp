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

#include "../PacketAllocator.h"
#include "../SiameseTools.h"

#define ENABLE_TEST_ALLOCATOR
#define ENABLE_TEST_REALLOC
#define ENABLE_TEST_SHRINK

static const unsigned kAllocationCount = 8000;

void TestAllocator()
{
    pktalloc::Allocator allocator;

    cout << "Used at start: " << allocator.GetMemoryUsedBytes() << endl;
    cout << "Allocated at start: " << allocator.GetMemoryAllocatedBytes() << endl;

    siamese::PCGRandom prng;
    prng.Seed(0);

    for (unsigned j = 0; j < 2000; ++j)
    {
        cout << "Test iteration " << j << endl;

        if (!allocator.IntegrityCheck())
        {
            cout << "Integrity check failed(3)!" << endl;
            return;
        }

#ifdef ENABLE_TEST_SHRINK
        {
            uint8_t* allocationsA[kAllocationCount];
            unsigned sizesA[kAllocationCount];
            unsigned sizesB[kAllocationCount];

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                sizesA[i] = 1 + (prng.Next() % 4000);
                sizesB[i] = sizesA[i] + (prng.Next() % (4000 - sizesA[i] + 1));
                SIAMESE_DEBUG_ASSERT(sizesB[i] >= sizesA[i]);
                allocationsA[i] = allocator.Allocate(sizesB[i]);
                SIAMESE_DEBUG_ASSERT(allocationsA[i]);
                memset(allocationsA[i], 1, sizesB[i]);
            }

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                allocator.Shrink(allocationsA[i], sizesA[i]);
                memset(allocationsA[i], 3, sizesA[i]);
            }

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                memset(allocationsA[i], 4, sizesA[i]);
                allocator.Free(allocationsA[i]);
            }
        }

        if (!allocator.IntegrityCheck())
        {
            cout << "Integrity check failed(1)!" << endl;
            return;
        }
#endif

        {
            uint8_t* allocationsA[kAllocationCount];
            unsigned sizesA[kAllocationCount];

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                sizesA[i] = i + 1;
                allocationsA[i] = allocator.Allocate(sizesA[i]);
                SIAMESE_DEBUG_ASSERT(allocationsA[i]);
                memset(allocationsA[i], 1, sizesA[i]);
            }

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                memset(allocationsA[i], 2, sizesA[i]);
                allocator.Free(allocationsA[i]);
            }

            uint8_t* allocationsB[kAllocationCount];
            unsigned sizesB[kAllocationCount];

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                sizesB[i] = 1 + (prng.Next() % 4000);
                allocationsB[i] = allocator.Allocate(sizesB[i]);
                SIAMESE_DEBUG_ASSERT(allocationsB[i]);
                memset(allocationsB[i], 1, sizesB[i]);
            }

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                memset(allocationsB[i], 2, sizesB[i]);
                allocator.Free(allocationsB[i]);
            }
        }

        if (!allocator.IntegrityCheck())
        {
            cout << "Integrity check failed(1)!" << endl;
            return;
        }

        {
            uint8_t* allocationsA[kAllocationCount];
            uint8_t* allocationsB[kAllocationCount];
            unsigned sizesA[kAllocationCount];
            unsigned sizesB[kAllocationCount];

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                sizesA[i] = i + 1;
                allocationsA[i] = allocator.Allocate(sizesA[i]);
                SIAMESE_DEBUG_ASSERT(allocationsA[i]);
                memset(allocationsA[i], 1, sizesA[i]);

                sizesB[i] = 1 + (prng.Next() % 4000);
                allocationsB[i] = allocator.Allocate(sizesB[i]);
                SIAMESE_DEBUG_ASSERT(allocationsB[i]);
                memset(allocationsB[i], 1, sizesB[i]);

                memset(allocationsA[i], 2, sizesA[i]);
                allocator.Free(allocationsA[i]);
            }

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                memset(allocationsB[i], 2, sizesB[i]);
                allocator.Free(allocationsB[i]);
            }
        }

        if (!allocator.IntegrityCheck())
        {
            cout << "Integrity check failed(2)!" << endl;
            return;
        }

        {
            uint8_t* allocationsA[kAllocationCount];
            uint8_t* allocationsB[kAllocationCount];
            unsigned sizesA[kAllocationCount];
            unsigned sizesB[kAllocationCount];

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                sizesB[i] = 1 + (prng.Next() % 4000);
                allocationsB[i] = allocator.Allocate(sizesB[i]);
                SIAMESE_DEBUG_ASSERT(allocationsB[i]);
                memset(allocationsB[i], 1, sizesB[i]);

                sizesA[i] = i + 1;
                allocationsA[i] = allocator.Allocate(sizesA[i]);
                SIAMESE_DEBUG_ASSERT(allocationsA[i]);
                memset(allocationsA[i], 1, sizesA[i]);

                memset(allocationsB[i], 2, sizesB[i]);
                allocator.Free(allocationsB[i]);
            }

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                memset(allocationsA[i], 2, sizesA[i]);
                allocator.Free(allocationsA[i]);
            }
        }

        if (!allocator.IntegrityCheck())
        {
            cout << "Integrity check failed(4)!" << endl;
            return;
        }

#ifdef ENABLE_TEST_REALLOC
        {
            uint8_t* allocationsA[kAllocationCount];
            unsigned sizesA[kAllocationCount];
            uint8_t* allocationsB[kAllocationCount];
            unsigned sizesB[kAllocationCount];

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                sizesA[i] = 1 + (prng.Next() % 4000);
                allocationsA[i] = allocator.Allocate(sizesA[i]);
                SIAMESE_DEBUG_ASSERT(allocationsA[i]);
                memset(allocationsA[i], 1, sizesA[i]);
            }

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                sizesB[i] = 1 + (prng.Next() % 4000);
                allocationsB[i] = allocator.Reallocate(allocationsA[i], sizesB[i], pktalloc::Realloc::Uninitialized);
                memset(allocationsB[i], 3, sizesB[i]);
            }

            for (unsigned i = 0; i < kAllocationCount; ++i)
            {
                memset(allocationsB[i], 4, sizesB[i]);
                allocator.Free(allocationsB[i]);
            }
        }

        if (!allocator.IntegrityCheck())
        {
            cout << "Integrity check failed(1)!" << endl;
            return;
        }
#endif
    }

    cout << "Used at end: " << allocator.GetMemoryUsedBytes() << endl;
    cout << "Allocated at end: " << allocator.GetMemoryAllocatedBytes() << endl;
}

#ifdef ENABLE_TEST_ALLOCATOR

int main()
{
    TestAllocator();

    int x;
    cin >> x;

    return 0;
}

#endif // ENABLE_UNIT_TEST
