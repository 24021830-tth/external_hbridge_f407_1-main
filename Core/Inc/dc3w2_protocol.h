/* Giao thức frame nhị phân dùng cho DC3-W2/W4. */
#ifndef DC3W2_PROTOCOL_H
#define DC3W2_PROTOCOL_H

#include <stdint.h>

#define DC3W2_SYNC_A 0xA5U
#define DC3W2_SYNC_B 0x5AU
#define DC3W2_VERSION 1U
#define DC3W2_MAX_PAYLOAD 64U
#define DC3W2_HEADER_SIZE 9U
#define DC3W2_MAX_FRAME_SIZE (2U + DC3W2_HEADER_SIZE + DC3W2_MAX_PAYLOAD + 2U)

enum
{
  DC3W2_TYPE_CONTROL = 0x01U,
  DC3W2_TYPE_TELEMETRY = 0x02U,
  DC3W2_TYPE_PING = 0x03U,
  DC3W2_TYPE_PROBE = 0x10U,
  DC3W2_TYPE_DIAG = 0x7DU,
  DC3W2_TYPE_ACK = 0x7EU,
  DC3W2_TYPE_ERROR = 0x7FU
};

typedef struct
{
  uint8_t version;
  uint8_t frame_type;
  uint8_t flags;
  uint32_t seq;
  uint16_t payload_length;
  uint8_t payload[DC3W2_MAX_PAYLOAD];
} Dc3w2Frame;

typedef struct
{
  uint8_t buffer[DC3W2_MAX_FRAME_SIZE];
  uint16_t length;
  uint16_t expected_length;
  uint8_t active;
} Dc3w2Decoder;

void Dc3w2_DecoderInit(Dc3w2Decoder *decoder);
uint8_t Dc3w2_DecoderBusy(const Dc3w2Decoder *decoder);
uint8_t Dc3w2_DecoderFeed(Dc3w2Decoder *decoder, uint8_t byte, Dc3w2Frame *frame);
uint16_t Dc3w2_Encode(uint8_t frame_type, uint8_t flags, uint32_t seq,
                      const uint8_t *payload, uint16_t payload_length,
                      uint8_t *output, uint16_t output_capacity);
uint16_t Dc3w2_Crc16(const uint8_t *data, uint16_t length);

#endif
