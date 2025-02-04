//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 1998-2013 Sourcefire, Inc.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------
// sf_email_attach_decode.h author Bhagyashree Bantwal <bbantwal@cisco.com>

#ifndef SF_EMAIL_ATTACH_DECODE_H
#define SF_EMAIL_ATTACH_DECODE_H

// Email attachment decoder

#include <stdlib.h>

#include "main/snort_types.h"

#define MAX_BUF 65535

// FIXIT-L: Should make this a (scoped?) enum
#define DECODE_SUCCESS  0
#define DECODE_EXCEEDED  1 // Decode Complete when we reach the max depths
#define DECODE_FAIL    -1

// FIXIT-L: Should be a scoped enum
typedef enum
{
    DECODE_NONE = 0,
    DECODE_B64,
    DECODE_QP,
    DECODE_UU,
    DECODE_BITENC,
    DECODE_ALL
} DecodeType;

struct Base64_DecodeState
{
    uint32_t encode_bytes_read;
    uint32_t decode_bytes_read;
    int encode_depth;
    int decode_depth;
};

struct QP_DecodeState
{
    uint32_t encode_bytes_read;
    uint32_t decode_bytes_read;
    int encode_depth;
    int decode_depth;
};

struct UU_DecodeState
{
    uint32_t encode_bytes_read;
    uint32_t decode_bytes_read;
    int encode_depth;
    int decode_depth;
    uint8_t begin_found;
    uint8_t end_found;
};

struct BitEnc_DecodeState
{
    uint32_t bytes_read;
    int depth;
};

// Should be a C++ OOP struct with constructor, etc
struct Email_DecodeState
{
    DecodeType decode_type;
    uint8_t decode_present;
    uint32_t prev_encoded_bytes;
    uint32_t decoded_bytes;
    uint32_t buf_size;
    uint8_t* prev_encoded_buf;
    uint8_t* encodeBuf;
    uint8_t* decodeBuf;
    uint8_t* decodePtr;
    Base64_DecodeState b64_state;
    QP_DecodeState qp_state;
    UU_DecodeState uu_state;
    BitEnc_DecodeState bitenc_state;
};

struct MimeStats
{
    uint64_t memcap_exceeded;
    uint64_t attachments[DECODE_ALL];
    uint64_t decoded_bytes[DECODE_ALL];
};

int EmailDecode(const uint8_t* start, const uint8_t* end, Email_DecodeState*);

static inline int getCodeDepth(int code_depth, int64_t file_depth)
{
    if (file_depth < 0 )
        return code_depth;
    else if (( file_depth > MAX_BUF) || (!file_depth) )
        return 0;
    else if (file_depth > code_depth)
        return (int)file_depth;
    else
        return code_depth;
}

// FIXIT-L: Should be an (inline?) method of struct Email_DecodeState
static inline void SetEmailDecodeState(Email_DecodeState* ds, void* data, int buf_size,
    int b64_depth, int qp_depth, int uu_depth, int bitenc_depth, int64_t file_depth)
{
    ds->decode_type = DECODE_NONE;
    ds->decode_present = 0;
    ds->prev_encoded_bytes = 0;
    ds->prev_encoded_buf = nullptr;
    ds->decoded_bytes = 0;

    ds->encodeBuf = (uint8_t*)data;
    ds->decodeBuf = (uint8_t*)data + buf_size;
    ds->buf_size = buf_size;

    ds->b64_state.encode_depth = ds->b64_state.decode_depth = getCodeDepth(b64_depth, file_depth);
    ds->b64_state.encode_bytes_read = ds->b64_state.decode_bytes_read = 0;

    ds->qp_state.encode_depth = ds->qp_state.decode_depth = getCodeDepth(qp_depth, file_depth);
    ds->qp_state.encode_bytes_read = ds->qp_state.decode_bytes_read = 0;

    ds->uu_state.encode_depth = ds->uu_state.decode_depth = getCodeDepth(uu_depth, file_depth);
    ds->uu_state.encode_bytes_read = ds->uu_state.decode_bytes_read = 0;
    ds->uu_state.begin_found = 0;
    ds->uu_state.end_found = 0;

    ds->bitenc_state.depth = getCodeDepth(bitenc_depth, file_depth);
    ds->bitenc_state.bytes_read = 0;
}

