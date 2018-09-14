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

#include <vector>
#include <string>
#include <queue>
using namespace std;

#include "../Logger.h"
#include "../siamese.h"
#include "../SiameseTools.h"
#include "../SiameseSerializers.h"

#define TEST_VARIABLE_SIZED_DATA

// Test: Verify the kMaximumLossRecoveryCount logic works properly
//#define TEST_LARGE_BURST_LOSS

// Test: This is a simulated channel with delay and uniform packetloss + ARQ + FEC for transport
//#define TEST_HARQ_STREAM

// Test: Encoding data with packetloss
#define TEST_STREAMING

// Test: Using Siamese as a block code
#define TEST_BLOCK
#define TEST_ENABLE_DECODER

// This experiment uses FEC instead of retransmission to see how it performs
//#define HARQ_RETRANSMIT_WITH_FEC

//#define VERBOSE_STREAMING_LOGS

static const unsigned kSeed = 1013;

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif


#ifdef VERBOSE_STREAMING_LOGS
    static logger::Channel Logger("UnitTest", logger::Level::Trace);
#else
    static logger::Channel Logger("UnitTest", logger::Level::Debug);
#endif


//------------------------------------------------------------------------------
// SetPacket

static unsigned GetPacketBytes(unsigned packetId)
{
#ifdef TEST_VARIABLE_SIZED_DATA
    siamese::PCGRandom prng;
    prng.Seed(packetId, 24124);
    return 2 + (prng.Next() % (1200 - 2));
#else
    return 1200;
#endif
}

static void SetPacket(unsigned packetId, void* packet, unsigned bytes)
{
    siamese::PCGRandom prng;
    prng.Seed(packetId, bytes);

    uint8_t* buffer = (uint8_t*)packet;

    if (bytes >= 4)
    {
        *(uint32_t*)(buffer) = bytes;
        buffer += 4, bytes -= 4;
    }
    while (bytes >= 4)
    {
        *(uint32_t*)(buffer) = prng.Next();
        buffer += 4, bytes -= 4;
    }
    if (bytes > 0)
    {
        uint32_t x = prng.Next();
        for (unsigned i = 0; i < bytes; ++i)
        {
            buffer[i] = (uint8_t)x;
            x >>= 8;
        }
    }
}

static bool CheckPacket(unsigned packetId, const void* packet, unsigned bytes)
{
    static const unsigned kCheckLimit = 2000;
    uint8_t expected[kCheckLimit];
    SIAMESE_DEBUG_ASSERT(bytes <= kCheckLimit);
    SetPacket(packetId, expected, bytes);
    return 0 == memcmp(expected, packet, bytes);
}


//------------------------------------------------------------------------------
// FunctionTimer

class FunctionTimer
{
public:
    FunctionTimer(const std::string& name)
    {
        FunctionName = name;
    }
    void BeginCall()
    {
        SIAMESE_DEBUG_ASSERT(t0 == 0);
        t0 = siamese::GetTimeUsec();
    }
    void EndCall()
    {
        SIAMESE_DEBUG_ASSERT(t0 != 0);
        uint64_t t1 = siamese::GetTimeUsec();
        ++Invokations;
        TotalUsec += t1 - t0;
        t0 = 0;
    }
    void Reset()
    {
        SIAMESE_DEBUG_ASSERT(t0 == 0);
        t0 = 0;
        Invokations = 0;
        TotalUsec = 0;
    }
    void Print(unsigned trials)
    {
        if (Invokations == 0)
            Logger.Info(FunctionName, " not called");
        else
            Logger.Info(FunctionName, " called ", Invokations / (float)trials, " times on avg. ", TotalUsec / (double)Invokations, " usec avg of ", trials, " trials");
    }

    uint64_t t0 = 0;
    uint64_t Invokations = 0;
    uint64_t TotalUsec = 0;
    std::string FunctionName;
};


