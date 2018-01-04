/** \file
    \brief Siamese FEC Implementation: Decoder
    \copyright Copyright (c) 2017 Christopher A. Taylor.  All rights reserved.

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

#include "SiameseDecoder.h"
#include "SiameseSerializers.h"

namespace siamese {

#ifdef SIAMESE_DECODER_DUMP_VERBOSE
    static logger::Channel Logger("Decoder", logger::Level::Debug);
#else
    static logger::Channel Logger("Decoder", logger::Level::Silent);
#endif


//------------------------------------------------------------------------------
// DecoderStats

DecoderStats::DecoderStats()
{
    for (unsigned i = 0; i < SiameseDecoderStats_Count; ++i)
        Counts[i] = 0;
}


//------------------------------------------------------------------------------
// Decoder

Decoder::Decoder()
{
    RecoveryPackets.TheAllocator  = &TheAllocator;
    RecoveryPackets.CheckedRegion = &CheckedRegion;
    Window.TheAllocator           = &TheAllocator;
    Window.Stats                  = &Stats;
    Window.CheckedRegion          = &CheckedRegion;
    Window.RecoveryPackets        = &RecoveryPackets;
    Window.RecoveryMatrix         = &RecoveryMatrix;
    RecoveryMatrix.TheAllocator   = &TheAllocator;
    RecoveryMatrix.Window         = &Window;
    RecoveryMatrix.CheckedRegion  = &CheckedRegion;
    CheckedRegion.RecoveryMatrix  = &RecoveryMatrix;
}

SiameseResult Decoder::Get(SiameseOriginalPacket& packetOut)
{
    // Note: Keep this in sync with Encoder::Get

    if (Window.EmergencyDisabled)
        return Siamese_Disabled;

    // Note: This also works when Count == 0
    const unsigned element = Window.ColumnToElement(packetOut.PacketNum);
    if (Window.InvalidElement(element))
    {
        // Set default return value
        packetOut.Data      = nullptr;
        packetOut.DataBytes = 0;
        return Siamese_NeedMoreData;
    }

    // Return the packet data
    OriginalPacket* original = Window.GetWindowElement(element);
    if (original->Buffer.Bytes <= 0)
    {
        packetOut.Data      = nullptr;
        packetOut.DataBytes = 0;
        return Siamese_NeedMoreData;
    }

    const unsigned headerBytes = original->HeaderBytes;
    SIAMESE_DEBUG_ASSERT(headerBytes > 0 && original->Buffer.Bytes > headerBytes);
    const unsigned length = original->Buffer.Bytes - headerBytes;

#ifdef SIAMESE_DEBUG
    // Check: Deserialize length from the front
    unsigned lengthCheck;
    int headerBytesCheck = DeserializeHeader_PacketLength(
        original->Buffer.Data,
        original->Buffer.Bytes,
        lengthCheck);

    if (lengthCheck != length || (int)headerBytes != headerBytesCheck ||
        headerBytesCheck < 1 || lengthCheck == 0 ||
        lengthCheck + headerBytesCheck != original->Buffer.Bytes)
    {
        SIAMESE_DEBUG_BREAK(); // Invalid input
        Window.EmergencyDisabled = true;
        return Siamese_Disabled;
    }
#endif // SIAMESE_DEBUG

    packetOut.Data      = original->Buffer.Data + headerBytes;
    packetOut.DataBytes = length;
    return Siamese_Success;
}

SiameseResult Decoder::GenerateAcknowledgement(
    uint8_t* buffer,
    unsigned byteLimit,
    unsigned& usedBytesOut)
{
    if (Window.EmergencyDisabled)
        return Siamese_Disabled;

    SIAMESE_DEBUG_ASSERT(byteLimit >= SIAMESE_ACK_MIN_BYTES);

    std::ostringstream* pDebugMsg = nullptr;

    // If we have no data yet:
    const unsigned windowCount = Window.Count;
    if (windowCount == 0)
    {
        // This should only happen before we receive any data at all.
        // After we receive some data we keep a window of data around to decode FEC packets
        SIAMESE_DEBUG_ASSERT(Window.ColumnStart == 0);
        usedBytesOut = 0;
        return Siamese_NeedMoreData;
    }

    const uint8_t* bufferStart = buffer;

    // Calculate next column we expect to receive
    const unsigned nextElementExpected = Window.NextExpectedElement;
    SIAMESE_DEBUG_ASSERT(nextElementExpected <= windowCount);
    const unsigned nextColumnExpected = Window.ElementToColumn(nextElementExpected);
    unsigned headerBytes = SerializeHeader_PacketNum(nextColumnExpected, buffer);
    buffer += headerBytes, byteLimit -= headerBytes;

    // If there is no missing data:
    if (Window.InvalidElement(nextElementExpected))
    {
        // Write used bytes
        usedBytesOut = (unsigned)(uintptr_t)(buffer - bufferStart);

        Stats.Counts[SiameseDecoderStats_AckCount]++;
        Stats.Counts[SiameseDecoderStats_AckBytes] += usedBytesOut;

        return Siamese_Success;
    }

    SIAMESE_DEBUG_ASSERT(Window.GetWindowElement(nextElementExpected)->Buffer.Bytes == 0);

    // Start searching for the next set bit at the next after the next expected element
    unsigned rangeOffset = nextElementExpected;

    if (Logger.ShouldLog(logger::Level::Debug))
    {
        delete pDebugMsg;
        pDebugMsg = new std::ostringstream();
        *pDebugMsg << "Building ack from nextExpectedColumn=" << nextColumnExpected << " : NACKs = {";
    }

    // While there is room for another maximum-length loss range:
    while (byteLimit >= kMaxLossRangeFieldBytes)
    {
        const unsigned rangeStart = Window.FindNextLostElement(rangeOffset);
        if (rangeStart >= windowCount)
        {
            SIAMESE_DEBUG_ASSERT(rangeStart == windowCount);
            if (pDebugMsg)
                *pDebugMsg << " next:" << AddColumns(Window.ColumnStart, rangeStart);

            // Noticed this can happen somehow
            if (windowCount >= rangeOffset)
            {
                // Take range start relative to the range offset
                const unsigned relativeStart = windowCount - rangeOffset;

                // Serialize this NACK loss range into the buffer
                const unsigned encodedBytes = SerializeHeader_NACKLossRange(relativeStart, 0, buffer);
                buffer += encodedBytes;
            }

            break;
        }
        SIAMESE_DEBUG_ASSERT(rangeStart >= rangeOffset);

        unsigned rangeEnd = Window.FindNextGotElement(rangeStart + 1);
        SIAMESE_DEBUG_ASSERT(rangeEnd > rangeStart);
        SIAMESE_DEBUG_ASSERT(rangeEnd <= windowCount);
        unsigned lossCountM1 = rangeEnd - rangeStart - 1; // Loss count minus 1

        if (pDebugMsg)
        {
            if (lossCountM1 > 0)
                *pDebugMsg << " " << AddColumns(Window.ColumnStart, rangeStart) << "-" << Window.ElementToColumn(rangeEnd - 1);
            else
                *pDebugMsg << " " << AddColumns(Window.ColumnStart, rangeStart);
        }

        // Take range start relative to the range offset
        SIAMESE_DEBUG_ASSERT(rangeStart >= rangeOffset);
        const unsigned relativeStart = rangeStart - rangeOffset;

        // Serialize this NACK loss range into the buffer
        const unsigned encodedBytes = SerializeHeader_NACKLossRange(relativeStart, lossCountM1, buffer);

        // Range end is one beyond the end of the loss region.
        // The next loss cannot be before one after the range end, since we
        // either found a received packet id there, or we hit end of range.
        // This is also where we should start searching for losses again
        rangeOffset = rangeEnd + 1;

        // Advance buffer write pointer
        buffer += encodedBytes;
        byteLimit -= encodedBytes;
    }
    // Note that the loss range list may have been truncated due to the buffer space constraint

    if (pDebugMsg)
    {
        *pDebugMsg << " }";
        Logger.Debug(pDebugMsg->str());
    }

    // Write used bytes
    usedBytesOut = (unsigned)(uintptr_t)(buffer - bufferStart);

    Stats.Counts[SiameseDecoderStats_AckCount]++;
    Stats.Counts[SiameseDecoderStats_AckBytes] += usedBytesOut;

    return Siamese_Success;
}

SiameseResult Decoder::AddRecovery(const SiameseRecoveryPacket& packet)
{
    if (Window.EmergencyDisabled)
        return Siamese_Disabled;

    // Deserialize the recovery metadata from the front of the packet
    RecoveryMetadata metadata;
    int footerSize = DeserializeFooter_RecoveryMetadata(packet.Data, packet.DataBytes, metadata);
    if (footerSize < 0)
    {
        Window.EmergencyDisabled = true;
        Logger.Error("AddRecovery: Corrupt recovery metadata");
        SIAMESE_DEBUG_BREAK(); // Should never happen
        return Siamese_Disabled;
    }

    Stats.Counts[SiameseDecoderStats_RecoveryCount]++;
    Stats.Counts[SiameseDecoderStats_RecoveryBytes] += packet.DataBytes;

    unsigned elementStart, elementEnd;

    // Check if we need this recovery packet:
    if (Window.Count <= 0)
    {
        Logger.Info("Got first recovery packet: ColumnStart=", metadata.ColumnStart, " SumCount=", metadata.SumCount, " LDPC_Count=", metadata.LDPCCount, " Row=", metadata.Row);

        Window.ColumnStart = metadata.ColumnStart;

        if (!Window.GrowWindow(metadata.SumCount))
        {
            Window.EmergencyDisabled = true;
            Logger.Error("AddRecovery.GrowWindow: OOM");
            return Siamese_Disabled;
        }

        elementEnd   = metadata.SumCount;
        elementStart = elementEnd - metadata.LDPCCount;

        // This should only happen at the start if we get recovery first before data
        SIAMESE_DEBUG_ASSERT(Window.NextExpectedElement == 0);
    }
    else
    {
        Logger.Info("Got recovery packet: ColumnStart=", metadata.ColumnStart, " SumCount=", metadata.SumCount, " LDPC_Count=", metadata.LDPCCount, " Row=", metadata.Row);

        SIAMESE_DEBUG_ASSERT(metadata.ColumnStart + metadata.SumCount >= Window.ColumnStart);
        elementEnd = Window.ColumnToElement(metadata.ColumnStart + metadata.SumCount);

        // Ignore data from too long ago
        if (IsColumnDeltaNegative(elementEnd))
        {
            Logger.Info("Packet cannot be used because it ends before the window starts");
            Stats.Counts[SiameseDecoderStats_DupedRecoveryCount]++;
            return Siamese_Success;
        }

        // If we clipped the LDPC region already out of the window:
        if (elementEnd < metadata.LDPCCount)
        {
            Logger.Warning("Recovery packet cannot be used because we clipped its LDPC region already: Received too far out of order?");
            Stats.Counts[SiameseDecoderStats_DupedRecoveryCount]++;
            return Siamese_Success; // This packet cannot be used for recovery
        }
        elementStart = elementEnd - metadata.LDPCCount;

        // Ignore data we already have
        if (elementEnd <= Window.NextExpectedElement)
        {
            Logger.Debug("Ignoring unnecessary recovery packet for data we received successfully");
            if (elementStart >= kDecoderRemoveThreshold)
            {
                const unsigned recoveryBytes = packet.DataBytes - footerSize;

                // Update last recovery data
                RecoveryPackets.LastRecovery.FirstKeptElement = elementStart;
                RecoveryPackets.LastRecovery.InitialRecoveryBytes = recoveryBytes;
                RecoveryPackets.LastRecovery.SumColumnCount = metadata.SumCount;
                RecoveryPackets.LastRecovery.SumStartColumn = metadata.ColumnStart;

                Window.RemoveElements();
            }
            Stats.Counts[SiameseDecoderStats_DupedRecoveryCount]++;
            return Siamese_Success;
        }

        // Ignore sums that include data we have removed already
#ifdef SIAMESE_ENABLE_CAUCHY
        // If it is a Siamese sum row:
        if (metadata.SumCount > SIAMESE_CAUCHY_THRESHOLD)
#endif
        {
            // If there is no running sum or it does not match the new one:
            if (Window.SumColumnCount == 0 || Window.SumColumnStart != metadata.ColumnStart)
            {
                // Then we need to have all the data in the sum at hand or it is useless.
                const unsigned elementSumStart = Window.ColumnToElement(metadata.ColumnStart);
                if (Window.InvalidElement(elementSumStart))
                {
                    Logger.Info("Recovery packet cannot be used because we clipped its Sum region already : Received too far out of order ? Window.SumColumnCount = ", Window.SumColumnCount, ", Window.SumColumnCount = ", Window.SumColumnCount, ", metadata.ColumnStart = ", metadata.ColumnStart);
                    Stats.Counts[SiameseDecoderStats_DupedRecoveryCount]++;
                    return Siamese_Success;
                }
            }
        }

        // Grow the original packet window to cover all the packets this one protects
        if (!Window.GrowWindow(elementEnd))
        {
            Window.EmergencyDisabled = true;
            Logger.Error("AddRecovery.GrowWindow2: OOM");
            return Siamese_Disabled;
        }
    }

    // If this is a single (duplicate) packet:
    if (metadata.SumCount == 1)
    {
        if (!AddSingleRecovery(packet, metadata, footerSize))
        {
            Window.EmergencyDisabled = true;
            Logger.Error("AddRecovery.AddSingleRecovery failed");
            return Siamese_Disabled;
        }
        return Siamese_Success;
    }

    // Allocate a packet object
    RecoveryPacket* recovery = (RecoveryPacket*)TheAllocator.Construct<RecoveryPacket>();
    if (!recovery)
    {
        Window.EmergencyDisabled = true;
        Logger.Error("AddRecovery.Construct OOM");
        return Siamese_Disabled;
    }

    SIAMESE_DEBUG_ASSERT((unsigned)footerSize < packet.DataBytes);
    const unsigned recoveryBytes = packet.DataBytes - footerSize;
    SIAMESE_DEBUG_ASSERT(recoveryBytes > 0);

    if (!recovery->Buffer.Initialize(&TheAllocator, recoveryBytes))
    {
        TheAllocator.Destruct(recovery);
        Window.EmergencyDisabled = true;
        Logger.Error("AddRecovery.Initialize OOM");
        return Siamese_Disabled;
    }

    // Fill in the packet object
    memcpy(recovery->Buffer.Data, packet.Data, recoveryBytes);
    recovery->Metadata     = metadata;
    recovery->ElementStart = elementStart;
    recovery->ElementEnd   = elementEnd;

    // Insert it into the sorted packet list
    RecoveryPackets.Insert(recovery);

    // Remove elements from the front if possible
    if (elementStart >= kDecoderRemoveThreshold)
        Window.RemoveElements();

    return Siamese_Success;
}

bool Decoder::AddSingleRecovery(const SiameseRecoveryPacket& packet, const RecoveryMetadata& metadata, int footerSize)
{
    const unsigned element = Window.ColumnToElement(metadata.ColumnStart);
    if (Window.InvalidElement(element))
    {
        SIAMESE_DEBUG_BREAK(); // Should never happen
        return false;
    }

    // Note: In this case the length is already prefixed to the data
    SIAMESE_DEBUG_ASSERT(metadata.LDPCCount == 1 && metadata.Row == 0);
    OriginalPacket* windowOriginal = Window.GetWindowElement(element);

    // Ignore duplicate data
    if (windowOriginal->Buffer.Bytes != 0)
        return true;

    // Check: Deserialize length from the front
    SIAMESE_DEBUG_ASSERT(packet.DataBytes > (unsigned)footerSize);
    const unsigned lengthPlusDataBytes = packet.DataBytes - footerSize;
    unsigned lengthCheck;
    int headerBytes = DeserializeHeader_PacketLength(packet.Data, lengthPlusDataBytes, lengthCheck);
    if (headerBytes < 1 || lengthCheck == 0 ||
        lengthCheck + headerBytes != lengthPlusDataBytes)
    {
        SIAMESE_DEBUG_BREAK(); // Invalid input
        return false;
    }

    SiameseOriginalPacket original;
    SIAMESE_DEBUG_ASSERT((int)packet.DataBytes > footerSize + headerBytes);
    original.DataBytes = packet.DataBytes - footerSize - headerBytes;
    original.Data      = packet.Data + headerBytes;
    original.PacketNum = metadata.ColumnStart;

    unsigned newHeaderBytes = windowOriginal->Initialize(&TheAllocator, original);
    SIAMESE_DEBUG_ASSERT(newHeaderBytes == (unsigned)headerBytes);
    if (0 == newHeaderBytes)
    {
        SIAMESE_DEBUG_BREAK(); // Invalid input
        return false;
    }
    SIAMESE_DEBUG_ASSERT(windowOriginal->Buffer.Bytes > 1);

    if (!Window.HasRecoveredPackets)
    {
        Window.HasRecoveredPackets = true;
        Window.RecoveredPackets.clear();
    }

    original.Data = windowOriginal->Buffer.Data + newHeaderBytes;
    SIAMESE_DEBUG_ASSERT(original.DataBytes == windowOriginal->Buffer.Bytes - headerBytes);

    Window.RecoveredPackets.push_back(original);
    Window.RecoveredColumns.push_back(metadata.ColumnStart);

    // If the added element is somewhere inside the previously checked region:
    if (element >= CheckedRegion.ElementStart &&
        element < CheckedRegion.NextCheckStart)
    {
        CheckedRegion.Reset();
    }

    // If this was the next expected element:
    if (Window.MarkGotColumn(metadata.ColumnStart))
    {
        SIAMESE_DEBUG_ASSERT(element == Window.NextExpectedElement);

        // Iterate the next expected element beyond the recovery region
        Window.IterateNextExpectedElement(element + 1);

        Logger.Debug("AddSingleRecovery: Deleting recovery packets before element ", Window.NextExpectedElement, " column = ", (Window.NextExpectedElement + Window.ColumnStart));

        RecoveryPackets.DeletePacketsBefore(Window.NextExpectedElement);

        if (CheckedRegion.NextCheckStart >= kDecoderRemoveThreshold)
            Window.RemoveElements();
    }

    return true;
}

bool Decoder::CheckRecoveryPossible()
{
    if (Window.EmergencyDisabled)
        return false;

    RecoveryPacket* recovery;
    unsigned nextCheckStart, recoveryCount, lostCount;

    // If we just started checking again:
    if (!CheckedRegion.LastRecovery)
    {
        recovery = RecoveryPackets.Head;
        if (!recovery)
            return false; // No recovery data

        CheckedRegion.FirstRecovery = recovery;
        CheckedRegion.ElementStart  = recovery->ElementStart;
#ifdef SIAMESE_DEBUG
        const unsigned lostPacketsBeforeLDPC = Window.RangeLostPackets(0, recovery->ElementStart);
        SIAMESE_DEBUG_ASSERT(0 == lostPacketsBeforeLDPC);
#endif
        recoveryCount  = 1;
        nextCheckStart = recovery->ElementEnd;
        lostCount      = Window.RangeLostPackets(recovery->ElementStart, nextCheckStart);
        CheckedRegion.SolveFailed = false;

        // Keep track of how many losses this recovery packet is facing
        recovery->LostCount = lostCount;
    }
    else
    {
        recoveryCount = CheckedRegion.RecoveryCount;
        lostCount     = CheckedRegion.LostCount;
        if (recoveryCount >= lostCount && !CheckedRegion.SolveFailed)
            return true; // It is already possible

        recovery       = CheckedRegion.LastRecovery;
        nextCheckStart = CheckedRegion.NextCheckStart;
    }
    SIAMESE_DEBUG_ASSERT(lostCount > 0);

    // While we do not have enough recovery data:
    while ((recoveryCount < lostCount || CheckedRegion.SolveFailed) &&
           (recovery->Next != nullptr))
    {
        recovery = recovery->Next;
        ++recoveryCount;

        // Accumulate losses within the range of this recovery packet, skipping
        // losses we've already accumulated into the checked region
        unsigned elementEnd = recovery->ElementEnd;
        if (elementEnd < nextCheckStart)
            elementEnd = nextCheckStart; // This can happen when interleaved with Cauchy packets
        Logger.Debug("RecoveryPossible? Searching between ", nextCheckStart, " and ", elementEnd);
        lostCount += Window.RangeLostPackets(nextCheckStart, elementEnd);
        SIAMESE_DEBUG_ASSERT(lostCount > 0);
        nextCheckStart = elementEnd;

        // Keep track of how many losses this recovery packet is facing
        recovery->LostCount = lostCount;

        CheckedRegion.SolveFailed = false;
    }

    // Remember state for the next time around
    CheckedRegion.LastRecovery   = recovery;
    CheckedRegion.RecoveryCount  = recoveryCount;
    CheckedRegion.LostCount      = lostCount;
    CheckedRegion.NextCheckStart = nextCheckStart;

    Logger.Debug("RecoveryPossible? LostCount=", CheckedRegion.LostCount, " RecoveryCount=", CheckedRegion.RecoveryCount);

    return recoveryCount >= lostCount && !CheckedRegion.SolveFailed;
}

SiameseResult Decoder::Decode(SiameseOriginalPacket** packetsPtrOut, unsigned* countOut)
{
    if (Window.EmergencyDisabled)
        return Siamese_Disabled;

    // If there are already recovered packets to report:
    if (Window.HasRecoveredPackets)
    {
        Window.HasRecoveredPackets = false;
        SIAMESE_DEBUG_ASSERT(!Window.RecoveredPackets.empty());
        if (packetsPtrOut)
        {
            *packetsPtrOut = &Window.RecoveredPackets[0];
            *countOut      = (unsigned)Window.RecoveredPackets.size();
        }
        return Siamese_Success;
    }

    // Default return on failure
    if (packetsPtrOut)
    {
        *packetsPtrOut = nullptr;
        *countOut      = 0;
    }

    /*
        The goal of this routine is to determine if solutions to the matrix inverse
        problem exists with minimal work, and to keep track of the data operations
        required to arrive at the solution.  Given the row and column descriptions,
        we generate the square matrix to invert.  Then we use Gaussian Elimination
        while keeping track of what values are multiplied in place of the original
        matrix elements.  If successful, the resulting matrix can be multiplied by
        the recovery data in the solution order to recover the lost data.
    */

    // Advance the checked region to the first possible solution
    if (!CheckRecoveryPossible())
        return Siamese_NeedMoreData;

    RecoveryPacket* recovery = CheckedRegion.LastRecovery;
    unsigned nextCheckStart  = CheckedRegion.NextCheckStart;
    unsigned recoveryCount   = CheckedRegion.RecoveryCount;
    unsigned lostCount       = CheckedRegion.LostCount;

    SIAMESE_DEBUG_ASSERT(recovery != nullptr && nextCheckStart > CheckedRegion.ElementStart);
    SIAMESE_DEBUG_ASSERT(lostCount > 0 && lostCount <= recoveryCount);

    for (;;)
    {
        if (recoveryCount >= lostCount)
        {
            SiameseResult result = DecodeCheckedRegion();

            // Pass error or success up; continue on decode failure
            if (result == Siamese_Success)
            {
                if (packetsPtrOut)
                {
                    *packetsPtrOut = &Window.RecoveredPackets[0];
                    *countOut      = (unsigned)Window.RecoveredPackets.size();
                }
                return Siamese_Success;
            }

            if (result != Siamese_NeedMoreData)
                return result;
        }

        if (recovery->Next == nullptr)
            break;
        recovery = recovery->Next;
        ++recoveryCount;

        // Accumulate losses within the range of this recovery packet, skipping
        // losses we've already accumulated into the checked region
        unsigned elementEnd = recovery->ElementEnd;
        if (elementEnd < nextCheckStart)
            elementEnd = nextCheckStart; // This can happen when interleaved with Cauchy packets
        lostCount += Window.RangeLostPackets(nextCheckStart, elementEnd);

        // Keep track of how many lost packets this recovery packet is facing
        recovery->LostCount = lostCount;

        nextCheckStart = elementEnd;
    }

    // Remember state for the next time around
    CheckedRegion.LastRecovery   = recovery;
    CheckedRegion.NextCheckStart = nextCheckStart;
    CheckedRegion.RecoveryCount  = recoveryCount;
    CheckedRegion.LostCount      = lostCount;
    return Siamese_NeedMoreData;
}

