/*
 * I'll just set a limit of 64 for now. Most file systems have a limit of 255
 * bytes for filenames, but for my testing, I'll declare it a crime to have
 * names longer than 64 bytes.
 */
#define FILENAME_MAX_LEN 64
#define FILENAME_MIN_LEN 1

/*
 * "netascii", "octet", or "mail"
 */
#define TFTP_MODE_MAX_LEN (sizeof("netascii") - 1)
#define TFTP_MODE_MIN_LEN (sizeof("octet") - 1) // "mail" is not supported

/*
 *   2 bytes     string    1 byte     string   1 byte
 *  ------------------------------------------------
 * | Opcode |  Filename  |   0  |    Mode    |   0  |
 *  ------------------------------------------------
 */
#define TFTP_RQST_MAX_SZ (2 + FILENAME_MAX_LEN + 1 + TFTP_MODE_MAX_LEN + 1)
#define TFTP_RQST_MIN_SZ (2 + FILENAME_MIN_LEN + 1 + TFTP_MODE_MIN_LEN + 1)

/*
 * TFTP DATA packet: 2-byte opcode + 2-byte block# + up to 512 bytes of data
 */
#define TFTP_BLOCK_DATA_SZ 512
#define TFTP_DATA_HDR_SZ   4
#define TFTP_DATA_MAX_SZ   (TFTP_DATA_HDR_SZ + TFTP_BLOCK_DATA_SZ)

/*
 * TFTP ACK packet: 2-byte opcode + 2-byte block#
 */
#define TFTP_ACK_SZ 4

/*
 * TFTP ERROR packet: 2-byte opcode + 2-byte error code + string + 1-byte NUL
 */
#define TFTP_ERR_HDR_SZ 4

/*
 * Compile-time assert trick that is compatible /w C99 (which doesn't have static_assert :(...)
 */
#define CompileTimeAssert(cond, msg) typedef char msg[ (cond) ? 1 : -1 ]

/*
 * Compile-time array length
 */
#define ARRAY_SZ(arr) (sizeof(arr) / sizeof(arr[0]))