// FIXIT-L: Should refactor as a constructor for struct Email_DecodeState
static inline Email_DecodeState* NewEmailDecodeState(
    int max_depth, int b64_depth, int qp_depth,
    int uu_depth, int bitenc_depth, int64_t file_depth)
{
    Email_DecodeState* ds = (Email_DecodeState*)calloc(1, sizeof(*ds) + (2*max_depth));

    if ( ds )
    {
        uint8_t* data = ((uint8_t*)ds) + sizeof(*ds);
        SetEmailDecodeState(
            ds, data, max_depth, b64_depth, qp_depth,
            uu_depth, bitenc_depth, file_depth);
    }
    return ds;
}

// FIXIT-L: Should refactor as a destructor for struct Email_DecodeState
static inline void DeleteEmailDecodeState(Email_DecodeState* ds)
{
    free(ds);
}

// FIXIT-L: An assignment by value is more intuitive than by reference
static inline void updateMaxDepth(int64_t file_depth, int* max_depth)
{
    if ((!file_depth) || (file_depth > MAX_BUF))
    {
        *max_depth = MAX_BUF;
    }
    else if (file_depth > (*max_depth))
    {
        *max_depth = (int)file_depth;
    }
}

// FIXIT-L: Should refactor as a method of struct Email_DecodeState
static inline void ClearPrevEncodeBuf(Email_DecodeState* ds)
{
    ds->prev_encoded_bytes = 0;
    ds->prev_encoded_buf = nullptr;
}

// FIXIT-L: Should refactor as a method of struct Email_DecodeState
static inline void ResetBytesRead(Email_DecodeState* ds)
{
    ds->uu_state.begin_found = ds->uu_state.end_found = 0;
    ClearPrevEncodeBuf(ds);
    ds->b64_state.encode_bytes_read = ds->b64_state.decode_bytes_read = 0;
    ds->qp_state.encode_bytes_read = ds->qp_state.decode_bytes_read = 0;
    ds->uu_state.encode_bytes_read = ds->uu_state.decode_bytes_read = 0;
    ds->bitenc_state.bytes_read = 0;
}

// FIXIT-L: Should refactor as a method of struct Email_DecodeState
static inline void ResetDecodedBytes(Email_DecodeState* ds)
{
    ds->decodePtr = nullptr;
    ds->decoded_bytes = 0;
    ds->decode_present = 0;
}

// FIXIT-L: Should refactor as a method of struct Email_DecodeState
static inline void ResetEmailDecodeState(Email_DecodeState* ds)
{
    if ( ds == nullptr )
        return;

    ds->uu_state.begin_found = ds->uu_state.end_found = 0;
    ResetDecodedBytes(ds);
    ClearPrevEncodeBuf(ds);
}

// FIXIT-L: Should refactor as a method of struct Email_DecodeState
static inline void ClearEmailDecodeState(Email_DecodeState* ds)
{
    if (ds == nullptr)
        return;

    ds->decode_type = DECODE_NONE;
    ResetEmailDecodeState(ds);
}

static inline int limitDetection(int depth, int decoded_bytes, int decode_bytes_total)
{
    if (!depth)
        return decoded_bytes;
    else if (depth < decode_bytes_total - decoded_bytes)
        return 0;
    else if (depth > decode_bytes_total)
        return decoded_bytes;
    else
        return (depth + decoded_bytes - decode_bytes_total);
}

static inline int getDetectionSize(int b64_depth, int qp_depth, int uu_depth, int bitenc_depth,
    Email_DecodeState* ds)
{
    int iRet = 0;

    switch (ds->decode_type)
    {
    case DECODE_B64:
        iRet = limitDetection(b64_depth, ds->decoded_bytes, ds->b64_state.decode_bytes_read);
        break;
    case DECODE_QP:
        iRet = limitDetection(qp_depth, ds->decoded_bytes, ds->qp_state.decode_bytes_read);
        break;
    case DECODE_UU:
        iRet = limitDetection(uu_depth, ds->decoded_bytes, ds->uu_state.decode_bytes_read);
        break;
    case DECODE_BITENC:
        iRet = limitDetection(bitenc_depth, ds->decoded_bytes, ds->bitenc_state.bytes_read);
        break;
    default:
        break;
    }

    return iRet;
}

#endif