static void BlockRecoveryTest()
{
    Logger.Info("Recover one large block up to 255...");

    static const int N = 1000;
    static const int K = 255;
    static_assert(K <= siamese::kMaximumLossRecoveryCount, "Too high");

    for (unsigned num = 0; num < 1000; ++num)
    for (int lossCount = N / 10 - 3; lossCount < K && lossCount <= N; ++lossCount)
    {
        siamese::PCGRandom prng;
        prng.Seed(kSeed, lossCount * 1000 + num);

        FunctionTimer t_siamese_encoder_create("siamese_encoder_create");
        FunctionTimer t_siamese_decoder_create("siamese_decoder_create");
        FunctionTimer t_siamese_encoder_add("siamese_encoder_add");
        FunctionTimer t_siamese_decoder_add_original("siamese_decoder_add_original");
        FunctionTimer t_siamese_encode("siamese_encode");
        FunctionTimer t_siamese_decoder_add_recovery("siamese_decoder_add_recovery");
        FunctionTimer t_siamese_decoder_is_ready("siamese_decoder_is_ready");
        FunctionTimer t_siamese_decode("siamese_decode");

        static const unsigned kTrials = 100;

        for (unsigned trial = 0; trial < kTrials; ++trial)
        {
            t_siamese_encoder_create.BeginCall();
            SiameseEncoder encoder = siamese_encoder_create();
            t_siamese_encoder_create.EndCall();

            if (!encoder)
            {
                Logger.Error("Unable to create encoder");
                SIAMESE_DEBUG_BREAK();
                return;
            }

            t_siamese_decoder_create.BeginCall();
            SiameseDecoder decoder = siamese_decoder_create();
            t_siamese_decoder_create.EndCall();

            if (!decoder)
            {
                Logger.Error("Unable to create decoder");
                SIAMESE_DEBUG_BREAK();
                return;
            }

            unsigned decoderReceiveCount = 0;

            for (int i = 0; i < N; ++i)
            {
                uint8_t buffer[2000];
                unsigned bytes = GetPacketBytes(i);
                SIAMESE_DEBUG_ASSERT(bytes <= sizeof(buffer));
                SetPacket(i, buffer, bytes);

                SiameseOriginalPacket original;
                original.Data = buffer;
                original.DataBytes = bytes;
                {
                    t_siamese_encoder_add.BeginCall();
                    int result = siamese_encoder_add(encoder, &original);
                    t_siamese_encoder_add.EndCall();
                    if (result)
                    {
                        Logger.Error("Unable to add original data to encoder");
                        SIAMESE_DEBUG_BREAK();
                        return;
                    }
                }

#ifdef TEST_ENABLE_DECODER
                // Lose first few packets
                if (i >= lossCount)
                {
                    t_siamese_decoder_add_original.BeginCall();
                    int result = siamese_decoder_add_original(decoder, &original);
                    t_siamese_decoder_add_original.EndCall();
                    if (result)
                    {
                        Logger.Error("Unable to add original data to decoder");
                        SIAMESE_DEBUG_BREAK();
                        return;
                    }
                    ++decoderReceiveCount;
                }
#endif // TEST_ENABLE_DECODER
            }

#ifdef TEST_ENABLE_DECODER
            for (int i = 0; i < K; ++i)
#else
            for (int i = 0; i < lossCount; ++i)
#endif
            {
                SiameseRecoveryPacket recovery;

                {
                    t_siamese_encode.BeginCall();
                    int result = siamese_encode(encoder, &recovery);
                    t_siamese_encode.EndCall();
                    if (result)
                    {
                        SIAMESE_DEBUG_BREAK();
                        Logger.Error("Unable to generate encoded data");
                        return;
                    }
                }

#ifdef TEST_ENABLE_DECODER
                {
                    t_siamese_decoder_add_recovery.BeginCall();
                    int result = siamese_decoder_add_recovery(decoder, &recovery);
                    t_siamese_decoder_add_recovery.EndCall();
                    if (result)
                    {
                        Logger.Error("Unable to add recovery data to decoder");
                        SIAMESE_DEBUG_BREAK();
                        return;
                    }
                }

                for (;;)
                {
                    t_siamese_decoder_is_ready.BeginCall();
                    int readyResult = siamese_decoder_is_ready(decoder);
                    t_siamese_decoder_is_ready.EndCall();

                    if (readyResult)
                    {
                        SIAMESE_DEBUG_ASSERT(readyResult == Siamese_NeedMoreData);
                        break;
                    }

                    SiameseOriginalPacket* packets = nullptr;
                    unsigned packetCount = 0;

                    t_siamese_decode.BeginCall();
                    int decodeResult = siamese_decode(decoder, &packets, &packetCount);
                    t_siamese_decode.EndCall();

                    if (decodeResult == Siamese_Success)
                    {
                        //cout << "Successful decode: ";
                        for (unsigned i = 0; i < packetCount; ++i)
                        {
                            //cout << packets[i].PacketNum << " ";

                            if (!CheckPacket(packets[i].PacketNum, packets[i].Data, packets[i].DataBytes))
                            {
                                Logger.Error("Packet check failed for ", i, ".DataBytes = ", packets[i].DataBytes);
                                SIAMESE_DEBUG_BREAK();
                                return;
                            }

                            ++decoderReceiveCount;
                        }
                        //cout);

                        if (decoderReceiveCount >= N)
                        {
                            goto DoneDecoding;
                        }
                    }
                    else if (decodeResult == Siamese_NeedMoreData)
                    {
                        //cout << "Needed more data to decode");
                    }
                    else
                    {
                        Logger.Error("Decode returned ", decodeResult);
                        SIAMESE_DEBUG_BREAK();
                        return;
                    }
                }
#endif // TEST_ENABLE_DECODER
            }

#ifdef TEST_ENABLE_DECODER
DoneDecoding:
#endif // TEST_ENABLE_DECODER

            siamese_encoder_free(encoder);
            siamese_decoder_free(decoder);
        }

        // Flush the log so we do not miss the last part
        logger::Flush();

        Logger.Info("Using ", lossCount, " recovery packets for ", N, " original packets:");

        t_siamese_encoder_create.Print(kTrials);
        t_siamese_encoder_add.Print(kTrials);
        t_siamese_encode.Print(kTrials);
#ifdef TEST_ENABLE_DECODER
        t_siamese_decoder_create.Print(kTrials);
        t_siamese_decoder_add_original.Print(kTrials);
        t_siamese_decoder_add_recovery.Print(kTrials);
        t_siamese_decoder_is_ready.Print(kTrials);
        t_siamese_decode.Print(kTrials);
#endif
    }
}


