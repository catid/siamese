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
using namespace std;

#include "../SiameseTools.h"
#include "../gf256.h"

#define ENABLE_GENTAB_PRIMES
#define VERBOSE_PRIMES_TABLE_CREATION

//#define SKIP_EVEN_PRIMES

#ifdef VERBOSE_PRIMES_TABLE_CREATION
#define PT_LOG(x) x
#else
#define PT_LOG(x)
#endif

static uint8_t int2gray(uint8_t num)
{
    return num ^ (num >> 1);
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

static void GeneratePrimesTable()
{
    uint8_t Primes[256];
    int PrimeCount = 0;

    for (int i = 0; i < 256; ++i)
    {
        uint8_t x = (uint8_t)i;

#ifdef SKIP_EVEN_PRIMES
        // Skip even table entries, since they're similar to the odd ones
        if ((i & 1) == 0)
        {
            continue;
        }
#endif

        PT_LOG(cout << endl << "Table for i = " << i << " : 01";)

        uint8_t table[12];
        table[0] = 1;
        for (int j = 1; j < 12; ++j)
        {
            table[j] = gf256_mul(table[j - 1], x);
            PT_LOG(cout << " " << setw(2) << setfill('0') << hex << (int)table[j];)
        }

        PT_LOG(cout << endl;)
#if 0
        if (x != table[0])
        {
            cout << "Table entry 9 does not repeat entry 0!" << endl;
            assert(false);
        }
#endif

        for (int j = 0; j < 12; ++j)
        {
            uint8_t y = table[j];
            for (int k = j + 1; k < 12; ++k)
            {
                if (table[k] == y)
                {
                    PT_LOG(cout << "Cycle between " << j << " and " << k << endl;)
                    break;
                }
            }
        }

        uint8_t output[256];
        PT_LOG(cout << endl << "Linear combinations :";)

        for (int j = 0; j < 256; ++j)
        {
            uint8_t z = 0;
            uint8_t g = int2gray((uint8_t)j);

            uint8_t mask = 1;
            for (int k = 0; k < 8; ++k)
            {
                if (0 != (g & mask))
                {
                    z ^= table[k];
                }

                mask <<= 1;
            }

            output[j] = z;

            PT_LOG(cout << " " << setw(2) << setfill('0') << hex << (int)z;)
        }

        PT_LOG(cout << endl;)

        bool prime = true;
        for (int j = 0; j < 256; ++j)
        {
            uint8_t y = output[j];
            for (int k = j + 1; k < 256; ++k)
            {
                if (output[k] == y)
                {
                    PT_LOG(cout << "Early cycle between " << j << " and " << k << endl;)
                    prime = false;
                    goto done_checking_cycles;
                }
            }
        }
    done_checking_cycles:;

        if (prime)
        {
            Primes[PrimeCount++] = (uint8_t)i;

            cout << "Prime " << PrimeCount << " generator sequence: ";
            for (int j = 0; j < 12; ++j)
            {
                cout << " " << setw(2) << setfill('0') << hex << (int)table[j];
            }
            cout << endl;
        }
    }

    cout << "static const int PrimesCount = " << dec << PrimeCount << ";" << endl;
    cout << "static const uint8_t Primes[PrimesCount] = {";
    for (int i = 0; i < PrimeCount; ++i)
    {
        if (i % 16 == 0)
        {
            cout << endl << "\t";
        }
        cout << "0x" << setw(2) << setfill('0') << hex << (int)Primes[i] << ", ";
    }
    cout << endl << "};" << endl;

    siamese::PCGRandom prng;
    prng.Seed(0);
    uint16_t indices[256];
    ShuffleDeck16(prng, indices, PrimeCount);

    cout << "static const uint8_t ShuffledPrimes[PrimesCount] = {";
    for (int i = 0; i < PrimeCount; ++i)
    {
        if (i % 16 == 0)
        {
            cout << endl << "\t";
        }
        cout << "0x" << setw(2) << setfill('0') << hex << (int)Primes[indices[i]] << ", ";
    }
    cout << endl << "};" << endl;
}


#ifdef ENABLE_GENTAB_PRIMES

int main()
{
    if (0 != gf256_init())
    {
        cout << "Failed to initialize gf256" << endl;
        return -1;
    }

    GeneratePrimesTable();

    return 0;
}

#endif // ENABLE_GENTAB_PRIMES
