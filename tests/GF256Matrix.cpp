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

#include "GF256Matrix.h"

#include <string.h>
#include "../gf256.h"

#include <iostream>
#include <iomanip>
using namespace std;


//-----------------------------------------------------------------------------
// GF256Matrix

GF256Matrix::GF256Matrix()
{
    _matrix = nullptr;
}

GF256Matrix::~GF256Matrix()
{
    cleanup();
}

bool GF256Matrix::Initialize(int rows, int cols)
{
    if (rows <= 0 || cols <= 0)
        return false;

    cleanup();

    _rows = rows;
    _cols = cols;
    int bytes = rows * cols;
    _matrix = new (std::nothrow)uint8_t[bytes];
    if (!_matrix)
        return false;

    _pivot = new (std::nothrow)int[_rows];
    if (!_pivot)
        return false;

    Zero();

    return true;
}

void GF256Matrix::cleanup()
{
    if (_matrix)
    {
        delete[]_matrix;
        _matrix = nullptr;
    }
    if (_pivot)
    {
        delete[]_pivot;
        _pivot = nullptr;
    }
}

void GF256Matrix::Zero()
{
    memset(_matrix, 0, _rows * _cols);
}

void GF256Matrix::Print(int count)
{
    cout << endl << "GF256Matrix is (rows, cols = " << _rows << " x " << _cols << "):" << endl;

    for (int ii = 0; ii < _rows && ii < count; ++ii)
    {
        for (int jj = 0; jj < _cols; ++jj)
        {
            cout << hex << setfill('0') << setw(2) << (int)_matrix[_cols * ii + jj] << " ";
            //if (jj % 16 == 0) cout << endl;
        }
        cout << endl;
    }

    cout << dec << endl;
}

int GF256Matrix::Solve()
{
    int failures = 0;

    // Initialize pivot array
    for (int pivot_i = 0; pivot_i < _rows; ++pivot_i)
        _pivot[pivot_i] = pivot_i;
        //_pivot[pivot_i] = _rows - 1 - pivot_i;

    // For each pivot to determine:
    for (int pivot_i = 0; pivot_i < _cols; ++pivot_i)
    {
        failures = 0;

        bool found = false;
        for (int pivot_j = pivot_i; pivot_j < _rows; ++pivot_j)
        {
            int ge_row_j = _pivot[pivot_j];
            uint8_t *ge_row = _matrix + _cols * ge_row_j;

            if (ge_row[pivot_i] == 0)
            {
                if (pivot_j >= _cols - 1)
                {
                    ++failures;
                }
                else
                {
                    //cout << pivot_i << " of " << _cols << endl;
                }
            }
            else
            {
                found = true;

                // Swap out the pivot index for this one
                int temp = _pivot[pivot_i];
                _pivot[pivot_i] = _pivot[pivot_j];
                _pivot[pivot_j] = temp;

                // For each remaining unused row,
                for (int pivot_k = pivot_i + 1; pivot_k < _rows; ++pivot_k)
                {
                    int ge_row_k = _pivot[pivot_k];
                    uint8_t *rem_row = _matrix + _cols * ge_row_k;

                    if (rem_row[pivot_i])
                    {
                        uint8_t x = gf256_div(rem_row[pivot_i], ge_row[pivot_i]);

                        //gf256_muladd_mem(rem_row + pivot_i + 1, x, ge_row + pivot_i + 1, _cols - (pivot_i + 1));
                        gf256_muladd_mem(rem_row, x, ge_row, _cols);
                    }
                }

                break;
            }
        }

        // If pivot could not be found,
        if (!found)
        {
            return -1;
        }
    }

    return failures;
}
