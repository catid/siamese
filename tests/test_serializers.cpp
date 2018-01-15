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

#include "../siamese.h"
#include "../SiameseCommon.h"
#include "../SiameseSerializers.h"

#define ENABLE_TEST_SERIALIZERS

inline void Assert(unsigned N, bool condition)
{
    if (!condition)
    {
        cout << "**************************** FAILED for N = " << N << endl;
        SIAMESE_DEBUG_BREAK();
    }
}

bool TestPODSerialization16()
{
    static const unsigned kBufferBytes = 20;
    uint8_t buffer[kBufferBytes] = { 0 };

    uint16_t x = 0xabcd;

    siamese::WriteU16_LE(buffer + 1, x);

    const uint8_t expectedResult[kBufferBytes] = {
        0, 0xcd, 0xab, 0
    };

    for (unsigned i = 0; i < 20; ++i)
    {
        if (buffer[i] != expectedResult[i])
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    buffer[0] = 0xff;
    buffer[3] = 0xff;

    if (siamese::ReadU16_LE(buffer + 1) != x)
    {
        SIAMESE_DEBUG_BREAK();
        return false;
    }

    return true;
}

bool TestPODSerialization24()
{
    static const unsigned kBufferBytes = 20;
    uint8_t buffer[kBufferBytes] = { 0 };

    uint32_t x = 0xabcdef;

    siamese::WriteU24_LE(buffer + 1, x);

    const uint8_t expectedResult[kBufferBytes] = {
        0, 0xef, 0xcd, 0xab, 0
    };

    for (unsigned i = 0; i < 20; ++i)
    {
        if (buffer[i] != expectedResult[i])
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    buffer[0] = 0xff;
    buffer[4] = 0xff;

    if (siamese::ReadU24_LE(buffer + 1) != x)
    {
        SIAMESE_DEBUG_BREAK();
        return false;
    }

    return true;
}

bool TestPODSerialization32()
{
    static const unsigned kBufferBytes = 20;
    uint8_t buffer[kBufferBytes] = { 0 };

    uint32_t x = 0x89abcdef;

    siamese::WriteU32_LE(buffer + 1, x);

    const uint8_t expectedResult[kBufferBytes] = {
        0, 0xef, 0xcd, 0xab, 0x89, 0
    };

    for (unsigned i = 0; i < 20; ++i)
    {
        if (buffer[i] != expectedResult[i])
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    buffer[0] = 0xff;
    buffer[5] = 0xff;

    if (siamese::ReadU32_LE(buffer + 1) != x)
    {
        SIAMESE_DEBUG_BREAK();
        return false;
    }

    return true;
}

bool TestPODSerialization64()
{
    static const unsigned kBufferBytes = 20;
    uint8_t buffer[kBufferBytes] = { 0 };

    uint64_t x = 0x0123456789abcdefULL;

    siamese::WriteU64_LE(buffer + 1, x);

    const uint8_t expectedResult[kBufferBytes] = {
        0, 0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0
    };

    for (unsigned i = 0; i < 20; ++i)
    {
        if (buffer[i] != expectedResult[i])
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    buffer[0] = 0xff;
    buffer[9] = 0xff;

    if (siamese::ReadU64_LE(buffer + 1) != x)
    {
        SIAMESE_DEBUG_BREAK();
        return false;
    }

    return true;
}

bool TestByteStream()
{
    uint8_t buffer[256];

    siamese::WriteByteStream bs(buffer, 256);

    const uint8_t x = 0x01;
    const uint16_t y = 0x2345;
    const uint32_t z = 0x6789ab;
    const uint32_t w = 0xcdef1234;
    const uint64_t t = 0x2143567890badcfeULL;

    bs.Write8(x);
    bs.Write16(y);
    bs.Write24(z);
    bs.Write32(w);
    bs.Write64(t);

    if (bs.WrittenBytes != 1 + 2 + 3 + 4 + 8)
    {
        SIAMESE_DEBUG_BREAK();
        return false;
    }

    siamese::ReadByteStream bs1(buffer, 256);

    uint8_t x1 = bs1.Read8();
    uint16_t y1 = bs1.Read16();
    uint32_t z1 = bs1.Read24();
    uint32_t w1 = bs1.Read32();
    uint64_t t1 = bs1.Read64();

    if (x != x1 || y != y1 || z != z1 || w != w1 || t != t1)
    {
        return false;
    }
    if (bs1.BytesRead != 1 + 2 + 3 + 4 + 8)
    {
        SIAMESE_DEBUG_BREAK();
        return false;
    }

    return true;
}

bool TestPacketCount_Header()
{
    uint8_t buffer[siamese::kMaxPacketCountFieldBytes];

    static const unsigned kCountsCount = 9;
    static const unsigned Counts[kCountsCount] = {
        1, 2, 3, 126, 127, 128, 129, SIAMESE_MAX_PACKETS - 1, SIAMESE_MAX_PACKETS
    };
    for (unsigned i = 0; i < kCountsCount; ++i)
    {
        unsigned count = Counts[i];

        unsigned written = siamese::SerializeHeader_PacketCount(count, buffer);
        if (written < 1 || written > siamese::kMaxPacketCountFieldBytes)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        unsigned countOut = 0xfffffff;
        int bytes = siamese::DeserializeHeader_PacketCount(buffer, (unsigned)sizeof(buffer), countOut);
        if (bytes < 0 || bytes != (int)written)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        if (count != countOut)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    return true;
}

bool TestPacketCount_Footer()
{
    uint8_t buffer[siamese::kMaxPacketCountFieldBytes];

    static const unsigned kCountsCount = 9;
    static const unsigned Counts[kCountsCount] = {
        1, 2, 3, 126, 127, 128, 129, SIAMESE_MAX_PACKETS - 1, SIAMESE_MAX_PACKETS
    };
    for (unsigned i = 0; i < kCountsCount; ++i)
    {
        unsigned count = Counts[i];

        unsigned written = siamese::SerializeFooter_PacketCount(count, buffer);
        if (written < 1 || written > siamese::kMaxPacketCountFieldBytes)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        unsigned countOut = 0xfffffff;
        int bytes = siamese::DeserializeFooter_PacketCount(buffer, written, countOut);
        if (bytes < 0 || bytes != (int)written)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        if (count != countOut)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    return true;
}

bool TestPacketLength_Header()
{
    uint8_t buffer[siamese::kMaxPacketLengthFieldBytes];

    static const unsigned kCountsCount = 16;
    static const unsigned Counts[kCountsCount] = {
        1, 2, 3, 0x7d, 0x7e, 0x7f, 0x80, 0x81,
        0x3fff - 1, 0x3fff, 0x3fff + 1, 0x1fffff - 1,
        0x1fffff, 0x1fffff + 1, SIAMESE_MAX_PACKET_BYTES - 1,
        SIAMESE_MAX_PACKET_BYTES
    };
    for (unsigned i = 0; i < kCountsCount; ++i)
    {
        unsigned count = Counts[i];

        unsigned written = siamese::SerializeHeader_PacketLength(count, buffer);
        if (written < 1 || written > siamese::kMaxPacketLengthFieldBytes)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        unsigned countOut = 0xfffffff;
        int bytes = siamese::DeserializeHeader_PacketLength(buffer, (unsigned)sizeof(buffer), countOut);
        if (bytes < 0 || bytes != (int)written)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        if (count != countOut)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    return true;
}

bool TestPacketLength_Footer()
{
    uint8_t buffer[siamese::kMaxPacketLengthFieldBytes];

    static const unsigned kCountsCount = 16;
    static const unsigned Counts[kCountsCount] = {
        1, 2, 3, 0x7d, 0x7e, 0x7f, 0x80, 0x81,
        0x3fff - 1, 0x3fff, 0x3fff + 1, 0x1fffff - 1,
        0x1fffff, 0x1fffff + 1, SIAMESE_MAX_PACKET_BYTES - 1,
        SIAMESE_MAX_PACKET_BYTES
    };
    for (unsigned i = 0; i < kCountsCount; ++i)
    {
        unsigned count = Counts[i];

        unsigned written = siamese::SerializeFooter_PacketLength(count, buffer);
        if (written < 1 || written > siamese::kMaxPacketLengthFieldBytes)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        unsigned countOut = 0xfffffff;
        int bytes = siamese::DeserializeFooter_PacketLength(buffer, written, countOut);
        if (bytes < 0 || bytes != (int)written)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        if (count != countOut)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    return true;
}

bool TestPacketNum_Header()
{
    uint8_t buffer[siamese::kMaxPacketNumEncodedBytes];

    static const unsigned kColumnStartsCount = 17;
    static const unsigned ColumnStarts[kColumnStartsCount] = {
        0, 1, 2, 3, 4, 0x7e, 0x7f, 0x80, 0x81, 0x3ffe, 0x3fff, 0x4000,
        0x4001, 0x4002, 0x4003, SIAMESE_PACKET_NUM_MAX - 1, SIAMESE_PACKET_NUM_MAX
    };
    for (unsigned i = 0; i < kColumnStartsCount; ++i)
    {
        unsigned count = ColumnStarts[i];

        unsigned written = siamese::SerializeHeader_PacketNum(count, buffer);
        if (written < 1 || written > siamese::kMaxPacketNumEncodedBytes)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        unsigned countOut = 0xfffffff;
        int bytes = siamese::DeserializeHeader_PacketNum(buffer, (unsigned)sizeof(buffer), countOut);
        if (bytes < 0 || bytes != (int)written)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        if (count != countOut)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    return true;
}

bool TestPacketNum_Footer()
{
    uint8_t buffer[siamese::kMaxPacketNumEncodedBytes];

    static const unsigned kColumnStartsCount = 17;
    static const unsigned ColumnStarts[kColumnStartsCount] = {
        0, 1, 2, 3, 4, 0x7e, 0x7f, 0x80, 0x81, 0x3ffe, 0x3fff, 0x4000,
        0x4001, 0x4002, 0x4003, SIAMESE_PACKET_NUM_MAX - 1, SIAMESE_PACKET_NUM_MAX
    };
    for (unsigned i = 0; i < kColumnStartsCount; ++i)
    {
        unsigned count = ColumnStarts[i];

        unsigned written = siamese::SerializeFooter_PacketNum(count, buffer);
        if (written < 1 || written > siamese::kMaxPacketNumEncodedBytes)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        unsigned countOut = 0xfffffff;
        int bytes = siamese::DeserializeFooter_PacketNum(buffer, written, countOut);
        if (bytes < 0 || bytes != (int)written)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        if (count != countOut)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    return true;
}

bool TestRecoveryMetadata_Footer()
{
    uint8_t buffer[siamese::kMaxRecoveryMetadataBytes];

    static const unsigned kCountsCount = 9;
    static const unsigned Counts[kCountsCount] = {
        1, 2, 3, 126, 127, 128, 129, SIAMESE_MAX_PACKETS - 1, SIAMESE_MAX_PACKETS
    };

    static const unsigned kRowsCount = siamese::kRowPeriod;
    static unsigned Rows[kRowsCount] = {
    };
    for (unsigned i = 0; i < kRowsCount; ++i)
        Rows[i] = i;

    static const unsigned kColumnStartsCount = 17;
    static const unsigned ColumnStarts[kColumnStartsCount] = {
        0, 1, 2, 3, 4, 0x7e, 0x7f, 0x80, 0x81, 0x3ffe, 0x3fff, 0x4000,
        0x4001, 0x4002, 0x4003, SIAMESE_PACKET_NUM_MAX - 1, SIAMESE_PACKET_NUM_MAX
    };

    for (unsigned i = 0; i < kCountsCount; ++i)
    for (unsigned j = 0; j < kRowsCount; ++j)
    for (unsigned k = 0; k < kCountsCount; ++k)
    for (unsigned m = 0; m < kColumnStartsCount; ++m)
    {
        siamese::RecoveryMetadata metadata;
        metadata.SumCount = Counts[i];
        metadata.Row = Rows[j];
        metadata.LDPCCount = Counts[k];
        metadata.ColumnStart = ColumnStarts[m];

        if (metadata.LDPCCount > metadata.SumCount)
            continue;

        unsigned written = siamese::SerializeFooter_RecoveryMetadata(metadata, buffer);
        if (written < 1 || written > siamese::kMaxRecoveryMetadataBytes)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        siamese::RecoveryMetadata metadataOut;
        int bytes = siamese::DeserializeFooter_RecoveryMetadata(buffer, written, metadataOut);
        if (bytes < 0 || bytes != (int)written)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        if (metadata.SumCount > 1)
        {
            if (metadata.ColumnStart != metadataOut.ColumnStart ||
                metadata.LDPCCount != metadataOut.LDPCCount ||
                metadata.Row != metadataOut.Row ||
                metadata.SumCount != metadataOut.SumCount)
            {
                SIAMESE_DEBUG_BREAK();
                return false;
            }
        }
        else
        {
            if (metadata.ColumnStart != metadataOut.ColumnStart ||
                metadata.SumCount != metadataOut.SumCount)
            {
                SIAMESE_DEBUG_BREAK();
                return false;
            }
            if (metadataOut.LDPCCount != 1 ||
                metadataOut.Row != 0)
            {
                SIAMESE_DEBUG_BREAK();
                return false;
            }
        }
    }

    return true;
}

bool TestNACKLossRange_Header()
{
    uint8_t buffer[siamese::kMaxLossRangeFieldBytes];

    static const unsigned kColumnStartsCount = 15;
    static const unsigned ColumnStarts[kColumnStartsCount] = {
        0, 1, 2, 3, (1 << 5) - 1, (1 << 5), (1 << 5) + 1,
        (1 << (5 + 7)) - 1, (1 << (5 + 7)), (1 << (5 + 7)) + 1,
        (1 << (5 + 7 + 7)) - 1, (1 << (5 + 7 + 7)), (1 << (5 + 7 + 7)) + 1,
        SIAMESE_PACKET_NUM_MAX - 1, SIAMESE_PACKET_NUM_MAX
    };

    static const unsigned kLossCountM1Count = 14;
    static const unsigned LossCountM1s[kLossCountM1Count] = {
        0, 1, 2, 3, 4, 5, (1 << 7) - 1, (1 << 7), (1 << 7) - 1,
        (1 << (7 + 7)) - 1, (1 << (7 + 7)), (1 << (7 + 7)) + 1,
        SIAMESE_PACKET_NUM_MAX - 1, SIAMESE_PACKET_NUM_MAX
    };

    for (unsigned i = 0; i < kColumnStartsCount; ++i)
    for (unsigned j = 0; j < kLossCountM1Count; ++j)
    {
        unsigned columnStart = ColumnStarts[i];
        unsigned lossCountM1 = LossCountM1s[j];

        unsigned written = siamese::SerializeHeader_NACKLossRange(columnStart, lossCountM1, buffer);
        if (written < 1 || written > siamese::kMaxRecoveryMetadataBytes)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        unsigned columnStartOut;
        unsigned lossCountM1Out;
        int bytes = siamese::DeserializeHeader_NACKLossRange(buffer, (unsigned)sizeof(buffer), columnStartOut, lossCountM1Out);
        if (bytes < 0 || bytes != (int)written)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
        if (columnStart != columnStartOut ||
            lossCountM1 != lossCountM1Out)
        {
            SIAMESE_DEBUG_BREAK();
            return false;
        }
    }

    return true;
}

bool TestSerializers()
{
    if (!TestPODSerialization16())
        return false;
    if (!TestPODSerialization24())
        return false;
    if (!TestPODSerialization32())
        return false;
    if (!TestPODSerialization64())
        return false;

    if (!TestByteStream())
        return false;

    if (!TestPacketCount_Header())
        return false;
    if (!TestPacketCount_Footer())
        return false;

    if (!TestPacketLength_Header())
        return false;
    if (!TestPacketLength_Footer())
        return false;

    if (!TestPacketNum_Header())
        return false;
    if (!TestPacketNum_Footer())
        return false;

    if (!TestRecoveryMetadata_Footer())
        return false;

    if (!TestNACKLossRange_Header())
        return false;

    return true;
}


#ifdef ENABLE_TEST_SERIALIZERS

int main()
{
    if (!TestSerializers())
    {
        cout << "FAIL" << endl;
        return -1;
    }

    cout << "Success!" << endl;
    return 0;
}

#endif // ENABLE_TEST_SERIALIZERS
