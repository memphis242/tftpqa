/*
 * I'll just set a limit of 64 for now. Most file systems have a limit of 255
 * bytes for filenames, but for my testing, I'll declare it a crime to have
 * names longer than 64 bytes.
 */
#define FILENAME_MAX_LEN 64

/*
 * "netascii", "octet", or "mail"
 */
#define TFTP_MODE_MAX_LEN (sizeof("netascii") - 1)

/*
 *   2 bytes     string    1 byte     string   1 byte
 *  ------------------------------------------------
 * | Opcode |  Filename  |   0  |    Mode    |   0  |
 *  ------------------------------------------------
 */
#define TFTP_RQST_MAX_SZ (2 + FILENAME_MAX_LEN + 1 + TFTP_MODE_MAX_LEN + 1)
