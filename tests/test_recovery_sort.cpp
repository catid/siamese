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

#define ENABLE_TEST_RECOVERY_SORT

#include <iostream>
#include <iomanip>
#include <cassert>
#include <vector>
using namespace std;

#include "../SiameseDecoder.h"
#include "../SiameseCommon.h"

#ifdef ENABLE_TEST_RECOVERY_SORT
#include "../SiameseDecoder.cpp"
#include "../SiameseCommon.cpp"
#endif

static void TestRecoverySort()
{
    // Verifying assertion:
    /*
        This insertion order guarantees that the left and right side of
        the recovery input ranges are monotonically increasing as in:

            recovery 0: 012345
            recovery 1:   23456 <- Cauchy row
            recovery 2: 01234567
            recovery 3:     45678
            recovery 4:     456789
    */
    pktalloc::Allocator allocator;
    siamese::RecoveryPacketList lister;
    siamese::CheckedRegionState region;
    lister.TheAllocator = &allocator;
    lister.CheckedRegion = &region;
    siamese::RecoveryMatrixState matrix;
    region.RecoveryMatrix = &matrix;

    static const unsigned kRecoveryCount = 5;
    static const unsigned kRanges[kRecoveryCount * 2] = {
        0, 5,
        2, 6,
        0, 7,
        4, 8,
        4, 9
    };
    siamese::RecoveryPacket* recoveries[kRecoveryCount];

    // Check all possible inputs
    unsigned order[kRecoveryCount];
    for (unsigned i = 0; i < kRecoveryCount; ++i)
    {
        order[0] = i;
        for (unsigned j = 0; j < kRecoveryCount; ++j)
        {
            if (i == j)
                continue;
            order[1] = j;
            for (unsigned k = 0; k < kRecoveryCount; ++k)
            {
                if (i == k || j == k)
                    continue;
                order[2] = k;
                for (unsigned m = 0; m < kRecoveryCount; ++m)
                {
                    if (i == m || j == m || k == m)
                        continue;
                    order[3] = m;
                    for (unsigned n = 0; n < kRecoveryCount; ++n)
                    {
                        if (i == n || j == n || k == n || m == n)
                            continue;
                        order[4] = n;
                        static_assert(5 == kRecoveryCount, "FIX");

                        for (unsigned y = 0; y < kRecoveryCount; ++y)
                        {
                            siamese::RecoveryPacket* recovery = allocator.Construct<siamese::RecoveryPacket>();
                            recovery->Metadata.ColumnStart = kRanges[y * 2];
                            recovery->Metadata.SumCount = kRanges[y * 2 + 1] - kRanges[y * 2] + 1;

                            recovery->Metadata.Row = y;
                            recovery->ElementStart = recovery->Metadata.ColumnStart;
                            recovery->Metadata.LDPCCount = recovery->Metadata.SumCount;
                            recovery->ElementEnd = recovery->ElementStart + recovery->Metadata.SumCount;

                            unsigned x = order[y];
                            recoveries[x] = recovery;
                            lister.Insert(recovery);
                        }

                        SIAMESE_DEBUG_ASSERT(lister.RecoveryPacketCount == kRecoveryCount);
                        SIAMESE_DEBUG_ASSERT(lister.Head);
                        SIAMESE_DEBUG_ASSERT(lister.Tail);

                        siamese::RecoveryPacket* recovery = lister.Head;
                        siamese::RecoveryPacket* prev = nullptr;
                        for (unsigned y = 0; y < kRecoveryCount; ++y)
                        {
                            SIAMESE_DEBUG_ASSERT(recovery != nullptr);
                            // Verify it was sorted in the correct order
                            SIAMESE_DEBUG_ASSERT(recovery->Metadata.Row == y);
                            prev = recovery;
                            recovery = recovery->Next;
                            SIAMESE_DEBUG_ASSERT(!recovery || recovery->Prev == prev);
                            lister.Delete(prev);
                        }
                        SIAMESE_DEBUG_ASSERT(recovery == nullptr);

                        const unsigned usedBytes = allocator.GetMemoryUsedBytes();
                        SIAMESE_DEBUG_ASSERT(usedBytes == 0);
                        SIAMESE_DEBUG_ASSERT(lister.RecoveryPacketCount == 0);
                    }
                }
            }
        }
    }

    cout << "Test passed!" << endl;
}

#ifdef ENABLE_TEST_RECOVERY_SORT

int main()
{
    TestRecoverySort();

    int x;
    cin >> x;

    return 0;
}

#endif