static void StreamingTest()
{
    siamese::PCGRandom prngLoss;
    prngLoss.Seed(kSeed);
    siamese::PCGRandom prngPacketData;
    prngPacketData.Seed(kSeed + 2);
    siamese::PCGRandom prngLength;
    prngLength.Seed(kSeed + 1);

    // Number of packets
    static const unsigned kLastPacket = 1000000;

    static const unsigned kLossRate = 10; // percent
    static const unsigned kRecoveryRate = 100 / 12;

    static const unsigned kDelayBeforeAck = 40;

    FunctionTimer t_siamese_encoder_create("siamese_encoder_create");
    FunctionTimer t_siamese_decoder_create("siamese_decoder_create");
    FunctionTimer t_siamese_encoder_add("siamese_encoder_add");
    FunctionTimer t_siamese_decoder_add_original("siamese_decoder_add_original");
    FunctionTimer t_siamese_encode("siamese_encode");
    FunctionTimer t_siamese_decoder_add_recovery("siamese_decoder_add_recovery");
    FunctionTimer t_siamese_decoder_is_ready("siamese_decoder_is_ready");
    FunctionTimer t_siamese_decode("siamese_decode");
    FunctionTimer t_siamese_encoder_remove("siamese_encoder_remove");

    unsigned overheadCount = 0;
    unsigned lostOriginalCount = 0;

    t_siamese_encoder_create.BeginCall();
    SiameseEncoder encoder = siamese_encoder_create();
    t_siamese_encoder_create.EndCall();

    if (!encoder)
    {
        Logger.Error("Unable to create encoder");
        SIAMESE_DEBUG_BREAK();
        return;
    }

    t_siamese_decoder_create.BeginCall();
    SiameseDecoder decoder = siamese_decoder_create();
    t_siamese_decoder_create.EndCall();

    if (!decoder)
    {
        Logger.Error("Unable to create decoder");
        SIAMESE_DEBUG_BREAK();
        return;
    }

    unsigned NextExpectedPacket = 0;
    unsigned PacketId = 0;

    for (unsigned loopCount = 0;; ++loopCount)
    {
        uint8_t originalPacket[2000];
        unsigned originalBytes = GetPacketBytes(PacketId);
        SetPacket(PacketId, originalPacket, originalBytes);

        SiameseOriginalPacket original;
        original.Data = originalPacket;
        original.DataBytes = originalBytes;

        {
            t_siamese_encoder_add.BeginCall();
            int result = siamese_encoder_add(encoder, &original);
            t_siamese_encoder_add.EndCall();
            if (result)
            {
                Logger.Error("Unable to add original data to encoder. Note overhead count = ", overheadCount, " and total loss = ", lostOriginalCount);
                SIAMESE_DEBUG_BREAK();
                return;
            }
            SIAMESE_DEBUG_ASSERT(original.PacketNum == PacketId);
            PacketId++;
        }

        bool Lost = (prngLoss.Next() % 100) < kLossRate;

        if (!Lost)
        {
            // If this is the next packet in sequence:
            if (original.PacketNum == NextExpectedPacket)
            {
                NextExpectedPacket = SIAMESE_PACKET_NUM_INC(NextExpectedPacket);
                Logger.Trace("Received in sequence: ", original.PacketNum);
                if (NextExpectedPacket == kLastPacket)
                    goto DoneDecoding;
            }

            t_siamese_decoder_add_original.BeginCall();
            int result = siamese_decoder_add_original(decoder, &original);
            t_siamese_decoder_add_original.EndCall();
            if (result)
            {
                Logger.Error("Unable to add original data to decoder");
                SIAMESE_DEBUG_BREAK();
                return;
            }
        }
        else
        {
            Logger.Trace("** Lost ", original.PacketNum);
            ++lostOriginalCount;
        }

        bool TimeToSendRecoveryPacket = (loopCount % kRecoveryRate == 0);

        if (TimeToSendRecoveryPacket)
        {
            SiameseRecoveryPacket recovery;

            t_siamese_encode.BeginCall();
            int result = siamese_encode(encoder, &recovery);
            t_siamese_encode.EndCall();
            if (result)
            {
                // If there was no data to encode:
                if (result == Siamese_NeedMoreData)
                    continue;

                Logger.Error("Unable to generate encoded data");
                SIAMESE_DEBUG_BREAK();
                return;
            }

            bool LostRecovery = (prngLoss.Next() % 100) < 5;

            if (!LostRecovery)
            {
                t_siamese_decoder_add_recovery.BeginCall();
                int result = siamese_decoder_add_recovery(decoder, &recovery);
                t_siamese_decoder_add_recovery.EndCall();
                if (result)
                {
                    Logger.Error("Unable add recovery data to encoder");
                    SIAMESE_DEBUG_BREAK();
                    return;
                }

                for (;;)
                {
                    t_siamese_decoder_is_ready.BeginCall();
                    int readyResult = siamese_decoder_is_ready(decoder);
                    t_siamese_decoder_is_ready.EndCall();

                    if (readyResult)
                    {
                        SIAMESE_DEBUG_ASSERT(readyResult == Siamese_NeedMoreData);
                        break;
                    }

                    SiameseOriginalPacket* packets;
                    unsigned packetCount = 0;

                    t_siamese_decode.BeginCall();
                    int decodeResult = siamese_decode(decoder, &packets, &packetCount);
                    t_siamese_decode.EndCall();

                    if (decodeResult == Siamese_Success)
                    {
                        for (unsigned i = 0; i < packetCount; ++i)
                        {
                            if (!CheckPacket(packets[i].PacketNum, packets[i].Data, packets[i].DataBytes))
                            {
                                Logger.Error("Corrupted data after decode");
                                SIAMESE_DEBUG_BREAK();
                                return;
                            }

                            const unsigned packetNum = packets[i].PacketNum;

                            // If this is the next packet in sequence:
                            if (packetNum == NextExpectedPacket)
                            {
                                NextExpectedPacket = SIAMESE_PACKET_NUM_INC(NextExpectedPacket);
                                Logger.Trace("Recovered in sequence: ", packetNum);
                                if (NextExpectedPacket == kLastPacket)
                                    goto DoneDecoding;

                                for (;;)
                                {
                                    SiameseOriginalPacket original;
                                    original.PacketNum = NextExpectedPacket;

                                    t_siamese_decode.BeginCall();
                                    int getResult = siamese_decoder_get(decoder, &original);
                                    t_siamese_decode.EndCall();
                                    if (getResult == Siamese_Success)
                                    {
                                        NextExpectedPacket = SIAMESE_PACKET_NUM_INC(NextExpectedPacket);
                                        Logger.Trace("Resumed sequence: ", original.PacketNum);
                                        if (!CheckPacket(original.PacketNum, original.Data, original.DataBytes))
                                        {
                                            Logger.Error("Corrupted data after decode2");
                                            SIAMESE_DEBUG_BREAK();
                                            return;
                                        }

                                        if (NextExpectedPacket == kLastPacket)
                                            goto DoneDecoding;

                                        continue;
                                    }

                                    break;
                                }
                            }
                            else
                            {
                                Logger.Trace("Recovered out of sequence : ", packetNum);
                            }
                        }
                    }
                    else if (decodeResult == Siamese_NeedMoreData)
                    {
                        Logger.Trace("** Recovery failed and needs more data (rare)");
                        ++overheadCount;
                    }
                    else
                    {
                        Logger.Error("Unexpected decode result code ", decodeResult);
                        SIAMESE_DEBUG_BREAK();
                        return;
                    }
                }
            }
        }

        if ((unsigned)(original.PacketNum - NextExpectedPacket) >= kDelayBeforeAck &&
            (unsigned)(original.PacketNum - NextExpectedPacket) < SIAMESE_PACKET_NUM_COUNT / 2)
        {
            Logger.Trace("<<< Back-channel <<< Simulating ACK: Waiting for ", NextExpectedPacket);

            // Simulate ack to sender
            t_siamese_encoder_remove.BeginCall();
            int result = siamese_encoder_remove_before(encoder, NextExpectedPacket);
            t_siamese_encoder_remove.EndCall();
            if (result)
            {
                Logger.Error("Unable to remove from encoder");
                SIAMESE_DEBUG_BREAK();
                return;
            }
        }
    }

DoneDecoding:
    // Flush the log so we do not miss the last part
    logger::Flush();

    Logger.Info("Streaming completed:");

    siamese_encoder_free(encoder);
    siamese_decoder_free(decoder);

    static const unsigned kTrials = 1;

    t_siamese_encoder_create.Print(kTrials);
    t_siamese_encoder_add.Print(kTrials);
    t_siamese_encoder_remove.Print(kTrials);
    t_siamese_encode.Print(kTrials);

    t_siamese_decoder_create.Print(kTrials);
    t_siamese_decoder_add_original.Print(kTrials);
    t_siamese_decoder_add_recovery.Print(kTrials);
    t_siamese_decoder_is_ready.Print(kTrials);
    t_siamese_decode.Print(kTrials);

    const float packetOverhead = ((lostOriginalCount + overheadCount) / (float)lostOriginalCount - 1.f);

    Logger.Info("Code inefficiency summary: Failed to recover ", overheadCount, " times for ",
        lostOriginalCount, " lost packets. Average overhead: ", packetOverhead, " packets");
}


class HARQSimulation
{
    enum DataHeaderTypes
    {
        OpCode_Data,
        OpCode_Recovery,
        OpCode_Ack
    };

    // Number of packets
    static const unsigned kLastPacket = 20000; // Something longer than Siamese max 16K packets to make sure that is tested

    // Channel character
    static const unsigned kLossPercent = 3;

    // FEC parameters
    static const unsigned kRedundancyPercent = 6;

    // Bandwidth
    static const unsigned kPacketsPerInterval = 4;

    // Latency
    static const unsigned kPacketIntervalMsec = 4;
    static const unsigned kQueueDepth = 10;

    siamese::PCGRandom prngLoss;
    siamese::PCGRandom prngPacketData;
    siamese::PCGRandom prngLength;