SiameseResult Decoder::DecodeCheckedRegion()
{
    Logger.Debug("Attempting decode...");

#ifdef SIAMESE_DECODER_DUMP_SOLVER_PERF
    bool skipLog = CheckedRegion.LostCount <= 1;
    if (!skipLog)
        Logger.Debug("For ", CheckedRegion.LostCount, " losses:");

    uint64_t t0 = GetTimeUsec();
#endif

    // Generate updated recovery matrix
    if (!RecoveryMatrix.GenerateMatrix())
    {
        Window.EmergencyDisabled = true;
        Logger.Error("DecodeCheckedRegion.GenerateMatrix failed");
        return Siamese_Disabled;
    }

#ifdef SIAMESE_DECODER_DUMP_SOLVER_PERF
    uint64_t t1 = GetTimeUsec();
#endif

    // Attempt to solve the linear system
    if (!RecoveryMatrix.GaussianElimination())
    {
        CheckedRegion.SolveFailed = true;
        Stats.Counts[SiameseDecoderStats_SolveFailCount]++;
        return Siamese_NeedMoreData;
    }

#ifdef SIAMESE_DECODER_DUMP_SOLVER_PERF
    uint64_t t2 = GetTimeUsec();
#endif

    if (!EliminateOriginalData())
    {
        Window.EmergencyDisabled = true;
        Logger.Error("DecodeCheckedRegion.EliminateOriginalData failed");
        return Siamese_Disabled;
    }

#ifdef SIAMESE_DECODER_DUMP_SOLVER_PERF
    uint64_t t3 = GetTimeUsec();
#endif

    if (!MultiplyLowerTriangle())
    {
        Window.EmergencyDisabled = true;
        Logger.Error("DecodeCheckedRegion.MultiplyLowerTriangle failed");
        return Siamese_Disabled;
    }

#ifdef SIAMESE_DECODER_DUMP_SOLVER_PERF
    uint64_t t4 = GetTimeUsec();
#endif

    SiameseResult solveResult = BackSubstitution();

#ifdef SIAMESE_DECODER_DUMP_SOLVER_PERF
    uint64_t t5 = GetTimeUsec();
#endif

    CheckedRegion.Reset();

#ifdef SIAMESE_DECODER_DUMP_SOLVER_PERF
    uint64_t t6 = GetTimeUsec();

    if (!skipLog)
    {
        Logger.Info("RecoveryMatrix.GenerateMatrix: ", (t1 - t0), " usec");
        Logger.Info("RecoveryMatrix.GaussianElimination: ", (t2 - t1), " usec");
        Logger.Info("EliminateOriginalData: ", (t3 - t2), " usec");
        Logger.Info("MultiplyLowerTriangle: ", (t4 - t3), " usec");
        Logger.Info("BackSubstitution: ", (t5 - t4), " usec");
        Logger.Info("Cleanup: ", (t6 - t5), " usec");
    }
#endif

    return solveResult;
}

