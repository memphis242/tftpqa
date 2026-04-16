/**
 * @file tftp_pkt.h
 * @brief TFTP packet encoding, decoding, and validation (RFC 1350).
 * @date Apr 02, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTP_PKT_H
#define TFTP_PKT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// TFTP opcodes (RFC 1350 Section 5)
#define TFTP_OP_RRQ  ((uint16_t)1)
#define TFTP_OP_WRQ  ((uint16_t)2)
#define TFTP_OP_DATA ((uint16_t)3)
#define TFTP_OP_ACK  ((uint16_t)4)
#define TFTP_OP_ERR  ((uint16_t)5)

// TFTP error codes (RFC 1350 Section 5)
#define TFTP_ERRC_NOT_DEFINED       ((uint16_t)0)
#define TFTP_ERRC_FILE_NOT_FOUND    ((uint16_t)1)
#define TFTP_ERRC_ACCESS_VIOLATION  ((uint16_t)2)
#define TFTP_ERRC_DISK_FULL         ((uint16_t)3)
#define TFTP_ERRC_ILLEGAL_OP        ((uint16_t)4)
#define TFTP_ERRC_UNKNOWN_TID       ((uint16_t)5)
#define TFTP_ERRC_FILE_EXISTS       ((uint16_t)6)
#define TFTP_ERRC_NO_SUCH_USER      ((uint16_t)7)
#define TFTP_ERRC_COUNT             ((uint16_t)8)

/**
 * @brief Validate that a received buffer is a well-formed RRQ or WRQ packet.
 * @param[in] buf  Pointer to the raw packet bytes.
 * @param[in] sz   Number of bytes in the packet.
 * @return true if the packet is a valid RRQ or WRQ, false otherwise.
 */
bool tftp_pkt_request_is_valid(const uint8_t *buf, size_t sz);

/**
 * @brief Extract fields from a validated RRQ/WRQ packet (zero-copy).
 * @param[in]  buf       Pointer to the raw packet bytes.
 * @param[in]  sz        Number of bytes in the packet.
 * @param[out] opcode    Set to TFTP_OP_RRQ or TFTP_OP_WRQ.
 * @param[out] filename  Set to point into buf at the filename string.
 * @param[out] mode      Set to point into buf at the mode string.
 * @return 0 on success, -1 if the packet is malformed.
 */
int tftp_pkt_parse_request(const uint8_t *buf, size_t sz,
                           uint16_t *opcode,
                           const char **filename,
                           const char **mode);

/**
 * @brief Build a DATA packet.
 * @return Number of bytes written to out, or 0 on error.
 */
size_t tftp_pkt_build_data(uint8_t *out, size_t out_cap,
                           uint16_t block_num,
                           const uint8_t *data, size_t data_len);

/**
 * @brief Build an ACK packet.
 * @return Number of bytes written to out, or 0 on error.
 */
size_t tftp_pkt_build_ack(uint8_t *out, size_t out_cap, uint16_t block_num);

/**
 * @brief Build an ERROR packet.
 * @return Number of bytes written to out, or 0 on error.
 */
size_t tftp_pkt_build_error(uint8_t *out, size_t out_cap,
                            uint16_t error_code, const char *errmsg);

/**
 * @brief Parse an ACK packet.
 * @return 0 on success, -1 if malformed.
 */
int tftp_pkt_parse_ack(const uint8_t *buf, size_t sz, uint16_t *block_num);

/**
 * @brief Parse a DATA packet (zero-copy for the data payload).
 * @return 0 on success, -1 if malformed.
 */
int tftp_pkt_parse_data(const uint8_t *buf, size_t sz,
                        uint16_t *block_num,
                        const uint8_t **data, size_t *data_len);

/**
 * @brief Parse an ERROR packet (zero-copy for the error message).
 * @return 0 on success, -1 if malformed.
 */
int tftp_pkt_parse_error(const uint8_t *buf, size_t sz,
                         uint16_t *error_code, const char **errmsg);

#endif // TFTP_PKT_H
