/**
 ******************************************************************************
 * @file    STM32F4xx_IAP/src/ymodem.c
 * @author  MCD Application Team
 * @version V1.0.0
 * @date    10-October-2011
 * @brief   This file provides all the software functions related to the ymodem
 *          protocol.
 ******************************************************************************
 * @attention
 *
 * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
 * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
 * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
 * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
 * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * <h2><center>&copy; COPYRIGHT 2011 STMicroelectronics</center></h2>
 ******************************************************************************
 */

/** @addtogroup STM32F4xx_IAP
 * @{
 */

/* Includes ------------------------------------------------------------------*/
#include "sysflash.h"
#include "bootutil/boot_public.h"
#include "flash_map_backend.h"
#include "usart.h"
#include "ymodem.h"
#include "common.h"
#include "string.h"
#include "iwdg.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
extern uint8_t FileName[];

/* Private function prototypes -----------------------------------------------*/
static uint16_t Cal_CRC16(const uint8_t *data, uint32_t size);
/* Private functions ---------------------------------------------------------*/

static uint8_t Send_Byte(uint8_t c)
{
  HAL_UART_Transmit(&huart1, &c, 1, HAL_MAX_DELAY);
  return 0;
}

static int8_t Receive_Byte(uint8_t *c, uint32_t timeout)
{
  while (timeout--)
  {
    if ((timeout & 0x3FFFU) == 0U)
    {
      IWDG_Feed();
    }
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET)
    {
      *c = (uint8_t)(huart1.Instance->RDR & 0xFFU);
      return 0;
    }
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) != RESET)
    {
      __HAL_UART_CLEAR_OREFLAG(&huart1);
    }
  }
  return -1;
}

/**
 * @brief  Receive a packet from sender
 * @param  data
 * @param  length
 * @param  timeout
 *     0: end of transmission
 *    -1: abort by sender
 *    >0: packet length
 * @retval 0: normally return
 *        -1: timeout or packet error
 *         1: abort by user
 */
static int32_t Receive_Packet(uint8_t *data, int32_t *length, uint32_t timeout)
{
  uint16_t i, packet_size;
  uint8_t c;
  *length = 0;

  if (Receive_Byte(&c, timeout) != 0)
  {
    return -1;
  }

  switch (c)
  {
  case SOH:
    packet_size = PACKET_SIZE;
    break;
  case STX:
    packet_size = PACKET_1K_SIZE;
    break;
  case EOT:
    return 0;
  case CA:
    if ((Receive_Byte(&c, timeout) == 0) && (c == CA))
    {
      *length = -1;
      return 0;
    }
    else
    {
      return -1;
    }
  case ABORT1:
  case ABORT2:
    return 1;
  default:
    return -1;
  }
  *data = c;
  for (i = 1; i < (packet_size + PACKET_OVERHEAD); i++)
  {
    if (Receive_Byte(data + i, timeout) != 0)
    {
      return -1;
    }
  }
  if (data[PACKET_SEQNO_INDEX] != ((data[PACKET_SEQNO_COMP_INDEX] ^ 0xff) & 0xff))
  {
    return -1;
  }

  {
    uint16_t packet_crc = (uint16_t)((uint16_t)data[PACKET_HEADER + packet_size] << 8);
    packet_crc |= data[PACKET_HEADER + packet_size + 1];
    uint16_t calc_crc = Cal_CRC16(data + PACKET_HEADER, packet_size);
    if (calc_crc != packet_crc)
    {
      return -1;
    }
  }

  *length = packet_size;
  return 0;
}

/**
 * @brief  Receive a file using the ymodem protocol.
 * @param  buf: Temporary buffer (size >= PACKET_1K_SIZE + PACKET_OVERHEAD).
 * @param  target_slot: BOOT_SLOT_PRIMARY or BOOT_SLOT_SECONDARY.
 * @retval The size of the file.
 */