bool Decoder::EliminateOriginalData()
{
    SIAMESE_DEBUG_ASSERT(CheckedRegion.LostCount == (unsigned)RecoveryMatrix.Columns.size());

    std::ostringstream* pDebugMsg = nullptr;

    // Note: This is done because the Siamese sums need to be accumulated from
    // left to right in the same order that the encoder generated them.
    // This step tends to be slow because there is a lot of data that was
    // successfully received that we need to eliminate from the recovery sums

    const unsigned rows = CheckedRegion.RecoveryCount;
    SIAMESE_DEBUG_ASSERT(CheckedRegion.RecoveryCount == (unsigned)RecoveryMatrix.Rows.size());

    // Eliminate data in sorted row order regardless of pivot order:
    for (unsigned matrixRowIndex = 0; matrixRowIndex < rows; ++matrixRowIndex)
    {
        if (!RecoveryMatrix.Rows[matrixRowIndex].UsedForSolution)
            continue;

        RecoveryPacket* recovery        = RecoveryMatrix.Rows[matrixRowIndex].Recovery;
        const RecoveryMetadata metadata = recovery->Metadata;
        const unsigned elementStart     = recovery->ElementStart;
        const unsigned elementEnd       = recovery->ElementEnd;
        GrowingAlignedDataBuffer& recoveryBuffer = recovery->Buffer;
        SIAMESE_DEBUG_ASSERT(recoveryBuffer.Data && recoveryBuffer.Bytes > 0);

#ifdef SIAMESE_ENABLE_CAUCHY
        // If it is a Cauchy or parity row:
        if (metadata.SumCount <= SIAMESE_CAUCHY_THRESHOLD)
        {
            // If this is a parity row:
            if (metadata.Row == 0)
            {
                // Fill columns from left for new rows:
                for (unsigned j = elementStart; j < elementEnd; ++j)
                {
                    OriginalPacket* original = Window.GetWindowElement(j);
                    unsigned addBytes = original->Buffer.Bytes;
                    if (addBytes > 0)
                    {
                        if (addBytes > recoveryBuffer.Bytes)
                        {
                            SIAMESE_DEBUG_BREAK(); // Should never happen
                            addBytes = recoveryBuffer.Bytes;
                        }
                        gf256_add_mem(recoveryBuffer.Data, original->Buffer.Data, addBytes);
                    }
                }
            }
            else // This is a Cauchy row:
            {
                // Fill columns from left for new rows:
                for (unsigned j = elementStart; j < elementEnd; ++j)
                {
                    OriginalPacket* original = Window.GetWindowElement(j);
                    unsigned addBytes = original->Buffer.Bytes;
                    if (addBytes > 0)
                    {
                        const uint8_t y = CauchyElement(metadata.Row - 1, original->Column % kCauchyMaxColumns);
                        if (addBytes > recoveryBuffer.Bytes)
                        {
                            SIAMESE_DEBUG_BREAK(); // Should never happen
                            addBytes = recoveryBuffer.Bytes;
                        }
                        gf256_muladd_mem(recoveryBuffer.Data, y, original->Buffer.Data, addBytes);
                    }
                }
            }

            continue;
        }
#endif // SIAMESE_ENABLE_CAUCHY

        // Zero the product sum
        const unsigned recoveryBytes = recoveryBuffer.Bytes;
        if (!ProductSum.Initialize(&TheAllocator, recoveryBytes))
            return false;
        memset(ProductSum.Data, 0, recoveryBytes);

        Logger.Debug("Starting sums for row=", recovery->Metadata.Row, " start=", recovery->Metadata.ColumnStart, " count=", recovery->Metadata.SumCount);

        // Determine sum start element
        unsigned sumElementStart = Window.ColumnToElement(recovery->Metadata.ColumnStart);
        if (Window.InvalidElement(sumElementStart))
            sumElementStart = 0;

        if (metadata.ColumnStart != Window.SumColumnStart ||
            metadata.SumCount < Window.SumColumnCount)
        {
            Window.ResetSums(sumElementStart);
            Window.SumColumnStart = metadata.ColumnStart;
        }
        else if (!Window.StartSums(sumElementStart, recoveryBytes))
            return false;
        Window.SumColumnCount = metadata.SumCount;

        // Eliminate dense recovery data outside of matrix:
        for (unsigned laneIndex = 0; laneIndex < kColumnLaneCount; ++laneIndex)
        {
            const unsigned opcode = GetRowOpcode(laneIndex, metadata.Row);

            // For summations into the RecoveryPacket buffer:
            unsigned mask = 1;
            for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
            {
                if (opcode & mask)
                {
                    const GrowingAlignedDataBuffer* sum = Window.GetSum(laneIndex, sumIndex, elementEnd);
                    unsigned addBytes = sum->Bytes;
                    if (addBytes > 0)
                    {
                        if (addBytes > recoveryBytes)
                            addBytes = recoveryBytes;
                        gf256_add_mem(recoveryBuffer.Data, sum->Data, addBytes);
                    }
                }
                mask <<= 1;
            }

            // For summations into the ProductWorkspace buffer:
            for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
            {
                if (opcode & mask)
                {
                    const GrowingAlignedDataBuffer* sum = Window.GetSum(laneIndex, sumIndex, elementEnd);
                    unsigned addBytes = sum->Bytes;
                    if (addBytes > 0)
                    {
                        if (addBytes > recoveryBytes)
                            addBytes = recoveryBytes;
                        gf256_add_mem(ProductSum.Data, sum->Data, addBytes);
                    }
                }
                mask <<= 1;
            }
        }

        // Eliminate light recovery data outside of matrix:
        PCGRandom prng;
        prng.Seed(metadata.Row, metadata.LDPCCount);
        SIAMESE_DEBUG_ASSERT(metadata.SumCount >= metadata.LDPCCount);

        if (Logger.ShouldLog(logger::Level::Debug))
        {
            delete pDebugMsg;
            pDebugMsg = new std::ostringstream();
            *pDebugMsg << "(Eliminate originals) LDPC columns (*=missing): ";
        }

        const unsigned pairCount = (metadata.LDPCCount + kPairAddRate - 1) / kPairAddRate;
        for (unsigned i = 0; i < pairCount; ++i)
        {
            const unsigned element1   = elementStart + (prng.Next() % metadata.LDPCCount);
            OriginalPacket* original1 = Window.GetWindowElement(element1);
            unsigned addBytes1 = original1->Buffer.Bytes;
            if (addBytes1 > 0)
            {
                if (addBytes1 > recoveryBytes)
                {
                    SIAMESE_DEBUG_BREAK(); // Should never happen
                    addBytes1 = recoveryBytes;
                }
                gf256_add_mem(recoveryBuffer.Data, original1->Buffer.Data, addBytes1);

                if (pDebugMsg)
                    *pDebugMsg << element1 << " ";
            }
            else
            {
                if (pDebugMsg)
                    *pDebugMsg << element1 << "* ";
            }

            const unsigned elementRX   = elementStart + (prng.Next() % metadata.LDPCCount);
            OriginalPacket* originalRX = Window.GetWindowElement(elementRX);
            unsigned addBytesRX = originalRX->Buffer.Bytes;
            if (addBytesRX > 0)
            {
                if (addBytesRX > recoveryBytes)
                {
                    SIAMESE_DEBUG_BREAK(); // Should never happen
                    addBytesRX = recoveryBytes;
                }
                gf256_add_mem(ProductSum.Data, originalRX->Buffer.Data, addBytesRX);

                if (pDebugMsg)
                    *pDebugMsg << elementRX << " ";
            }
            else
            {
                if (pDebugMsg)
                    *pDebugMsg << elementRX << "* ";
            }
        }

        if (pDebugMsg)
            Logger.Debug(pDebugMsg->str());

        SIAMESE_DEBUG_ASSERT(recoveryBuffer.Bytes == ProductSum.Bytes);
        const uint8_t RX = GetRowValue(metadata.Row);
        gf256_muladd_mem(recoveryBuffer.Data, RX, ProductSum.Data, ProductSum.Bytes);
    }

    // Return false if GetSum() ran out of memory
    return !Window.EmergencyDisabled;
}