    FunctionTimer t_siamese_encoder_create;
    FunctionTimer t_siamese_decoder_create;
    FunctionTimer t_siamese_encoder_add;
    FunctionTimer t_siamese_decoder_add_original;
    FunctionTimer t_siamese_encode;
    FunctionTimer t_siamese_decoder_add_recovery;
    FunctionTimer t_siamese_decoder_is_ready;
    FunctionTimer t_siamese_decode;
    FunctionTimer t_siamese_encoder_remove;
    FunctionTimer t_siamese_encoder_ack;
    FunctionTimer t_siamese_decoder_ack;
    FunctionTimer t_siamese_encoder_retransmit;

    struct Counts
    {
        unsigned Sent = 0, Received = 0, Lost = 0;

        uint64_t BytesSent = 0, BytesReceived = 0;
    };

    Counts Originals, Acks, Recoveries;

    unsigned RetransmitCount = 0;
    uint64_t RetransmitBytes = 0;

    unsigned RecoveryFailCount = 0;
    unsigned DuplicateOriginalsReceived = 0;
    unsigned RecoverySuccessCount = 0;
    unsigned RecoveredPacketCount = 0;

    SiameseEncoder encoder;
    SiameseDecoder decoder;

    unsigned NextExpectedPacket = 0;
    unsigned NextSendPacketId = 0;

    std::vector<uint64_t> Timestamps;
    std::vector<unsigned> UsecDeltas;

    // Simulate a delay over a network
    struct QueuedPacket
    {
        uint8_t Data[2000];
        unsigned Bytes;
    };
    struct QueueRound
    {
        std::vector<QueuedPacket> Packets;
    };
    std::queue<QueueRound> c2sRounds, s2cRounds;

    /*
        Client is sending video data to server (simulation)
        c2s = video data + recovery data (from encoder)
        s2c = acks (from decoder)
    */
    QueueRound c2sRound, s2cRound;

    unsigned loopCount = 0;

    bool UnrecoverableError = false;

public:
    HARQSimulation();
    void Run(unsigned seed);

    void ClientSendNewVideoData();
    void ClientReceiveData();
    bool ClientRetransmitData();
    void ClientSendRecoveryData();

    void ServerReceiveData();
    void ServerSendAck();
    void ServerOnOriginal(SiameseOriginalPacket& original);
    void ServerOnRecovery(SiameseRecoveryPacket& recovery);
    void ServerCheckRecovery();

    void ServerResumeProcessing();
    void ServerSimulateProcessingOriginal(SiameseOriginalPacket& original);
};

HARQSimulation::HARQSimulation()
    : t_siamese_encoder_create("siamese_encoder_create")
    , t_siamese_decoder_create("siamese_decoder_create")
    , t_siamese_encoder_add("siamese_encoder_add")
    , t_siamese_decoder_add_original("siamese_decoder_add_original")
    , t_siamese_encode("siamese_encode")
    , t_siamese_decoder_add_recovery("siamese_decoder_add_recovery")
    , t_siamese_decoder_is_ready("siamese_decoder_is_ready")
    , t_siamese_decode("siamese_decode")
    , t_siamese_encoder_remove("siamese_encoder_remove")
    , t_siamese_encoder_ack("siamese_encoder_ack")
    , t_siamese_decoder_ack("siamese_decoder_ack")
    , t_siamese_encoder_retransmit("siamese_encoder_retransmit")
{
}

void HARQSimulation::ClientSendNewVideoData()
{
    QueuedPacket newData;

    newData.Bytes = GetPacketBytes(NextSendPacketId);

    Originals.Sent++;
    Originals.BytesSent += newData.Bytes;

    SetPacket(NextSendPacketId, newData.Data + 1 + 4, newData.Bytes);

    SiameseOriginalPacket original;
    original.Data = newData.Data + 1 + 4;
    original.DataBytes = newData.Bytes;
    newData.Bytes += 1 + 4;

    t_siamese_encoder_add.BeginCall();
    int result = siamese_encoder_add(encoder, &original);
    t_siamese_encoder_add.EndCall();
    if (result)
    {
        UnrecoverableError = true;
        Logger.Error("Unable to add original data to encoder");
        SIAMESE_DEBUG_BREAK();
        return;
    }
    SIAMESE_DEBUG_ASSERT(NextSendPacketId == original.PacketNum);
    SIAMESE_DEBUG_ASSERT(Timestamps.size() == NextSendPacketId);
    ++NextSendPacketId;
    Timestamps.push_back(siamese::GetTimeUsec());

    newData.Data[0] = OpCode_Data;
    siamese::WriteU32_LE(newData.Data + 1, original.PacketNum);

    c2sRound.Packets.push_back(newData);
}

void HARQSimulation::ClientReceiveData()
{
    QueueRound nextS2C = s2cRounds.front();
    s2cRounds.pop();

    for (auto& s2cPacket : nextS2C.Packets)
    {
        switch (s2cPacket.Data[0])
        {
        case OpCode_Ack:
            if ((prngLoss.Next() % 100) >= kLossPercent)
            {
                Logger.Trace("** Got ACK");

                Acks.Received++;
                Acks.BytesReceived += s2cPacket.Bytes - 1;

                t_siamese_encoder_ack.BeginCall();
                unsigned nextExpectedAck;
                int result = siamese_encoder_ack(encoder, s2cPacket.Data + 1, s2cPacket.Bytes - 1, &nextExpectedAck);
                t_siamese_encoder_ack.EndCall();
                if (result)
                {
                    UnrecoverableError = true;
                    Logger.Error("Encoder decided ack data was malformed");
                    SIAMESE_DEBUG_BREAK();
                    return;
                }
            }
            else
            {
                Logger.Trace("** Lost ACK");
                Acks.Lost++;
            }
            break;
        case OpCode_Data:
        case OpCode_Recovery:
        default:
            UnrecoverableError = true;
            Logger.Error("Invalid s2c protocol opcode");
            SIAMESE_DEBUG_BREAK();
            return;
        }
    }
}

void HARQSimulation::ServerReceiveData()
{
    QueueRound nextC2S = c2sRounds.front();
    c2sRounds.pop();

    Logger.Trace("*** Processing ", nextC2S.Packets.size(), " c2s packets this round");

    for (auto& c2sPacket : nextC2S.Packets)
    {
        switch (c2sPacket.Data[0])
        {
        case OpCode_Data:
            if ((prngLoss.Next() % 100) >= kLossPercent)
            {
                // Note: Also counts retransmits
                Originals.Received++;
                Originals.BytesReceived += c2sPacket.Bytes - 1 - 4;

                SiameseOriginalPacket original;
                original.PacketNum = siamese::ReadU32_LE(c2sPacket.Data + 1);
                Logger.Trace("** Got Original: ", original.PacketNum);
                original.Data = c2sPacket.Data + 1 + 4;
                original.DataBytes = c2sPacket.Bytes - 1 - 4;
                ServerOnOriginal(original);
            }
            else
            {
                uint32_t packetNum = siamese::ReadU32_LE(c2sPacket.Data + 1);
                Logger.Trace("** Lost Data ", packetNum);
                Originals.Lost++;
            }
            break;
        case OpCode_Recovery:
            if ((prngLoss.Next() % 100) >= kLossPercent)
            {
                Logger.Trace("** Got Recovery");
                Recoveries.Received++;
                Recoveries.BytesReceived += c2sPacket.Bytes - 1;

                SiameseRecoveryPacket recovery;
                recovery.Data = c2sPacket.Data + 1;
                recovery.DataBytes = c2sPacket.Bytes - 1;
                ServerOnRecovery(recovery);
            }
            else
            {
                Logger.Trace("** Lost Recovery");
                Recoveries.Lost++;
            }
            break;
        case OpCode_Ack:
        default:
            UnrecoverableError = true;
            Logger.Error("Invalid c2s protocol opcode");
            SIAMESE_DEBUG_BREAK();
            return;
        }
    }
}