int32_t Ymodem_Receive(uint8_t *buf, uint8_t target_slot)
{
  uint8_t file_size[FILE_SIZE_LENGTH], *file_ptr;
  uint8_t *packet_data = buf;
  uint8_t verify_buf[32];
  int32_t i, packet_length, session_done, file_done, packets_received, errors, session_begin, size = 0;
  uint32_t flash_off = 0;
  uint32_t flash_size = 0;
  int32_t result = 0;
  int fa_id;
  const struct flash_area *fa = NULL;

  if (buf == NULL)
  {
    return -4;
  }

  fa_id = flash_area_id_from_multi_image_slot(0, target_slot);
  if (fa_id < 0 || flash_area_open((uint8_t)fa_id, &fa) != 0)
  {
    return -4;
  }

  flash_size = flash_area_get_size(fa);
  /* Send initial CRC request ('C') for YMODEM handshake */
  Send_Byte(CRC16);

  for (session_done = 0, errors = 0, session_begin = 0;;)
  {
    for (packets_received = 0, file_done = 0;;)
    {
      switch (Receive_Packet(packet_data, &packet_length, NAK_TIMEOUT))
      {
      case 0:
        errors = 0;
        switch (packet_length)
        {
        /* Abort by sender */
        case -1:
          Send_Byte(ACK);
          result = 0;
          goto cleanup;
        /* End of transmission */
        case 0:
          Send_Byte(ACK);
          file_done = 1;
          break;
        /* Normal packet */
        default:
          if ((packet_data[PACKET_SEQNO_INDEX] & 0xff) != (packets_received & 0xff))
          {
            Send_Byte(NAK);
          }
          else
          {
            if (packets_received == 0)
            {
              /* Filename packet */
              if (packet_data[PACKET_HEADER] != 0)
              {
                /* Filename packet has valid data */
                for (i = 0, file_ptr = packet_data + PACKET_HEADER; (*file_ptr != 0) && (i < (FILE_NAME_LENGTH - 1));)
                {
                  FileName[i++] = *file_ptr++;
                }
                FileName[i++] = '\0';
                for (i = 0, file_ptr++; (*file_ptr != ' ') && (i < (FILE_SIZE_LENGTH - 1));)
                {
                  file_size[i++] = *file_ptr++;
                }
                file_size[i++] = '\0';
                Str2Int(file_size, &size);

                /* Test the size of the image to be sent */
                /* Image size is greater than Flash size */
                if (size > (int32_t)flash_size)
                {
                  /* End session */
                  Send_Byte(CA);
                  Send_Byte(CA);
                  result = -1;
                  goto cleanup;
                }
                /* erase target slot */
                if (flash_area_erase(fa, 0, flash_size) != 0)
                {
                  Send_Byte(CA);
                  Send_Byte(CA);
                  result = -2;
                  goto cleanup;
                }
                flash_off = 0;
                Send_Byte(ACK);
                Send_Byte(CRC16);
              }
              /* Filename packet is empty, end session */
              else
              {
                Send_Byte(ACK);
                file_done = 1;
                session_done = 1;
                break;
              }
            }
            /* Data packet */
            else
            {
              /* Write received data in Flash */
              if (flash_area_write(fa, flash_off,
                                   packet_data + PACKET_HEADER,
                                   (uint32_t)packet_length) == 0)
              {
                uint32_t verify_off = 0;
                while (verify_off < (uint32_t)packet_length)
                {
                  uint32_t chunk = (uint32_t)packet_length - verify_off;
                  if (chunk > sizeof(verify_buf))
                  {
                    chunk = sizeof(verify_buf);
                  }
                  if (flash_area_read(fa, flash_off + verify_off, verify_buf, chunk) != 0 ||
                      memcmp(verify_buf,
                             packet_data + PACKET_HEADER + verify_off,
                             chunk) != 0)
                  {
                    Send_Byte(CA);
                    Send_Byte(CA);
                    result = -2;
                    goto cleanup;
                  }
                  verify_off += chunk;
                }
                Send_Byte(ACK);
                IWDG_Feed();
                flash_off += (uint32_t)packet_length;
              }
              else /* An error occurred while writing to Flash memory */
              {
                /* End session */
                Send_Byte(CA);
                Send_Byte(CA);
                result = -2;
                goto cleanup;
              }
            }
            packets_received++;
            session_begin = 1;
          }
        }
        break;
      case 1:
        Send_Byte(CA);
        Send_Byte(CA);
        result = -3;
        goto cleanup;
      default:
        if (session_begin > 0)
        {
          errors++;
        }
        if (errors > MAX_ERRORS)
        {
          Send_Byte(CA);
          Send_Byte(CA);
          result = 0;
          goto cleanup;
        }
        Send_Byte(CRC16);
        break;
      }
      if (file_done != 0)
      {
        break;
      }
    }
    if (session_done != 0)
    {
      break;
    }
  }
  result = (int32_t)size;

cleanup:
  if (fa != NULL)
  {
    flash_area_close(fa);
  }
  return result;
}

/**
 * @brief  check response using the ymodem protocol
 * @param  buf: Address of the first byte
 * @retval The size of the file
 */
int32_t Ymodem_CheckResponse(uint8_t c)
{
  return 0;
}

/**
 * @brief  Update CRC16 for input byte
 * @param  CRC input value
 * @param  input byte
 * @retval None
 */
uint16_t UpdateCRC16(uint16_t crcIn, uint8_t byte)
{
  uint32_t crc = crcIn;
  uint32_t in = byte | 0x100;

  do
  {
    crc <<= 1;
    in <<= 1;
    if (in & 0x100)
      ++crc;
    if (crc & 0x10000)
      crc ^= 0x1021;
  }

  while (!(in & 0x10000));

  return crc & 0xffffu;
}

/**
 * @brief  Cal CRC16 for YModem Packet
 * @param  data
 * @param  length
 * @retval None
 */
static uint16_t Cal_CRC16(const uint8_t *data, uint32_t size)
{
  uint32_t crc = 0;
  const uint8_t *dataEnd = data + size;

  while (data < dataEnd)
    crc = UpdateCRC16(crc, *data++);

  crc = UpdateCRC16(crc, 0);
  crc = UpdateCRC16(crc, 0);

  return crc & 0xffffu;
}

/**
 * @brief  Cal Check sum for YModem Packet
 * @param  data
 * @param  length
 * @retval None
 */
uint8_t CalChecksum(const uint8_t *data, uint32_t size)
{
  uint32_t sum = 0;
  const uint8_t *dataEnd = data + size;

  while (data < dataEnd)
    sum += *data++;

  return (sum & 0xffu);
}

/**
 * @}
 */

/*******************(C)COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