bool Decoder::MultiplyLowerTriangle()
{
    // Note: This step tends to be slow because it is a dense triangular
    // matrix-vector product

    const unsigned columns = CheckedRegion.LostCount;

    // Multiply lower triangle following solution order from left to right:
    for (unsigned col_i = 0; col_i < columns - 1; ++col_i)
    {
        const unsigned matrixRowIndex_i = RecoveryMatrix.Pivots[col_i];
        const GrowingAlignedDataBuffer& recovery_i = RecoveryMatrix.Rows[matrixRowIndex_i].Recovery->Buffer;
        const uint8_t* srcData  = recovery_i.Data;
        const unsigned srcBytes = recovery_i.Bytes;
        SIAMESE_DEBUG_ASSERT(srcData && srcBytes > 0);

        for (unsigned col_j = col_i + 1; col_j < columns; ++col_j)
        {
            const unsigned matrixRowIndex_j = RecoveryMatrix.Pivots[col_j];
            const uint8_t y = RecoveryMatrix.Matrix.Get(matrixRowIndex_j, col_i);

            if (y == 0)
                continue;

            GrowingAlignedDataBuffer& recovery_j = RecoveryMatrix.Rows[matrixRowIndex_j].Recovery->Buffer;
            SIAMESE_DEBUG_ASSERT(recovery_j.Data && recovery_j.Bytes > 0);

            // Make room for the summation
            if (!recovery_j.GrowZeroPadded(&TheAllocator, srcBytes))
                return false;

            gf256_muladd_mem(recovery_j.Data, y, srcData, srcBytes);
        }
    }

    return true;
}

SiameseResult Decoder::BackSubstitution()
{
    // Note: This step tends to be fast because the upper-right of the matrix
    // while streaming is mostly zero

    const unsigned columns = CheckedRegion.LostCount;
    Window.RecoveredPackets.resize(columns);

    bool iterateNextExpected = false;

    // For each column starting with the right-most column:
    for (int col_i = columns - 1; col_i >= 0; --col_i)
    {
        const unsigned matrixRowIndex = RecoveryMatrix.Pivots[col_i];
        OriginalPacket* original      = RecoveryMatrix.Columns[col_i].Original;
        RecoveryPacket* recovery      = RecoveryMatrix.Rows[matrixRowIndex].Recovery;
        SIAMESE_DEBUG_ASSERT(original->Column == (unsigned)col_i && original->Buffer.Bytes == 0);

        uint8_t* buffer = recovery->Buffer.Data;
        const uint8_t y = RecoveryMatrix.Matrix.Get(matrixRowIndex, col_i);

        SIAMESE_DEBUG_ASSERT(buffer && recovery->Buffer.Bytes > 0);
        SIAMESE_DEBUG_ASSERT(y != 0);

        // Reveal the first chunk of bytes of data
        unsigned bufferBytes      = recovery->Buffer.Bytes;
        unsigned lengthCheckBytes = pktalloc::kAlignmentBytes;
        if (lengthCheckBytes > bufferBytes)
            lengthCheckBytes = bufferBytes;
        gf256_div_mem(buffer, buffer, y, lengthCheckBytes);

        // Check the embedded length field
        unsigned length;
        int headerBytes = DeserializeHeader_PacketLength(buffer, lengthCheckBytes, length);
        if (headerBytes < 0 || length == 0 || headerBytes + length > bufferBytes)
        {
            //------------------------------------------------------------------
            // This error means that the Siamese FEC recovery has failed.
            // Common causes:
            // + Packet Numbers provided by application are incorrect.
            // + Or some software bug in this library I need to fix.
            //------------------------------------------------------------------
            Window.EmergencyDisabled = true;
            Logger.Error("BackSubstitution corrupted recovered data len");
            SIAMESE_DEBUG_BREAK(); // Should never happen
            return Siamese_Disabled;
        }

        // Reduce buffer bytes to only cover the original packet data
        bufferBytes = headerBytes + length;
        if (bufferBytes > lengthCheckBytes)
            gf256_div_mem(buffer + lengthCheckBytes, buffer + lengthCheckBytes, y, bufferBytes - lengthCheckBytes);

        // Swap original and recovery buffers
        uint8_t* oldOriginalData = original->Buffer.Data;
        original->Buffer.Data    = buffer;
        original->Buffer.Bytes   = bufferBytes;
        original->Column         = RecoveryMatrix.Columns[col_i].Column;
        original->HeaderBytes    = (unsigned)headerBytes;
        recovery->Buffer.Data    = oldOriginalData;
        recovery->Buffer.Bytes   = 0;

        // Write recovered packet data
        SiameseOriginalPacket& recovered = Window.RecoveredPackets[col_i];
        recovered.Data        = buffer + headerBytes;
        recovered.DataBytes   = length;
        recovered.PacketNum   = original->Column;

        Window.RecoveredColumns.push_back(original->Column);

        Logger.Trace("GE Decoded: Column=", original->Column, " Row=", recovery->Metadata.Row);

        iterateNextExpected |= Window.MarkGotColumn(original->Column);

        // Eliminate from all other pivot rows above it:
        for (unsigned col_j = 0; col_j < (unsigned)col_i; ++col_j)
        {
            unsigned pivot_j = RecoveryMatrix.Pivots[col_j];
            const uint8_t x  = RecoveryMatrix.Matrix.Get(pivot_j, col_i);

            if (x == 0)
                continue;

            const GrowingAlignedDataBuffer& buffer_j = RecoveryMatrix.Rows[pivot_j].Recovery->Buffer;
            SIAMESE_DEBUG_ASSERT(buffer_j.Data && buffer_j.Bytes > 0);

            unsigned addBytes = bufferBytes;
            if (addBytes > buffer_j.Bytes)
            {
                SIAMESE_DEBUG_BREAK(); // This should never happen
                addBytes = buffer_j.Bytes;
            }

            gf256_muladd_mem(buffer_j.Data, x, buffer, addBytes);
        }
    }

    // We always expect to have recovered the next expected packet
    if (!iterateNextExpected)
    {
        Window.EmergencyDisabled = true;
        Logger.Error("BackSubstitution.iterateNextExpected failed");
        SIAMESE_DEBUG_BREAK(); // Should never happen
        return Siamese_Disabled;
    }

    // Iterate the next expected element beyond the recovery region
    Window.IterateNextExpectedElement(CheckedRegion.NextCheckStart);

    Logger.Debug("BackSubstitution: Deleting recovery packets before element ", Window.NextExpectedElement, " column = ", (Window.NextExpectedElement + Window.ColumnStart));

    RecoveryPackets.DeletePacketsBefore(Window.NextExpectedElement);

    if (CheckedRegion.NextCheckStart >= kDecoderRemoveThreshold)
        Window.RemoveElements();

    Stats.Counts[SiameseDecoderStats_SolveSuccessCount]++;

    return Siamese_Success;
}

SiameseResult Decoder::GetStatistics(uint64_t* statsOut, unsigned statsCount)
{
    if (statsCount > SiameseEncoderStats_Count)
        statsCount = SiameseEncoderStats_Count;

    // Fill in memory allocated
    Stats.Counts[SiameseEncoderStats_MemoryUsed] = TheAllocator.GetMemoryAllocatedBytes();

    for (unsigned i = 0; i < statsCount; ++i)
        statsOut[i] = Stats.Counts[i];

    return Siamese_Success;
}


//------------------------------------------------------------------------------
// DecoderPacketWindow

bool DecoderPacketWindow::MarkGotColumn(unsigned column)
{
    // Convert to window element
    SIAMESE_DEBUG_ASSERT(column >= ColumnStart);
    const unsigned element = ColumnToElement(column);
    if (InvalidElement(element))
    {
        EmergencyDisabled = true;
        Logger.Error("MarkGotColumn failed");
        SIAMESE_DEBUG_BREAK(); // Should never happen
        return false;
    }

    DecoderSubwindow& subwindow = *Subwindows[element / kSubwindowSize];
    subwindow.GotCount++;
    subwindow.Got.Set(element % kSubwindowSize);

    return (element == NextExpectedElement);
}

unsigned DecoderPacketWindow::RangeLostPackets(unsigned elementStart, unsigned elementEnd)
{
    if (elementStart >= elementEnd)
        return 0;

    unsigned lostCount = 0;

    // Accumulate first partial subwindow (if any)
    unsigned subwindowStart = elementStart / kSubwindowSize;
    SIAMESE_DEBUG_ASSERT(subwindowStart < (unsigned)Subwindows.size());
    const unsigned bitStart = elementStart % kSubwindowSize;
    if (bitStart > 0)
    {
        unsigned bitEnd = bitStart + elementEnd - elementStart;
        if (bitEnd > kSubwindowSize)
            bitEnd = kSubwindowSize;
        const unsigned bitMaxSet = bitEnd - bitStart; // Bit count in range
        lostCount += bitMaxSet - Subwindows[subwindowStart]->Got.RangePopcount(bitStart, bitEnd);
        ++subwindowStart;
    }

    // Accumulate whole subwindows of losses
    const unsigned subwindowEnd = elementEnd / kSubwindowSize;
    SIAMESE_DEBUG_ASSERT(subwindowEnd <= (unsigned)Subwindows.size());
    for (unsigned i = subwindowStart; i < subwindowEnd; ++i)
        lostCount += kSubwindowSize - Subwindows[i]->GotCount;

    // Accumulate last partial subwindow (if any, common case)
    if (subwindowEnd >= subwindowStart)
    {
        const unsigned lastSubwindowBits = elementEnd - subwindowEnd * kSubwindowSize;
        if (lastSubwindowBits > 0)
            lostCount += lastSubwindowBits - Subwindows[subwindowEnd]->Got.RangePopcount(0, lastSubwindowBits);
    }

    return lostCount;
}