bool HARQSimulation::ClientRetransmitData()
{
    SiameseOriginalPacket original;

    t_siamese_encoder_retransmit.BeginCall();
    int result = siamese_encoder_retransmit(encoder, &original);
    t_siamese_encoder_retransmit.EndCall();

    if (result == Siamese_Success)
    {
#ifdef HARQ_RETRANSMIT_WITH_FEC
        SiameseRecoveryPacket recovery;

        t_siamese_encode.BeginCall();
        int result = siamese_encode(encoder, &recovery);
        t_siamese_encode.EndCall();
        if (result)
        {
            // If there was no data to encode:
            if (result != Siamese_NeedMoreData)
            {
                UnrecoverableError = true;
                Logger.Error("Unable to generate encoded data: ", result);
                SIAMESE_DEBUG_BREAK();
                return false;
            }
        }

        ++Recoveries.Sent;
        Recoveries.BytesSent += recovery.DataBytes;

        QueuedPacket newData;
        newData.Data[0] = OpCode_Recovery;
        memcpy(newData.Data + 1, recovery.Data, recovery.DataBytes);
        newData.Bytes = recovery.DataBytes + 1;

        c2sRound.Packets.push_back(newData);
#else
        Logger.Trace("Retransmitted : ", original.PacketNum);

        QueuedPacket newData;
        newData.Data[0] = OpCode_Data;
        siamese::WriteU32_LE(newData.Data + 1, original.PacketNum);
        memcpy(newData.Data + 1 + 4, original.Data, original.DataBytes);
        newData.Bytes = 1 + 4 + original.DataBytes;

        c2sRound.Packets.push_back(newData);

        ++RetransmitCount;
        RetransmitBytes += original.DataBytes;

        // Note: Also counts as original data sent
        Originals.Sent++;
        Originals.BytesSent += newData.Bytes;
#endif

        return true;
    }

    // If there was no data to retransmit:
    if (result != Siamese_NeedMoreData)
    {
        UnrecoverableError = true;
        Logger.Error("Unexpected error result from encoder retransmit: ", result);
        SIAMESE_DEBUG_BREAK();
        return false;
    }

    return false;
}

void HARQSimulation::ClientSendRecoveryData()
{
    if (Recoveries.Sent * 100.f / (float)Originals.Sent >= kRedundancyPercent)
        return;

    SiameseRecoveryPacket recovery;

    t_siamese_encode.BeginCall();
    int result = siamese_encode(encoder, &recovery);
    t_siamese_encode.EndCall();
    if (result)
    {
        // If there was no data to encode:
        if (result != Siamese_NeedMoreData)
        {
            UnrecoverableError = true;
            Logger.Error("Unable to generate encoded data: ", result);
            SIAMESE_DEBUG_BREAK();
            return;
        }
    }

    ++Recoveries.Sent;
    Recoveries.BytesSent += recovery.DataBytes;

    QueuedPacket newData;
    newData.Data[0] = OpCode_Recovery;
    memcpy(newData.Data + 1, recovery.Data, recovery.DataBytes);
    newData.Bytes = recovery.DataBytes + 1;

    c2sRound.Packets.push_back(newData);
}

void HARQSimulation::ServerSendAck()
{
    // TCP sends ACK every other packet
    bool TimeToSendAckPacket = (loopCount % 2 == 0);
    if (!TimeToSendAckPacket)
        return;

    QueuedPacket newData;
    newData.Data[0] = OpCode_Ack;

    unsigned usedBytes = 0;

    t_siamese_decoder_ack.BeginCall();
    int result = siamese_decoder_ack(decoder, newData.Data + 1, (unsigned)sizeof(newData.Data), &usedBytes);
    t_siamese_decoder_ack.EndCall();
    if (result)
    {
        // If there was no data to encode:
        if (result != Siamese_NeedMoreData)
        {
            UnrecoverableError = true;
            Logger.Error("Unable to generate decoder acknowledgement message: ", result);
            SIAMESE_DEBUG_BREAK();
            return;
        }
    }

    newData.Bytes = 1 + usedBytes;

    s2cRound.Packets.push_back(newData);

    ++Acks.Sent;
    Acks.BytesSent += usedBytes;
}

void HARQSimulation::ServerResumeProcessing()
{
    for (;;)
    {
        SiameseOriginalPacket original;
        original.PacketNum = NextExpectedPacket;

        t_siamese_decode.BeginCall();
        int getResult = siamese_decoder_get(decoder, &original);
        t_siamese_decode.EndCall();

        if (getResult != Siamese_Success)
            break;

        ServerSimulateProcessingOriginal(original);
    }
}

void HARQSimulation::ServerCheckRecovery()
{
    for (;;)
    {
        t_siamese_decoder_is_ready.BeginCall();
        int readyResult = siamese_decoder_is_ready(decoder);
        t_siamese_decoder_is_ready.EndCall();

        if (readyResult)
        {
            SIAMESE_DEBUG_ASSERT(readyResult == Siamese_NeedMoreData);
            break;
        }

        SiameseOriginalPacket* recoveredOriginals;
        unsigned recoveredOriginalCount;

        // Attempt to decode
        // Note we do not request the list of decoded packets because we only process
        // data in order.  Protocols that process data out of order may find that useful
        t_siamese_decode.BeginCall();
        int decodeResult = siamese_decode(decoder, &recoveredOriginals, &recoveredOriginalCount);
        t_siamese_decode.EndCall();

        if (decodeResult == Siamese_Success)
        {
            // Resume processing data
            ServerResumeProcessing();

            ++RecoverySuccessCount;
            RecoveredPacketCount += recoveredOriginalCount;
        }
        else if (decodeResult == Siamese_NeedMoreData)
        {
            Logger.Trace("** Recovery failed and needs more data (rare)");
            ++RecoveryFailCount;
        }
        else
        {
            UnrecoverableError = true;
            Logger.Error("Unexpected siamese decode error result: ", decodeResult);
            SIAMESE_DEBUG_BREAK();
            return;
        }
    }
}

void HARQSimulation::ServerOnRecovery(SiameseRecoveryPacket& recovery)
{
    t_siamese_decoder_add_recovery.BeginCall();
    int result = siamese_decoder_add_recovery(decoder, &recovery);
    t_siamese_decoder_add_recovery.EndCall();

    if (result)
    {
        UnrecoverableError = true;
        Logger.Error("Unable add recovery data to encoder: ", result);
        SIAMESE_DEBUG_BREAK();
        return;
    }

    ServerCheckRecovery();
}

