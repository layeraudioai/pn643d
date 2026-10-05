#ifndef SYSCTR_INPUT_CTRMEDIAFRAME_H
#define SYSCTR_INPUT_CTRMEDIAFRAME_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Bounded, byte-oriented media framing shared by UDS datagrams and TCP stream
// code. Header: "DMF1", version, media type, payload length (big endian), seq.
// The parser validates the declared length before exposing any payload.
namespace CTRMediaFrame
{
    static const size_t kHeaderSize = 10;
    static const size_t kMaxPayload = 1024;
    static const size_t kMaxWireSize = kHeaderSize + kMaxPayload;
    static const uint8_t kVersion = 1;
    enum Type { AUDIO = 1, VIDEO = 2 };

    inline size_t Encode(uint8_t *out, size_t capacity, uint8_t type,
                         uint16_t sequence, const void *payload, size_t length)
    {
        if (!out || (!payload && length) || length > kMaxPayload ||
            capacity < kHeaderSize + length || (type != AUDIO && type != VIDEO))
            return 0;
        memcpy(out, "DMF1", 4);
        out[4] = kVersion;
        out[5] = type;
        out[6] = (uint8_t)(length >> 8);
        out[7] = (uint8_t)length;
        out[8] = (uint8_t)(sequence >> 8);
        out[9] = (uint8_t)sequence;
        if (length)
            memcpy(out + kHeaderSize, payload, length);
        return kHeaderSize + length;
    }

    // Decode one complete datagram/frame. TCP users should first accumulate
    // exactly the header and declared payload into a kMaxWireSize buffer.
    inline bool Decode(const uint8_t *wire, size_t wireSize, uint8_t *type,
                       uint16_t *sequence, const uint8_t **payload, size_t *length)
    {
        if (!wire || wireSize < kHeaderSize || memcmp(wire, "DMF1", 4) != 0 ||
            wire[4] != kVersion || (wire[5] != AUDIO && wire[5] != VIDEO))
            return false;
        const size_t declared = ((size_t)wire[6] << 8) | wire[7];
        if (declared > kMaxPayload || wireSize != kHeaderSize + declared)
            return false;
        if (type) *type = wire[5];
        if (sequence) *sequence = (uint16_t)(((uint16_t)wire[8] << 8) | wire[9]);
        if (payload) *payload = wire + kHeaderSize;
        if (length) *length = declared;
        return true;
    }
}

#endif