unsigned DecoderPacketWindow::FindNextLostElement(unsigned elementStart)
{
    if (elementStart >= Count)
        return Count;

    const unsigned subwindowEnd = (Count + kSubwindowSize - 1) / kSubwindowSize;
    unsigned subwindowIndex = elementStart / kSubwindowSize;
    unsigned bitIndex = elementStart % kSubwindowSize;
    SIAMESE_DEBUG_ASSERT(subwindowEnd <= (unsigned)Subwindows.size());
    SIAMESE_DEBUG_ASSERT(subwindowIndex < (unsigned)Subwindows.size());

    while (subwindowIndex < subwindowEnd)
    {
        // If there may be any lost packets in this subwindow:
        if (Subwindows[subwindowIndex]->GotCount < kSubwindowSize)
        {
            for (;;)
            {
                // Seek next clear bit
                bitIndex = Subwindows[subwindowIndex]->Got.FindFirstClear(bitIndex);

                // If there were none, skip this subwindow
                if (bitIndex >= kSubwindowSize)
                    break;

                // Calculate element index and stop if we hit the end of the valid data
                unsigned nextElement = subwindowIndex * kSubwindowSize + bitIndex;
                if (nextElement > Count)
                    nextElement = Count;

                return nextElement;
            }
        }

        // Reset bit index to the front of the next subwindow
        bitIndex = 0;

        // Check next subwindow
        ++subwindowIndex;
    }

    return Count;
}

unsigned DecoderPacketWindow::FindNextGotElement(unsigned elementStart)
{
    if (elementStart >= Count)
        return Count;

    const unsigned subwindowEnd = (Count + kSubwindowSize - 1) / kSubwindowSize;
    unsigned subwindowIndex = elementStart / kSubwindowSize;
    unsigned bitIndex = elementStart % kSubwindowSize;
    SIAMESE_DEBUG_ASSERT(subwindowEnd <= (unsigned)Subwindows.size());
    SIAMESE_DEBUG_ASSERT(subwindowIndex < (unsigned)Subwindows.size());

    while (subwindowIndex < subwindowEnd)
    {
        // If there may be any got packets in this subwindow:
        if (Subwindows[subwindowIndex]->GotCount > 0)
        {
            for (;;)
            {
                // Seek next set bit
                bitIndex = Subwindows[subwindowIndex]->Got.FindFirstSet(bitIndex);

                // If there were none, skip this subwindow
                if (bitIndex >= kSubwindowSize)
                    break;

                // Calculate element index and stop if we hit the end of the valid data
                unsigned nextElement = subwindowIndex * kSubwindowSize + bitIndex;
                if (nextElement > Count)
                    nextElement = Count;

                return nextElement;
            }
        }

        // Reset bit index to the front of the next subwindow
        bitIndex = 0;

        // Check next subwindow
        ++subwindowIndex;
    }

    return Count;
}

void DecoderPacketWindow::IterateNextExpectedElement(unsigned elementStart)
{
    SIAMESE_DEBUG_ASSERT(elementStart > NextExpectedElement);
    if (NextExpectedElement >= Count)
        return;

    SIAMESE_DEBUG_ASSERT(RangeLostPackets(0, NextExpectedElement) == 0);
    SIAMESE_DEBUG_ASSERT(RangeLostPackets(NextExpectedElement, elementStart) == 0);

    const unsigned nextLostElement = FindNextLostElement(elementStart);

    SIAMESE_DEBUG_ASSERT(RangeLostPackets(elementStart, nextLostElement) == 0);

    NextExpectedElement = nextLostElement;
}

bool DecoderPacketWindow::GrowWindow(const unsigned windowElementEnd)
{
    // Note: Adding a buffer of lane count to create space ahead for snapshots
    // as a subwindow is filled and we need to store its snapshot
    const unsigned subwindowCount   = (unsigned)Subwindows.size();
    const unsigned subwindowsNeeded = (windowElementEnd + kColumnLaneCount + kSubwindowSize - 1) / kSubwindowSize;

    if (subwindowsNeeded > subwindowCount)
    {
        // Note resizing larger will keep old data in the vector
        Subwindows.resize(subwindowsNeeded);

        for (unsigned i = subwindowCount; i < subwindowsNeeded; ++i)
        {
            Subwindows[i] = TheAllocator->Construct<DecoderSubwindow>();

            if (!Subwindows[i])
                return false; // Out of memory
        }
    }

    // If this element expands the window:
    if (windowElementEnd > Count)
        Count = windowElementEnd;

    return true;
}

SiameseResult DecoderPacketWindow::AddOriginal(const SiameseOriginalPacket& packet)
{
    if (EmergencyDisabled)
        return Siamese_Disabled;

    SIAMESE_DEBUG_ASSERT(packet.Data && packet.DataBytes > 0);
    const unsigned element = ColumnToElement(packet.PacketNum);

    // If we just received an old element before our window:
    if (IsColumnDeltaNegative(element))
    {
        Logger.Debug("Ignored an old packet before window start: ", packet.PacketNum);
        Stats->Counts[SiameseDecoderStats_DupedOriginalCount]++;
        return Siamese_DuplicateData;
    }

    if (!GrowWindow(element + 1))
    {
        EmergencyDisabled = true;
        Logger.Error("AddOriginal.GrowWindow OOM");
        return Siamese_Disabled;
    }

    // Grab the window element for this packet
    DecoderSubwindow& subwindow     = *Subwindows[element / kSubwindowSize];
    const unsigned subwindowElement = element % kSubwindowSize;

    OriginalPacket* original = &subwindow.Originals[subwindowElement];
    if (original->Buffer.Bytes > 0)
    {
        Logger.Debug("Ignored a packet already received: ", packet.PacketNum);
        Stats->Counts[SiameseDecoderStats_DupedOriginalCount]++;
        return Siamese_DuplicateData;
    }

    // Make space for the packet data
    if (0 == original->Initialize(TheAllocator, packet))
    {
        EmergencyDisabled = true;
        Logger.Error("AddOriginal.Initialize OOM");
        return Siamese_Disabled;
    }
    SIAMESE_DEBUG_ASSERT(original->Buffer.Bytes > 1);

    // Increment the number of packets filled in for this subwindow
    subwindow.GotCount++;
    subwindow.Got.Set(subwindowElement);

    // If this was the next expected element:
    if (element == NextExpectedElement)
    {
        IterateNextExpectedElement(element + 1);

        Logger.Debug("AddOriginal: Deleting recovery packets before element ", NextExpectedElement, " column = ", (NextExpectedElement + ColumnStart));

        RecoveryPackets->DeletePacketsBefore(NextExpectedElement);
    }

    // If the added element is somewhere inside the previously checked region:
    if (element >= CheckedRegion->ElementStart &&
        element < CheckedRegion->NextCheckStart)
    {
        CheckedRegion->Reset();
    }

    Stats->Counts[SiameseDecoderStats_OriginalCount]++;
    Stats->Counts[SiameseDecoderStats_OriginalBytes] += packet.DataBytes;

    return Siamese_Success;
}

bool DecoderPacketWindow::PlugSumHoles(unsigned elementStart)
{
    const unsigned recoveredCount = (unsigned)RecoveredColumns.size();
    SIAMESE_DEBUG_ASSERT(recoveredCount > 0);

    // Use previously recovered packets to plug holes in the sums:
    for (unsigned i = 0; i < recoveredCount; ++i)
    {
        const unsigned column  = RecoveredColumns[i];
        const unsigned element = ColumnToElement(column);

        // If recovered data was far in the past:
        if (InvalidElement(element))
            continue;

        const unsigned laneIndex        = column % kColumnLaneCount;
        const unsigned laneElementStart = GetNextLaneElement(elementStart, laneIndex);

        for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
        {
            DecoderSum& sum = Lanes[laneIndex].Sums[sumIndex];

            // If this element fills in a hole in the new sum:
            if (element >= laneElementStart && element < sum.ElementEnd)
            {
                const OriginalPacket* original = GetWindowElement(element);
                unsigned originalBytes         = original->Buffer.Bytes;
                SIAMESE_DEBUG_ASSERT(original->Column == column);

                if (originalBytes <= 0)
                {
                    SIAMESE_DEBUG_BREAK(); // Should never happen
                    return false;
                }

                if (originalBytes > sum.Buffer.Bytes)
                {
                    // Grow sum to encompass the original data
                    if (!sum.Buffer.GrowZeroPadded(TheAllocator, originalBytes))
                        return false;
                }

                // Sum += PacketData
                if (sumIndex == 0)
                    gf256_add_mem(sum.Buffer.Data, original->Buffer.Data, originalBytes);
                else
                {
                    uint8_t CX = GetColumnValue(column);
                    if (sumIndex == 2)
                        CX = gf256_sqr(CX);

                    // Sum += CX * PacketData
                    gf256_muladd_mem(sum.Buffer.Data, CX, original->Buffer.Data, originalBytes);
                }

                Logger.Debug("Filled hole in sum for ", laneIndex, " sum ", sumIndex, " at column ", element + ColumnStart);
            }
        }
    }

    // Clear recovered packets to avoid double-plugging holes in the sums
    RecoveredColumns.clear();

    return true;
}

void DecoderPacketWindow::ResetSums(unsigned elementStart)
{
    Logger.Info("Clearing all sums");

    for (unsigned laneIndex = 0; laneIndex < kColumnLaneCount; ++laneIndex)
    {
        const unsigned laneElementStart = GetNextLaneElement(elementStart, laneIndex);

        for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
        {
            DecoderSum& sum  = Lanes[laneIndex].Sums[sumIndex];
            sum.ElementStart = laneElementStart;
            sum.ElementEnd   = laneElementStart;
            sum.Buffer.Bytes = 0;
        }
    }

    RecoveredColumns.clear();
}

bool DecoderPacketWindow::StartSums(unsigned elementStart, unsigned bufferBytes)
{
    for (unsigned laneIndex = 0; laneIndex < kColumnLaneCount; ++laneIndex)
    {
        const unsigned laneElementStart = GetNextLaneElement(elementStart, laneIndex);

        for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
        {
            DecoderSum& sum = Lanes[laneIndex].Sums[sumIndex];

            // If the sum contains no data or starts in a different place:
            if (sum.Buffer.Bytes == 0)
            {
                Logger.Debug("Re-Restarting sum for ", laneIndex, " sum ", sumIndex, " at column ", laneElementStart + ColumnStart, " current sum bytes = ", sum.Buffer.Bytes);

                sum.ElementEnd = laneElementStart;
            }
            else if (sum.ElementStart != laneElementStart)
            {
                Logger.Debug("Restarting sum for ", laneIndex, " sum ", sumIndex, " at column ", laneElementStart + ColumnStart, " current sum bytes = ", sum.Buffer.Bytes);

                sum.ElementEnd   = laneElementStart;
                sum.Buffer.Bytes = 0;
            }

            // Update the start element
            sum.ElementStart = laneElementStart;

            // Grow and zero pad
            if (!sum.Buffer.GrowZeroPadded(TheAllocator, bufferBytes))
                return false;

            SIAMESE_DEBUG_ASSERT((sum.ElementStart + ColumnStart) % kColumnLaneCount == laneIndex);
            SIAMESE_DEBUG_ASSERT((sum.ElementEnd   + ColumnStart) % kColumnLaneCount == laneIndex);
        }
    }

    // If we have previously recovered packets, use them to plug holes in the sums:
    if (!RecoveredColumns.empty() && !PlugSumHoles(elementStart))
        return false;

    return true;
}