void HARQSimulation::ServerSimulateProcessingOriginal(SiameseOriginalPacket& original)
{
    if (!CheckPacket(original.PacketNum, original.Data, original.DataBytes))
    {
        UnrecoverableError = true;
        Logger.Error("Data was corrupted");
        SIAMESE_DEBUG_BREAK();
        return;
    }

    // If this is the next packet in sequence:
    if (original.PacketNum != NextExpectedPacket)
    {
        UnrecoverableError = true;
        Logger.Error("Received data out of order");
        SIAMESE_DEBUG_BREAK();
        return;
    }

    SIAMESE_DEBUG_ASSERT(Timestamps.size() > original.PacketNum);
    const uint64_t tsUsec = Timestamps[original.PacketNum];
    const uint64_t nowUsec = siamese::GetTimeUsec();
    SIAMESE_DEBUG_ASSERT(nowUsec > tsUsec);
    const unsigned deltaUsec = (unsigned)(nowUsec - tsUsec);

    UsecDeltas.push_back(deltaUsec);

    NextExpectedPacket = SIAMESE_PACKET_NUM_INC(NextExpectedPacket);
    Logger.Trace("Received in sequence: ", original.PacketNum, " OWD = ", deltaUsec);

    // Note: Do not call any decoder functions here
    // because the original packet hasn't been added yet
}

void HARQSimulation::ServerOnOriginal(SiameseOriginalPacket& original)
{
    // If this is the next packet in sequence:
    if (original.PacketNum == NextExpectedPacket)
    {
        ServerSimulateProcessingOriginal(original);
    }

    t_siamese_decoder_add_original.BeginCall();
    int result = siamese_decoder_add_original(decoder, &original);
    t_siamese_decoder_add_original.EndCall();
    if (result == Siamese_Success)
    {
        // Process any data we can without recovery
        ServerResumeProcessing();

        // Check if we can recover now
        ServerCheckRecovery();
    }
    else if (result == Siamese_DuplicateData)
    {
        Logger.Trace("Received duplicated data: ", original.PacketNum);
        ++DuplicateOriginalsReceived;
    }
    else
    {
        UnrecoverableError = true;
        Logger.Error("Unable to add original data to decoder");
        SIAMESE_DEBUG_BREAK();
        return;
    }
}

