/* Cài đặt parser/encoder frame nhị phân DC3-W2, không phụ thuộc ROS 2. */
#include "dc3w2_protocol.h"

#include <string.h>

static void Dc3w2_Reset(Dc3w2Decoder *decoder)
{
  if (decoder == NULL)
  {
    return;
  }

  decoder->length = 0U;
  decoder->expected_length = 0U;
  decoder->active = 0U;
}

static uint32_t Dc3w2_ReadU32(const uint8_t *data)
{
  return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t Dc3w2_ReadU16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint16_t Dc3w2_Crc16(const uint8_t *data, uint16_t length)
{
  uint16_t crc = 0xFFFFU;

  if ((data == NULL) && (length > 0U))
  {
    return 0U;
  }

  for (uint16_t index = 0U; index < length; index++)
  {
    crc ^= (uint16_t)data[index] << 8;
    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
      crc = (crc & 0x8000U) != 0U
              ? (uint16_t)((crc << 1) ^ 0x1021U)
              : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

void Dc3w2_DecoderInit(Dc3w2Decoder *decoder)
{
  if (decoder != NULL)
  {
    memset(decoder, 0, sizeof(*decoder));
  }
}

uint8_t Dc3w2_DecoderBusy(const Dc3w2Decoder *decoder)
{
  return (decoder != NULL) ? decoder->active : 0U;
}

uint8_t Dc3w2_DecoderFeed(Dc3w2Decoder *decoder, uint8_t byte, Dc3w2Frame *frame)
{
  uint16_t header_length;
  uint16_t received_crc;
  uint16_t expected_crc;

  if ((decoder == NULL) || (frame == NULL))
  {
    return 0U;
  }

  if (decoder->active == 0U)
  {
    if (byte != DC3W2_SYNC_A)
    {
      return 0U;
    }
    decoder->active = 1U;
    decoder->length = 1U;
    decoder->buffer[0] = byte;
    return 0U;
  }

  if (decoder->length >= sizeof(decoder->buffer))
  {
    Dc3w2_Reset(decoder);
    return 0U;
  }
  decoder->buffer[decoder->length++] = byte;

  if (decoder->length == 2U)
  {
    if (byte != DC3W2_SYNC_B)
    {
      /* Keep a second A5 as a possible start of the next frame. */
      if (byte == DC3W2_SYNC_A)
      {
        decoder->length = 1U;
        decoder->expected_length = 0U;
        decoder->buffer[0] = byte;
      }
      else
      {
        Dc3w2_Reset(decoder);
      }
    }
    return 0U;
  }

  header_length = 2U + DC3W2_HEADER_SIZE;
  if (decoder->length < header_length)
  {
    return 0U;
  }

  if (decoder->length == header_length)
  {
    uint16_t payload_length = Dc3w2_ReadU16(&decoder->buffer[2U + 7U]);
    if ((decoder->buffer[2U] != DC3W2_VERSION) ||
        (payload_length > DC3W2_MAX_PAYLOAD))
    {
      Dc3w2_Reset(decoder);
      return 0U;
    }
    decoder->expected_length = (uint16_t)(header_length + payload_length + 2U);
  }

  if ((decoder->expected_length == 0U) ||
      (decoder->length < decoder->expected_length))
  {
    return 0U;
  }

  if (decoder->length > decoder->expected_length)
  {
    Dc3w2_Reset(decoder);
    return 0U;
  }

  expected_crc = Dc3w2_Crc16(&decoder->buffer[2U],
                             (uint16_t)(DC3W2_HEADER_SIZE +
                                        Dc3w2_ReadU16(&decoder->buffer[9U])));
  received_crc = Dc3w2_ReadU16(&decoder->buffer[decoder->expected_length - 2U]);
  if (expected_crc != received_crc)
  {
    Dc3w2_Reset(decoder);
    return 0U;
  }

  frame->version = decoder->buffer[2U];
  frame->frame_type = decoder->buffer[3U];
  frame->flags = decoder->buffer[4U];
  frame->seq = Dc3w2_ReadU32(&decoder->buffer[5U]);
  frame->payload_length = Dc3w2_ReadU16(&decoder->buffer[9U]);
  memcpy(frame->payload, &decoder->buffer[11U], frame->payload_length);
  Dc3w2_Reset(decoder);
  return 1U;
}

uint16_t Dc3w2_Encode(uint8_t frame_type, uint8_t flags, uint32_t seq,
                      const uint8_t *payload, uint16_t payload_length,
                      uint8_t *output, uint16_t output_capacity)
{
  uint16_t total_length;
  uint16_t crc;

  if ((payload_length > DC3W2_MAX_PAYLOAD) ||
      (output == NULL) ||
      ((payload_length > 0U) && (payload == NULL)) ||
      (output_capacity < (uint16_t)(2U + DC3W2_HEADER_SIZE + payload_length + 2U)))
  {
    return 0U;
  }

  total_length = (uint16_t)(2U + DC3W2_HEADER_SIZE + payload_length + 2U);
  output[0] = DC3W2_SYNC_A;
  output[1] = DC3W2_SYNC_B;
  output[2] = DC3W2_VERSION;
  output[3] = frame_type;
  output[4] = flags;
  output[5] = (uint8_t)(seq & 0xFFU);
  output[6] = (uint8_t)((seq >> 8) & 0xFFU);
  output[7] = (uint8_t)((seq >> 16) & 0xFFU);
  output[8] = (uint8_t)((seq >> 24) & 0xFFU);
  output[9] = (uint8_t)(payload_length & 0xFFU);
  output[10] = (uint8_t)((payload_length >> 8) & 0xFFU);
  if ((payload_length > 0U) && (payload != NULL))
  {
    memcpy(&output[11U], payload, payload_length);
  }
  crc = Dc3w2_Crc16(&output[2U], (uint16_t)(DC3W2_HEADER_SIZE + payload_length));
  output[11U + payload_length] = (uint8_t)(crc & 0xFFU);
  output[12U + payload_length] = (uint8_t)((crc >> 8) & 0xFFU);
  return total_length;
}