const GrowingAlignedDataBuffer* DecoderPacketWindow::GetSum(unsigned laneIndex, unsigned sumIndex, unsigned elementEnd)
{
    DecoderSum& sum = Lanes[laneIndex].Sums[sumIndex];

    SIAMESE_DEBUG_ASSERT(sum.ElementStart <= sum.ElementEnd);
    SIAMESE_DEBUG_ASSERT((sum.ElementStart + ColumnStart) % kColumnLaneCount == laneIndex);
    SIAMESE_DEBUG_ASSERT((sum.ElementEnd + ColumnStart) % kColumnLaneCount == laneIndex);

    unsigned element = sum.ElementEnd;
    if (element >= elementEnd)
        return &sum.Buffer;

    // For each element to accumulate in this lane:
    do
    {
        SIAMESE_DEBUG_ASSERT((element + ColumnStart) % kColumnLaneCount == laneIndex);
        OriginalPacket* original     = GetWindowElement(element);
        const unsigned originalBytes = original->Buffer.Bytes;

        Logger.Info("Lane ", laneIndex,  " sum ",  sumIndex,  " accumulating column: ",  element + ColumnStart,  ". Got = ",  (originalBytes > 0));

        if (originalBytes > 0)
        {
            SIAMESE_DEBUG_ASSERT(original->Column % kColumnLaneCount == laneIndex);
            if (originalBytes > sum.Buffer.Bytes)
            {
                // Grow sum to encompass the original data
                if (!sum.Buffer.GrowZeroPadded(TheAllocator, originalBytes))
                {
                    EmergencyDisabled = true;
                    goto ExitSum;
                }
            }

            // Sum += PacketData
            if (sumIndex == 0)
                gf256_add_mem(sum.Buffer.Data, original->Buffer.Data, originalBytes);
            else
            {
                uint8_t CX = GetColumnValue(original->Column);
                if (sumIndex == 2)
                    CX = gf256_sqr(CX);

                // Sum += CX * PacketData
                gf256_muladd_mem(sum.Buffer.Data, CX, original->Buffer.Data, originalBytes);
            }
        }

        SIAMESE_DEBUG_ASSERT(original->Buffer.Bytes == 0 || original->Column % kColumnLaneCount == laneIndex);
        element += kColumnLaneCount;
    } while (element < elementEnd);

    SIAMESE_DEBUG_ASSERT((element + ColumnStart) % kColumnLaneCount == laneIndex);

    sum.ElementEnd = element;

ExitSum:
    return &sum.Buffer;
}

/*
    IdentifyRemovalPoint

    This routine identifies where data can be removed from the window,
    without removing anything that is still useful for recovery.

    The LDPC/Cauchy start columns are where data needs to be kept for
    certain because that is where individual packet data is required.
    For the Siamese running sums, data in the sums can be removed as
    long as all the sums are accumulated past the removal point.
    So we identify the first column to keep and roll the sums up past
    that point.

    If we have any recovery packets stored, the metadata will describe
    the LDPC/Cauchy start column.  If no packets are stored, then the
    newest recovery packet we have received can be used.
*/
bool DecoderPacketWindow::IdentifyRemovalPoint(RemovalPoint& pointOut)
{
    pointOut = RemovalPoint();

    // Quick sanity check to make sure we do not remove too much
    if (NextExpectedElement < kDecoderRemoveThreshold)
        return false;

    // If there are no recovery packets in the list:
    if (RecoveryPackets->IsEmpty())
    {
        // If there has not been a recent recovery packet:
        if (RecoveryPackets->LastRecovery.IsEmpty())
        {
            SIAMESE_DEBUG_BREAK(); // Should never get here
            return false;
        }

        // Use the most recent one
        pointOut = RecoveryPackets->LastRecovery;

        // FIXME: If we never send any recovery packets we still need to remove data from the window eventually
        /*
            Idea: ACK-ACKs - Make acknowledgement messages acknowledged.
            Benefits also include long-tail tail latency improvement.
            Include in the ACK-ACK message the recovery index so we can eliminate old data.
        */

        // Return true if there are at least kDecoderRemoveThreshold elements to remove
        return pointOut.FirstKeptElement >= kDecoderRemoveThreshold;
    }

    // Search for the left-most edge of the recovery matrix
    const RecoveryPacket* recovery = RecoveryPackets->Head;
    pointOut.FirstKeptElement      = recovery->ElementStart;
    pointOut.InitialRecoveryBytes  = recovery->Buffer.Bytes;
    bool onlyCauchy = true;

#ifdef SIAMESE_ENABLE_CAUCHY
    goto StartRecoveryListCheck;

    for (; recovery; recovery = recovery->Next)
    {
        if (pointOut.FirstKeptElement > recovery->ElementStart)
            pointOut.FirstKeptElement = recovery->ElementStart;
        if (pointOut.InitialRecoveryBytes < recovery->Buffer.Bytes)
            pointOut.InitialRecoveryBytes = recovery->Buffer.Bytes;

StartRecoveryListCheck:
        // Skip Cauchy and parity rows
        if (recovery->Metadata.SumCount <= SIAMESE_CAUCHY_THRESHOLD)
            continue;

        pointOut.SumStartColumn = recovery->Metadata.ColumnStart;
        onlyCauchy = false;

        // Note: The first Siamese row will have the smallest ElementStart, so we can stop searching here
        break;
    }

    // Scan the rest of the list because the LDPC range can shrink.
    // (The list is only ordered by the sum region.)
    for (; recovery; recovery = recovery->Next)
    {
        if (pointOut.FirstKeptElement > recovery->ElementStart)
            pointOut.FirstKeptElement = recovery->ElementStart;
        if (pointOut.InitialRecoveryBytes < recovery->Buffer.Bytes)
            pointOut.InitialRecoveryBytes = recovery->Buffer.Bytes;
    }
#else
    pointOut.SumStartColumn = recovery->Metadata.ColumnStart;
    onlyCauchy = false;
#endif

    SIAMESE_DEBUG_ASSERT(!InvalidElement(pointOut.FirstKeptElement));

    // If recovery list only contains Cauchy rows:
    if (onlyCauchy)
    {
        // Note that if we see Cauchy rows again it means the encoder reset its sums small enough to
        // start sending those again, so we can use that as an indicator to reset ours also.
        // Reset the sum column count to zero, which will prevent us from rolling up any running sums.
        // Furthermore when a sum is encountered it will be reset during recovery.
        SumColumnCount = 0;
    }

#ifdef SIAMESE_DEBUG
    // Verify this is correct
    for (RecoveryPacket* testPacket = RecoveryPackets->Head; testPacket; testPacket = testPacket->Next)
    {
#ifdef SIAMESE_ENABLE_CAUCHY
        // Skip Cauchy and parity rows
        if (testPacket->Metadata.SumCount <= SIAMESE_CAUCHY_THRESHOLD)
            continue;
#endif
        SIAMESE_DEBUG_ASSERT(pointOut.FirstKeptElement <= testPacket->ElementStart);
        SIAMESE_DEBUG_ASSERT(!IsColumnDeltaNegative(SubtractColumns(testPacket->Metadata.ColumnStart, pointOut.SumStartColumn)));
    }
#endif

    // Return true if there are at least kDecoderRemoveThreshold elements to remove
    return pointOut.FirstKeptElement >= kDecoderRemoveThreshold;
}

void DecoderPacketWindow::RemoveElements()
{
    // Abort if we cannot identify a valid removal point
    RemovalPoint removalPoint;
    if (!IdentifyRemovalPoint(removalPoint))
        return;

    const unsigned firstKeptSubwindow  = removalPoint.FirstKeptElement / kSubwindowSize;
    const unsigned removedElementCount = firstKeptSubwindow * kSubwindowSize;
    SIAMESE_DEBUG_ASSERT(firstKeptSubwindow >= 1);
    SIAMESE_DEBUG_ASSERT(removedElementCount % kColumnLaneCount == 0);
    SIAMESE_DEBUG_ASSERT(removedElementCount <= NextExpectedElement);

    Logger.Info("********* Removing up to ", removedElementCount);

    // If there is a running sum:
    if (IsRunningSums())
    {
        // If the sum start point is changing:
        if (SumColumnStart != removalPoint.SumStartColumn)
        {
            // If the new sum start point is already clipped:
            const unsigned elementStart = ColumnToElement(removalPoint.SumStartColumn);
            if (InvalidElement(elementStart))
            {
                EmergencyDisabled = true;
                Logger.Error("RemoveElements failed: Removal point sum start is clipped! removalPoint.SumStartColumn=", removalPoint.SumStartColumn, ", ColumnStart=", ColumnStart);
                SIAMESE_DEBUG_BREAK(); // Should never happen
                return;
            }

            ResetSums(elementStart);

            SumColumnStart = removalPoint.SumStartColumn;
            SumColumnCount = removalPoint.SumColumnCount;
        }
        else
        {
            // Determine sum start element
            unsigned sumElementStart = ColumnToElement(removalPoint.SumStartColumn);
            if (InvalidElement(sumElementStart))
                sumElementStart = 0;

            if (!StartSums(sumElementStart, removalPoint.InitialRecoveryBytes))
            {
                Logger.Error("RemoveElements.StartSums failed. removalPoint.SumStartColumn=", removalPoint.SumStartColumn, ", sumElementStart=", sumElementStart, ", bytes=", removalPoint.InitialRecoveryBytes);
                EmergencyDisabled = true;
                return;
            }
        }

        // Roll up all the sums past the point of removal
        for (unsigned laneIndex = 0; laneIndex < kColumnLaneCount; ++laneIndex)
        {
            for (unsigned sumIndex = 0; sumIndex < kColumnSumCount; ++sumIndex)
            {
                GetSum(laneIndex, sumIndex, removedElementCount);

                // If the start element is getting clipped:
                if (Lanes[laneIndex].Sums[sumIndex].ElementStart >= removedElementCount)
                    Lanes[laneIndex].Sums[sumIndex].ElementStart -= removedElementCount;
                else
                    Lanes[laneIndex].Sums[sumIndex].ElementStart = laneIndex;

                SIAMESE_DEBUG_ASSERT(Lanes[laneIndex].Sums[sumIndex].ElementEnd >= removedElementCount);
                Lanes[laneIndex].Sums[sumIndex].ElementEnd -= removedElementCount;
            }
        }
    }

    // Reset windows before putting them on the back
    for (unsigned i = 0; i < firstKeptSubwindow; ++i)
        Subwindows[i]->Reset();

    // Shift kept subwindows to the front of the vector
    // Note: Removed entries get rotated to the end
    std::rotate(Subwindows.begin(), Subwindows.begin() + firstKeptSubwindow, Subwindows.end());

    // Update the count of elements in the window
    SIAMESE_DEBUG_ASSERT(Count >= removedElementCount);
    Count -= removedElementCount;

    // Roll up the ColumnStart member
    ColumnStart = ElementToColumn(removedElementCount);
    SIAMESE_DEBUG_ASSERT(ColumnStart == Subwindows[0]->Originals[0].Column || Subwindows[0]->Originals[0].Buffer.Bytes == 0);

    // Roll up the FirstUnremovedElement member
    SIAMESE_DEBUG_ASSERT(NextExpectedElement >= removedElementCount);
    NextExpectedElement -= removedElementCount;

    // Decrement element counters
    RecoveryPackets->DecrementElementCounters(removedElementCount);
    CheckedRegion->DecrementElementCounters(removedElementCount);
    RecoveryMatrix->DecrementElementCounters(removedElementCount);
}