void HARQSimulation::Run(unsigned seed)
{
    prngLoss.Seed(seed);
    prngPacketData.Seed(seed + 2);
    prngLength.Seed(seed + 1);

    t_siamese_encoder_create.BeginCall();
    encoder = siamese_encoder_create();
    t_siamese_encoder_create.EndCall();

    if (!encoder)
    {
        UnrecoverableError = true;
        Logger.Error("Unable to create encoder");
        SIAMESE_DEBUG_BREAK();
        return;
    }

    t_siamese_decoder_create.BeginCall();
    decoder = siamese_decoder_create();
    t_siamese_decoder_create.EndCall();

    if (!decoder)
    {
        UnrecoverableError = true;
        Logger.Error("Unable to create decoder");
        SIAMESE_DEBUG_BREAK();
        return;
    }

    NextExpectedPacket = 0;

    while (!c2sRounds.empty())
        c2sRounds.pop();
    while (!s2cRounds.empty())
        s2cRounds.pop();

    c2sRound.Packets.clear();
    s2cRound.Packets.clear();

    uint64_t simStartMsec = siamese::GetTimeMsec();

    for (loopCount = 0;; ++loopCount, c2sRounds.push(c2sRound), s2cRounds.push(s2cRound))
    {
        if (UnrecoverableError)
        {
            Logger.Error("Aborting simulation on unrecoverable error");
            break;
        }

        if (NextExpectedPacket >= kLastPacket)
        {
            Logger.Info("Ending simulation after ", NextExpectedPacket, " packets were received");
            break;
        }

        ::Sleep(kPacketIntervalMsec); // ms between rounds

        // TODO: Reordering

        c2sRound.Packets.clear(), s2cRound.Packets.clear();

        for (unsigned i = 0; i < kPacketsPerInterval; ++i)
        {
            const bool retransmitted = ClientRetransmitData();

            // Check if we should retransmit data every round (c2s)
            if (!retransmitted)
            {
                // Generate new e.g. video original data periodically
                ClientSendNewVideoData();
            }

            // FEC redundant data packets (c2s)
            ClientSendRecoveryData();
        }

        // Start by queuing up a bunch of original data packets
        if (loopCount < (size_t)kQueueDepth)
        {
            Logger.Trace("Building up initial pipe queue...");
            continue;
        }

        // This is the s2c back-channel: Acks are received here from "server"
        ClientReceiveData();

        // This is the c2s forward channel: Data/recovery packets are received here from "client"
        ServerReceiveData();

        // Server sends acknowledgement of data (s2c)
        ServerSendAck();
    }

    uint64_t simEndMsec = siamese::GetTimeMsec();

    // Flush the log so we do not miss the last part
    logger::Flush();

    Logger.Info("Streaming completed:");

    const uint64_t TotalBytesSent = Originals.BytesSent + Acks.BytesSent + Recoveries.BytesSent;
    const uint64_t TotalBytesReceived = Originals.BytesReceived + Acks.BytesReceived + Recoveries.BytesReceived;

    Logger.Info("Total bytes sent: ", TotalBytesSent);

    const uint64_t SimTimeMsec = simEndMsec - simStartMsec;

    static const unsigned OriginalDataSizeBytes = kLastPacket * 1200; // bytes

    Logger.Info("Total time taken: ", SimTimeMsec, " msec. Speed = ", OriginalDataSizeBytes / (double)SimTimeMsec, " KBPS");

    Logger.Info("Originals sent: ", Originals.Sent, " recv: ", Originals.Received, " lost: ", Originals.Lost, " = ", Originals.Lost * 100.f / (float)Originals.Sent, "% loss");
    Logger.Info("Originals sent bytes: ", Originals.BytesSent, " recv bytes: ", Originals.BytesReceived, " = ", Originals.BytesSent * 100.f / (float)TotalBytesSent, "% of total sent bytes");

    Logger.Info("Acks sent: ", Acks.Sent, " recv: ", Acks.Received, " lost: ", Acks.Lost, " = ", Acks.Lost * 100.f / (float)Acks.Sent, "% loss");
    Logger.Info("Acks sent bytes: ", Acks.BytesSent, " recv bytes: ", Acks.BytesReceived, " = ", Acks.BytesSent * 100.f / (float)TotalBytesSent, "% of total sent bytes");

    Logger.Info("Recoveries sent: ", Recoveries.Sent, " recv: ", Recoveries.Received, " lost: ", Recoveries.Lost, " = ", Recoveries.Lost * 100.f / (float)Recoveries.Sent, "% loss");
    Logger.Info("Recoveries sent bytes: ", Recoveries.BytesSent, " recv bytes: ", Recoveries.BytesReceived, " = ", Recoveries.BytesSent * 100.f / (float)TotalBytesSent, "% of total sent bytes");

    Logger.Info("RetransmitCount = ", RetransmitCount, " retransmit bytes = ", RetransmitBytes, " = ", RetransmitBytes * 100.f / (float)TotalBytesSent, "% of total sent bytes");

    Logger.Info("DuplicateOriginalsReceived = ", DuplicateOriginalsReceived);

    Logger.Info("RecoverySuccessCount = ", RecoverySuccessCount);
    Logger.Info("RecoveredPacketCount = ", RecoveredPacketCount);
    Logger.Info("RecoveryFailCount = ", RecoveryFailCount, " = ", RecoveryFailCount * 100.f / (RecoveryFailCount + RecoverySuccessCount), "% failure rate");

    unsigned percentile1 = 0;
    if (UsecDeltas.size() > 200)
    {
        std::vector<unsigned>::size_type goalOffset = (unsigned)(0.99 * UsecDeltas.size());
        std::nth_element(UsecDeltas.begin(), UsecDeltas.begin() + goalOffset, UsecDeltas.end(),
            [](unsigned a, unsigned b)->bool {
            return a < b;
        });
        percentile1 = UsecDeltas[goalOffset];
    }

    unsigned percentile5 = 0;
    if (UsecDeltas.size() > 100)
    {
        std::vector<unsigned>::size_type goalOffset = (unsigned)(0.95 * UsecDeltas.size());
        std::nth_element(UsecDeltas.begin(), UsecDeltas.begin() + goalOffset, UsecDeltas.end(),
            [](unsigned a, unsigned b)->bool {
            return a < b;
        });
        percentile5 = UsecDeltas[goalOffset];
    }

    unsigned percentile25 = 0;
    if (UsecDeltas.size() > 4)
    {
        std::vector<unsigned>::size_type goalOffset = (unsigned)(0.75 * UsecDeltas.size());
        std::nth_element(UsecDeltas.begin(), UsecDeltas.begin() + goalOffset, UsecDeltas.end(),
            [](unsigned a, unsigned b)->bool {
            return a < b;
        });
        percentile25 = UsecDeltas[goalOffset];
    }

    unsigned percentile50 = 0;
    if (UsecDeltas.size() > 4)
    {
        std::vector<unsigned>::size_type goalOffset = (unsigned)(0.5 * UsecDeltas.size());
        std::nth_element(UsecDeltas.begin(), UsecDeltas.begin() + goalOffset, UsecDeltas.end(),
            [](unsigned a, unsigned b)->bool {
            return a < b;
        });
        percentile50 = UsecDeltas[goalOffset];
    }

    unsigned percentile75 = 0;
    if (UsecDeltas.size() > 8)
    {
        std::vector<unsigned>::size_type goalOffset = (unsigned)(0.25 * UsecDeltas.size());
        std::nth_element(UsecDeltas.begin(), UsecDeltas.begin() + goalOffset, UsecDeltas.end(),
            [](unsigned a, unsigned b)->bool {
            return a < b;
        });
        percentile75 = UsecDeltas[goalOffset];
    }

    unsigned percentile95 = 0;
    if (UsecDeltas.size() > 40)
    {
        std::vector<unsigned>::size_type goalOffset = (unsigned)(0.05 * UsecDeltas.size());
        std::nth_element(UsecDeltas.begin(), UsecDeltas.begin() + goalOffset, UsecDeltas.end(),
            [](unsigned a, unsigned b)->bool {
            return a < b;
        });
        percentile95 = UsecDeltas[goalOffset];
    }

    unsigned percentile99 = 0;
    if (UsecDeltas.size() > 200)
    {
        std::vector<unsigned>::size_type goalOffset = (unsigned)(0.01 * UsecDeltas.size());
        std::nth_element(UsecDeltas.begin(), UsecDeltas.begin() + goalOffset, UsecDeltas.end(),
            [](unsigned a, unsigned b)->bool {
            return a < b;
        });
        percentile99 = UsecDeltas[goalOffset];
    }

    Logger.Info("Simulated one-way  1% percentile latency = ", percentile1 / 1000.f, " msec");
    Logger.Info("Simulated one-way  5% percentile latency = ", percentile5 / 1000.f, " msec");
    Logger.Info("Simulated one-way 25% percentile latency = ", percentile25 / 1000.f, " msec");
    Logger.Info("Simulated one-way 50% percentile latency = ", percentile50 / 1000.f, " msec (median)");
    Logger.Info("Simulated one-way 75% percentile latency = ", percentile75 / 1000.f, " msec");
    Logger.Info("Simulated one-way 95% percentile latency = ", percentile95 / 1000.f, " msec");
    Logger.Info("Simulated one-way 99% percentile latency = ", percentile99 / 1000.f, " msec");

    siamese_encoder_free(encoder);
    siamese_decoder_free(decoder);

    static const unsigned kTrials = 1;

    t_siamese_encoder_create.Print(kTrials);
    t_siamese_encoder_add.Print(kTrials);
    t_siamese_encoder_remove.Print(kTrials);
    t_siamese_encode.Print(kTrials);

    t_siamese_decoder_create.Print(kTrials);
    t_siamese_decoder_add_original.Print(kTrials);
    t_siamese_decoder_add_recovery.Print(kTrials);
    t_siamese_decoder_is_ready.Print(kTrials);
    t_siamese_decode.Print(kTrials);
}


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


