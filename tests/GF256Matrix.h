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

#pragma once

#include <stdint.h>

class GF256Matrix
{
public:
    GF256Matrix();
    ~GF256Matrix();

    inline uint8_t* Get(int row, int col)
    {
        return _matrix + row * _cols + col;
    }

    inline uint8_t* GetFront() { return _matrix; }
    inline int GetPitch() { return _cols; }

    inline int GetRows() { return _rows; }
    inline int GetCols() { return _cols; }

    inline int Size() { return _rows * _cols; }

    bool Initialize(int rows, int cols);

    void Zero();

    // Works for matrices with more rows than columns
    // Returns -1 for no success
    // Returns number of rows that were linearly related to the rest
    int Solve();

    void Print(int count = 0x7fffffff);

protected:
    int _rows = 0, _cols = 0;
    uint8_t* _matrix = nullptr;
    int* _pivot = nullptr;

    void cleanup();
};