//------------------------------------------------------------------------------
// RecoveryMatrixState

void RecoveryMatrixState::Reset()
{
    Columns.clear();
    Rows.clear();
    Pivots.clear();

    Matrix.Clear();

    PreviousNextCheckStart = 0;
    GEResumePivot = 0;
}

void RecoveryMatrixState::DecrementElementCounters(const unsigned elementCount)
{
    if (PreviousNextCheckStart > elementCount)
        PreviousNextCheckStart -= elementCount;
    else
        PreviousNextCheckStart = 0;
}

void RecoveryMatrixState::PopulateColumns(const unsigned oldColumns, const unsigned newColumns)
{
    if (oldColumns >= newColumns)
        return;

    Columns.resize(newColumns);

    // Resume adding from the last stop point
    unsigned elementStart     = PreviousNextCheckStart;
    PreviousNextCheckStart    = CheckedRegion->NextCheckStart;
    const unsigned elementEnd = CheckedRegion->NextCheckStart;
    if (elementStart < CheckedRegion->ElementStart)
        elementStart = CheckedRegion->ElementStart;

    // The column count increased which means we should have some columns to check
    SIAMESE_DEBUG_ASSERT(elementStart < elementEnd);

    // Check the current subwindow for next lost packet:
    const unsigned subwindowEnd = (elementEnd + kSubwindowSize - 1) / kSubwindowSize;
    unsigned subwindowIndex     = elementStart / kSubwindowSize;

    unsigned bitIndex = elementStart % kSubwindowSize;
    unsigned column   = oldColumns;

    while (subwindowIndex < subwindowEnd)
    {
        SIAMESE_DEBUG_ASSERT(subwindowIndex < (unsigned)Window->Subwindows.size());

        DecoderSubwindow& subwindow = *Window->Subwindows[subwindowIndex];

        // If there may be any lost packets in this subwindow:
        if (subwindow.GotCount < kSubwindowSize)
        {
            do
            {
                // Seek next clear bit
                bitIndex = subwindow.Got.FindFirstClear(bitIndex);

                // If there were none, skip this subwindow
                if (bitIndex >= kSubwindowSize)
                    break;

                // Calculate element index and stop if we hit the end of the valid data
                const unsigned element = subwindowIndex * kSubwindowSize + bitIndex;
                SIAMESE_DEBUG_ASSERT(element < elementEnd);

                ColumnInfo& columnInfo = Columns[column];
                columnInfo.Column      = Window->ElementToColumn(element);
                columnInfo.Original    = &subwindow.Originals[bitIndex];
                columnInfo.CX          = GetColumnValue(columnInfo.Column);

                // Point lost original packet to recovery matrix column
                SIAMESE_DEBUG_ASSERT(columnInfo.Original->Buffer.Bytes == 0);
                columnInfo.Original->Column = column;

                // If we just added the last column:
                if (++column >= newColumns)
                    return;

            } while (++bitIndex < kSubwindowSize);
        }

        // Reset bit index to the front of the next subwindow
        bitIndex = 0;

        // Check next subwindow
        ++subwindowIndex;
    }

    SIAMESE_DEBUG_BREAK(); // Should never get here
}

void RecoveryMatrixState::PopulateRows(const unsigned oldRows, const unsigned newRows)
{
    if (oldRows >= newRows)
        return;

    Rows.resize(newRows);

    RecoveryPacket* recovery;
    if (oldRows > 0)
        recovery = Rows[oldRows - 1].Recovery->Next;
    else
        recovery = CheckedRegion->FirstRecovery;
    SIAMESE_DEBUG_ASSERT(recovery);

    for (unsigned rowIndex = oldRows; rowIndex < newRows; ++rowIndex, recovery = recovery->Next)
    {
        RowInfo& rowInfo = Rows[rowIndex];
        rowInfo.Recovery          = recovery;
        rowInfo.UsedForSolution   = false;
        rowInfo.MatrixColumnCount = recovery->LostCount;

        Logger.Info("*** Recovery packet: start=", recovery->Metadata.ColumnStart,  " Sum_Count=",  recovery->Metadata.SumCount,  " LDPC_Count=",  recovery->Metadata.LDPCCount);
    }
}

bool RecoveryMatrixState::GenerateMatrix()
{
    const unsigned columns = CheckedRegion->LostCount;
    const unsigned rows    = CheckedRegion->RecoveryCount;
    SIAMESE_DEBUG_ASSERT(rows >= columns);

    unsigned oldRows    = (unsigned)Rows.size();
    unsigned oldColumns = (unsigned)Columns.size();

    // If we missed a reset somewhere:
    if (rows < oldRows || columns < oldColumns)
    {
        SIAMESE_DEBUG_BREAK(); // Should never happen
        Reset();
        oldRows    = 0;
        oldColumns = 0;
    }

    bool matrixAllocated = false;
    if (oldRows == 0)
        matrixAllocated = Matrix.Initialize(TheAllocator, rows, columns);
    else
        matrixAllocated = Matrix.Resize(TheAllocator, rows, columns);
    if (!matrixAllocated)
    {
        Reset();
        return false;
    }

    PopulateColumns(oldColumns, columns);
    PopulateRows(oldRows, rows);

    const unsigned stride = Matrix.AllocatedColumns;
    uint8_t* rowData      = Matrix.Data;
    unsigned startRow     = 0;

    // If we need to fill to the right, start at the top
    if (columns <= oldColumns)
    {
        startRow = oldRows;
        rowData += startRow * stride;
    }

    std::ostringstream* pDebugMsg = nullptr;

    // For each row to fill:
    for (unsigned i = startRow; i < rows; ++i, rowData += stride)
    {
        RecoveryPacket* recovery        = Rows[i].Recovery;
        const RecoveryMetadata metadata = recovery->Metadata;

#ifdef SIAMESE_ENABLE_CAUCHY
        // If this is a Cauchy or parity row:
        if (metadata.SumCount <= SIAMESE_CAUCHY_THRESHOLD)
        {
            const unsigned startMatrixColumn = (i < oldRows) ? oldColumns : 0;

            if (Logger.ShouldLog(logger::Level::Debug))
            {
                delete pDebugMsg;
                pDebugMsg = new std::ostringstream();
                *pDebugMsg << "Recovery row (" << (metadata.Row == 0 ? "Parity" : "Cauchy") << "): ";
            }

            // Fill columns from left for new rows:
            for (unsigned j = startMatrixColumn; j < columns; ++j)
            {
                const unsigned column  = Columns[j].Column;
                SIAMESE_DEBUG_ASSERT(column >= metadata.ColumnStart);
                const unsigned element = SubtractColumns(column, metadata.ColumnStart);

                // If we hit the end of the recovery packet data:
                if (element >= metadata.SumCount)
                {
                    for (; j < columns; ++j)
                        rowData[j] = 0;
                    break;
                }

                if (pDebugMsg)
                    *pDebugMsg << column << " ";

                const unsigned value = (metadata.Row == 0) ? 1 : CauchyElement(metadata.Row - 1, column % kCauchyMaxColumns);
                rowData[j] = (uint8_t)value;
            }

            if (pDebugMsg)
                Logger.Debug(pDebugMsg->str());

            continue;
        }
#endif // SIAMESE_ENABLE_CAUCHY

        // Calculate row multiplier RX
        const uint8_t RX = GetRowValue(metadata.Row);

        if (Logger.ShouldLog(logger::Level::Debug))
        {
            delete pDebugMsg;
            pDebugMsg = new std::ostringstream();
            *pDebugMsg << "Recovery row (Siamese): ";
        }

        const unsigned startMatrixColumn = (i < oldRows) ? oldColumns : 0;

        // Fill columns from left for new rows:
        for (unsigned j = startMatrixColumn; j < columns; ++j)
        {
            const unsigned column  = Columns[j].Column;
            SIAMESE_DEBUG_ASSERT(column >= metadata.ColumnStart);
            const unsigned element = SubtractColumns(column, metadata.ColumnStart);

            // If we hit the end of the recovery packet data:
            if (element >= metadata.SumCount)
            {
                for (; j < columns; ++j)
                    rowData[j] = 0;
                break;
            }

            if (pDebugMsg)
                *pDebugMsg << column << " ";

            // Generate opcode and parameters
            const uint8_t CX      = Columns[j].CX;
            const uint8_t CX2     = gf256_sqr(CX);
            const unsigned lane   = column % kColumnLaneCount;
            const unsigned opcode = GetRowOpcode(lane, metadata.Row);

            unsigned value = 0;

            // Interpret opcode to calculate matrix row element j
            if (opcode & 1)
                value ^= 1;
            if (opcode & 2)
                value ^= CX;
            if (opcode & 4)
                value ^= CX2;
            if (opcode & 8)
                value ^= RX;
            if (opcode & 16)
                value ^= gf256_mul(CX, RX);
            if (opcode & 32)
                value ^= gf256_mul(CX2, RX);

            rowData[j] = (uint8_t)value;
        }

        if (pDebugMsg)
            Logger.Debug(pDebugMsg->str());

        PCGRandom prng;
        prng.Seed(metadata.Row, metadata.LDPCCount);

        const unsigned elementStart = recovery->ElementStart;
        const unsigned pairCount    = (metadata.LDPCCount + kPairAddRate - 1) / kPairAddRate;
        SIAMESE_DEBUG_ASSERT(metadata.SumCount >= metadata.LDPCCount);

        Logger.Trace("(Generate matrix) LDPC columns: ");

        for (unsigned k = 0; k < pairCount; ++k)
        {
            const unsigned element1   = elementStart + (prng.Next() % metadata.LDPCCount);
            OriginalPacket* original1 = Window->GetWindowElement(element1);
            if (original1->Buffer.Bytes <= 0)
            {
                // Note: packet->Column is set to the recovery matrix column for lost data in PopulateColumns()
                const unsigned matrixColumn = original1->Column;
                SIAMESE_DEBUG_ASSERT(matrixColumn < columns);
                if (matrixColumn >= startMatrixColumn)
                    rowData[matrixColumn] ^= 1;
            }

            const unsigned elementRX   = elementStart + (prng.Next() % metadata.LDPCCount);
            OriginalPacket* originalRX = Window->GetWindowElement(elementRX);
            if (originalRX->Buffer.Bytes <= 0)
            {
                const unsigned matrixColumn = originalRX->Column;
                SIAMESE_DEBUG_ASSERT(matrixColumn < columns);
                if (matrixColumn >= startMatrixColumn)
                    rowData[matrixColumn] ^= RX;
            }

            Logger.Trace(element1, " ", elementRX); // If we use this for testing may want to stringstream it
        } // for each bundle of random columns
    } // for each recovery row

    // Fill in revealed column pivots with their own value
    Pivots.resize(rows);
    for (unsigned i = oldRows; i < rows; ++i)
        Pivots[i] = i;

    // If we have already performed some GE, then we need to eliminate new
    // row data and we need to carry on elimination for new columns
    if (GEResumePivot > 0)
    {
#ifdef SIAMESE_DEBUG
        // Check: Verify that newly exposed columns in old rows are zero
        for (unsigned i = 0; i < oldRows; ++i)
        {
            for (unsigned j = oldColumns; j < columns; ++j)
            {
                const uint8_t* ge_row = Matrix.Data + Matrix.AllocatedColumns * i;
                SIAMESE_DEBUG_ASSERT(ge_row[j] == 0);
            }
        }
#endif
        ResumeGE(oldRows, rows);
    }

#ifdef SIAMESE_DEBUG
    // Check: Verify zeroes after matrix rows
    for (unsigned i = 0; i < rows; ++i)
    {
        unsigned j;
        for (j = columns; j > 0; --j)
        {
            uint8_t v = Matrix.Get(i, j - 1);
            if (v != 0)
                break;
        }
        const unsigned expectedLossCount = j;
        SIAMESE_DEBUG_ASSERT(Rows[i].Recovery->LostCount >= expectedLossCount || GEResumePivot > 0);
        SIAMESE_DEBUG_ASSERT(Rows[i].MatrixColumnCount >= expectedLossCount);
    }
#endif

    return true;
}