// This test verifies that the kMaximumLossRecoveryCount logic works properly:
// It should only use up to kMaximumLossRecoveryCount recovery packets to attempt recovery.
// This test will pick 255 * 2 random losses in a set of 1000 packets, and then provide 255 * 2 recovery packets.
// Then lost original packets are provided out of order and recovery is attempted each time.
// The expectation is that it will recover after 255 packets most of the time, and sometimes 256.
// And that's what happens!
bool TestLargeBurstLoss()
{
    Logger.Info("Test: TestLargeBurstLoss");

    FunctionTimer t_siamese_encoder_create("siamese_encoder_create");
    FunctionTimer t_siamese_decoder_create("siamese_decoder_create");
    FunctionTimer t_siamese_encoder_add("siamese_encoder_add");
    FunctionTimer t_siamese_decoder_add_original("siamese_decoder_add_original");
    FunctionTimer t_siamese_encode("siamese_encode");
    FunctionTimer t_siamese_decoder_add_recovery("siamese_decoder_add_recovery");
    FunctionTimer t_siamese_decoder_is_ready("siamese_decoder_is_ready");
    FunctionTimer t_siamese_decode("siamese_decode");

    static const int N = 1000;
    static const int K = 255;

    static const unsigned kTrials = 999;

    for (unsigned trial = 0; trial < kTrials; ++trial)
    {
        uint16_t losses[N];
        siamese::PCGRandom prng;
        prng.Seed(kSeed, trial);
        ShuffleDeck16(prng, losses, N);

        t_siamese_encoder_create.BeginCall();
        SiameseEncoder encoder = siamese_encoder_create();
        t_siamese_encoder_create.EndCall();

        if (!encoder)
        {
            Logger.Error("Unable to create encoder");
            SIAMESE_DEBUG_BREAK();
            return false;
        }

        t_siamese_decoder_create.BeginCall();
        SiameseDecoder decoder = siamese_decoder_create();
        t_siamese_decoder_create.EndCall();

        if (!decoder)
        {
            Logger.Error("Unable to create decoder");
            SIAMESE_DEBUG_BREAK();
            return false;
        }

        unsigned decoderReceiveCount = 0;

        for (int i = 0; i < N; ++i)
        {
            uint8_t buffer[2000];
            unsigned bytes = GetPacketBytes(i);
            SIAMESE_DEBUG_ASSERT(bytes <= sizeof(buffer));
            SetPacket(i, buffer, bytes);

            SiameseOriginalPacket original;
            original.Data = buffer;
            original.DataBytes = bytes;
            {
                t_siamese_encoder_add.BeginCall();
                int result = siamese_encoder_add(encoder, &original);
                t_siamese_encoder_add.EndCall();
                if (result)
                {
                    Logger.Error("Unable to add original data to encoder");
                    SIAMESE_DEBUG_BREAK();
                    return false;
                }
            }

            // Lose 255 * 2 packets
            bool lost = false;
            for (unsigned j = 0; j < 255 * 2; ++j)
            {
                if (losses[j] == i)
                {
                    lost = true;
                    break;
                }
            }

            if (!lost)
            {
                t_siamese_decoder_add_original.BeginCall();
                int result = siamese_decoder_add_original(decoder, &original);
                t_siamese_decoder_add_original.EndCall();
                if (result)
                {
                    Logger.Error("Unable to add original data to decoder");
                    SIAMESE_DEBUG_BREAK();
                    return false;
                }
                ++decoderReceiveCount;
            }
        }

        // Add 255 recovery packets
        for (int j = 0; j < 255; ++j)
        {
            SiameseRecoveryPacket recovery;

            {
                t_siamese_encode.BeginCall();
                int result = siamese_encode(encoder, &recovery);
                t_siamese_encode.EndCall();
                if (result)
                {
                    SIAMESE_DEBUG_BREAK();
                    Logger.Error("Unable to generate encoded data");
                    return false;
                }
            }

            {
                t_siamese_decoder_add_recovery.BeginCall();
                int result = siamese_decoder_add_recovery(decoder, &recovery);
                t_siamese_decoder_add_recovery.EndCall();
                if (result)
                {
                    Logger.Error("Unable to add recovery data to decoder");
                    SIAMESE_DEBUG_BREAK();
                    return false;
                }
            }
        }

        for (int j = 0; j < N; ++j)
        {
            unsigned packetId = losses[j];

            uint8_t buffer[2000];
            unsigned bytes = GetPacketBytes(packetId);
            SIAMESE_DEBUG_ASSERT(bytes <= sizeof(buffer));
            SetPacket(packetId, buffer, bytes);

            SiameseOriginalPacket original;
            original.Data = buffer;
            original.DataBytes = bytes;
            original.PacketNum = packetId;

            t_siamese_decoder_add_original.BeginCall();
            int result = siamese_decoder_add_original(decoder, &original);
            t_siamese_decoder_add_original.EndCall();
            if (result)
            {
                Logger.Error("Unable to add original data to decoder");
                SIAMESE_DEBUG_BREAK();
                return false;
            }
            ++decoderReceiveCount;

            for (;;)
            {
                t_siamese_decoder_is_ready.BeginCall();
                int readyResult = siamese_decoder_is_ready(decoder);
                t_siamese_decoder_is_ready.EndCall();

                if (readyResult)
                {
                    SIAMESE_DEBUG_ASSERT(readyResult == Siamese_NeedMoreData);
                    break;
                }

                SiameseOriginalPacket* packets = nullptr;
                unsigned packetCount = 0;

                t_siamese_decode.BeginCall();
                int decodeResult = siamese_decode(decoder, &packets, &packetCount);
                t_siamese_decode.EndCall();

                if (decodeResult == Siamese_Success)
                {
                    //cout << "Successful decode: ";
                    for (unsigned i = 0; i < packetCount; ++i)
                    {
                        //cout << packets[i].PacketNum << " ";

                        if (!CheckPacket(packets[i].PacketNum, packets[i].Data, packets[i].DataBytes))
                        {
                            Logger.Error("Packet check failed for ", i, ".DataBytes = ", packets[i].DataBytes);
                            SIAMESE_DEBUG_BREAK();
                            return false;
                        }

                        ++decoderReceiveCount;
                    }
                    //cout);

                    if (decoderReceiveCount >= N)
                    {
                        Logger.Info("Decode successful after ", j + 1, " originals - Should be around 255");
                        goto DoneDecoding;
                    }
                }
                else if (decodeResult == Siamese_NeedMoreData)
                {
                    //cout << "Needed more data to decode");
                }
                else
                {
                    Logger.Error("Decode returned ", decodeResult);
                    SIAMESE_DEBUG_BREAK();
                    return false;
                }
            }
        }

DoneDecoding:

        siamese_encoder_free(encoder);
        siamese_decoder_free(decoder);
    }

    // Flush the log so we do not miss the last part
    logger::Flush();

    Logger.Info("Test successful. Timing summary:");

    t_siamese_encoder_create.Print(kTrials);
    t_siamese_encoder_add.Print(kTrials);
    t_siamese_encode.Print(kTrials);
    t_siamese_decoder_create.Print(kTrials);
    t_siamese_decoder_add_original.Print(kTrials);
    t_siamese_decoder_add_recovery.Print(kTrials);
    t_siamese_decoder_is_ready.Print(kTrials);
    t_siamese_decode.Print(kTrials);

    return true;
}


int main()
{
    FunctionTimer t_siamese_init("siamese_init");

    t_siamese_init.BeginCall();
    if (0 != siamese_init())
    {
        Logger.Error("Failed to initialize");
        return -1;
    }
    t_siamese_init.EndCall();

    t_siamese_init.Print(1);

#ifdef TEST_LARGE_BURST_LOSS
    if (!TestLargeBurstLoss())
    {
        Logger.Error("Test failed: TestLargeBurstLoss");
        SIAMESE_DEBUG_BREAK();
        return -1;
    }
#endif
#ifdef TEST_HARQ_STREAM
    for (unsigned seed = 0;; ++seed)
    {
        HARQSimulation simulation;
        simulation.Run(seed);
    }
#endif
#ifdef TEST_STREAMING
    StreamingTest();
#endif
#ifdef TEST_BLOCK
    BlockRecoveryTest();
#endif

    getchar();

    return 0;
}