void RecoveryMatrixState::ResumeGE(const unsigned oldRows, const unsigned rows)
{
    // If we did not add any new rows:
    if (oldRows >= rows)
    {
        SIAMESE_DEBUG_ASSERT(oldRows == rows);
        return;
    }

    const unsigned stride = Matrix.AllocatedColumns;

    // For each pivot we have determined already:
    for (unsigned pivot_i = 0; pivot_i < GEResumePivot; ++pivot_i)
    {
        // Get the row for that pivot
        const unsigned matrixRowIndex_i = Pivots[pivot_i];
        const uint8_t* ge_row = Matrix.Data + stride * matrixRowIndex_i;
        const uint8_t val_i   = ge_row[pivot_i];
        SIAMESE_DEBUG_ASSERT(val_i != 0);

        const unsigned pivotColumnCount = Rows[matrixRowIndex_i].MatrixColumnCount;

        uint8_t* rem_row = Matrix.Data + stride * oldRows;

        // For each new row that was added:
        for (unsigned newRowIndex = oldRows; newRowIndex < rows; ++newRowIndex, rem_row += stride)
        {
            if (EliminateRow(ge_row, rem_row, pivot_i, pivotColumnCount, val_i))
            {
                // Grow the column count of this row if we just filled it in on the right
                if (Rows[newRowIndex].MatrixColumnCount < pivotColumnCount)
                    Rows[newRowIndex].MatrixColumnCount = pivotColumnCount;
            }
            SIAMESE_DEBUG_ASSERT(Pivots[newRowIndex] == newRowIndex);
        }
    }
}

bool RecoveryMatrixState::GaussianElimination()
{
    // Attempt to solve as much of the matrix as possible without using a pivots array
    // since that requires extra memory operations.  Since the matrix will be dense we
    // have a good chance of going pretty far before we hit a zero

    if (GEResumePivot > 0)
        return PivotedGaussianElimination(GEResumePivot);

    const unsigned columns = Matrix.Columns;
    const unsigned stride  = Matrix.AllocatedColumns;
    const unsigned rows    = Matrix.Rows;
    uint8_t* ge_row        = Matrix.Data;

    for (unsigned pivot_i = 0; pivot_i < columns; ++pivot_i, ge_row += stride)
    {
        const uint8_t val_i = ge_row[pivot_i];
        if (val_i == 0)
            return PivotedGaussianElimination(pivot_i);

        RowInfo& rowInfo = Rows[pivot_i];
        rowInfo.UsedForSolution = true;
        const unsigned pivotColumnCount = rowInfo.MatrixColumnCount;

        uint8_t* rem_row = ge_row;

        // For each remaining row:
        for (unsigned pivot_j = pivot_i + 1; pivot_j < rows; ++pivot_j)
        {
            rem_row += stride;
            if (EliminateRow(ge_row, rem_row, pivot_i, pivotColumnCount, val_i))
            {
#ifdef SIAMESE_DECODER_TRACK_ZERO_COLUMNS
                // Grow the column count of this row if we just filled it in on the right
                if (Rows[pivot_j].MatrixColumnCount < pivotColumnCount)
                    Rows[pivot_j].MatrixColumnCount = pivotColumnCount;
#endif
            }
        }
    }

    return true;
}

bool RecoveryMatrixState::PivotedGaussianElimination(unsigned pivot_i)
{
    const unsigned columns = Matrix.Columns;
    const unsigned stride  = Matrix.AllocatedColumns;
    const unsigned rows    = Matrix.Rows;

    // Resume from next row down...
    // Note: This is designed to be called by the non-pivoted version
    unsigned pivot_j = pivot_i + 1;
    goto UsePivoting;

    // For each pivot to determine:
    for (; pivot_i < columns; ++pivot_i)
    {
        pivot_j = pivot_i;
UsePivoting:
        for (; pivot_j < rows; ++pivot_j)
        {
            const unsigned matrixRowIndex_j = Pivots[pivot_j];
            const uint8_t* ge_row = Matrix.Data + stride * matrixRowIndex_j;
            const uint8_t val_i = ge_row[pivot_i];
            if (val_i == 0)
                continue;

            // Swap out the pivot index for this one
            if (pivot_i != pivot_j)
            {
                const unsigned temp = Pivots[pivot_i];
                Pivots[pivot_i] = Pivots[pivot_j];
                Pivots[pivot_j] = temp;
            }

            RowInfo& rowInfo = Rows[matrixRowIndex_j];
            rowInfo.UsedForSolution = true;
            const unsigned pivotColumnCount = rowInfo.MatrixColumnCount;

            // Skip eliminating extra rows in the case that we just solved the matrix
            if (pivot_i >= columns - 1)
                return true;

            // For each remaining row:
            for (unsigned pivot_k = pivot_i + 1; pivot_k < rows; ++pivot_k)
            {
                const unsigned matrixRowIndex_k = Pivots[pivot_k];
                uint8_t* rem_row = Matrix.Data + stride * matrixRowIndex_k;
                if (EliminateRow(ge_row, rem_row, pivot_i, pivotColumnCount, val_i))
                {
                    // Grow the column count of this row if we just filled it in on the right
                    if (Rows[matrixRowIndex_k].MatrixColumnCount < pivotColumnCount)
                        Rows[matrixRowIndex_k].MatrixColumnCount = pivotColumnCount;
                }
            }

            goto NextPivot;
        }

        // Remember where we failed last time
        GEResumePivot = pivot_i;

        return false;
NextPivot:;
    }

    return true;
}


//------------------------------------------------------------------------------
// CheckedRegionState

void CheckedRegionState::Reset()
{
    ElementStart   = 0;
    NextCheckStart = 0;
    FirstRecovery  = nullptr;
    LastRecovery   = nullptr;
    RecoveryCount  = 0;
    LostCount      = 0;
    SolveFailed    = false;

    RecoveryMatrix->Reset();
}

void CheckedRegionState::DecrementElementCounters(const unsigned elementCount)
{
    if (ElementStart < elementCount || NextCheckStart < elementCount)
    {
        Logger.Warning("Just clipped the checked region state -- reset");
        Reset();
        return;
    }

    ElementStart -= elementCount;
    NextCheckStart -= elementCount;
}


//------------------------------------------------------------------------------
// RecoveryPacketList

void RecoveryPacketList::Insert(RecoveryPacket* recovery)
{
    RecoveryPacket* prev = Tail;
    RecoveryPacket* next = nullptr;

    const unsigned recoveryStart = recovery->Metadata.ColumnStart;
    const unsigned recoveryEnd   = recovery->ElementEnd;

    // Search for insertion point:
    for (; prev; next = prev, prev = prev->Prev)
    {
        const unsigned prevStart = prev->Metadata.ColumnStart;
        const unsigned prevEnd   = prev->ElementEnd;

        /*
            This insertion order guarantees that the left and right side of
            the recovery input ranges are monotonically increasing as in:

                recovery 0: 012345
                recovery 1:   23456 <- Cauchy row
                recovery 2: 01234567
                recovery 3:     45678
                recovery 4:     456789
        */
        if (recoveryEnd >= prevEnd)
        {
            if (recoveryEnd > prevEnd)
                break;
            if (IsColumnDeltaNegative(SubtractColumns(recoveryStart, prevStart)))
                break;
        }
    }

    // Insert into linked list
    recovery->Next = next;
    recovery->Prev = prev;
    if (prev)
        prev->Next = recovery;
    else
        Head = recovery;
    if (next)
        next->Prev = recovery;
    else
        Tail = recovery;

    // If inserting at head or somewhere in the middle:
    // Invalidate the checked region because a smaller solution may be available
    if (!prev || next)
        CheckedRegion->Reset();
    // Note that for the case where we insert at the end of a non-empty list we do
    // not reset the checked region.  This is the common case where recovery data is
    // received in order.

    ++RecoveryPacketCount;

    // Update last recovery data
    LastRecovery.FirstKeptElement     = recovery->ElementStart;
    LastRecovery.InitialRecoveryBytes = recovery->Buffer.Bytes;
    LastRecovery.SumColumnCount       = recovery->Metadata.SumCount;
    LastRecovery.SumStartColumn       = recovery->Metadata.ColumnStart;
}

void RecoveryPacketList::DeletePacketsBefore(const unsigned element)
{
    RecoveryPacket* recovery = Head;
    unsigned deleteCount     = 0;

    // Examine recovery packets starting with the oldest
    for (RecoveryPacket* next; recovery; recovery = next)
    {
        // Stop once we eclipse the element
        if (recovery->ElementEnd > element)
            break;

        next = recovery->Next;
        recovery->Buffer.Free(TheAllocator);
        TheAllocator->Destruct(recovery);
        ++deleteCount;
    }

    Head = recovery;
    if (recovery)
    {
        recovery->Prev = nullptr;
        RecoveryPacketCount -= deleteCount;
    }
    else
    {
        Tail = nullptr;
        RecoveryPacketCount = 0;
    }
}

void RecoveryPacketList::DecrementElementCounters(const unsigned elementCount)
{
    for (RecoveryPacket* recovery = Head; recovery; recovery = recovery->Next)
    {
        SIAMESE_DEBUG_ASSERT(recovery->ElementEnd >= elementCount);
        recovery->ElementEnd -= elementCount;

        SIAMESE_DEBUG_ASSERT(recovery->ElementStart >= elementCount);
        recovery->ElementStart -= elementCount;
    }

    // If we didn't clip the last recovery packet data:
    if (LastRecovery.FirstKeptElement >= elementCount)
        LastRecovery.FirstKeptElement -= elementCount;
    else
    {
        Logger.Warning("Just clipped off the last recovery packet data from RecoveryPacketList");
        LastRecovery = RemovalPoint();
    }
}


} // namespace siamese
