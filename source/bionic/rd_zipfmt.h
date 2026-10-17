/* rd_zipfmt.h -- ZIP record layout (PKWARE APPNOTE 4.3): signatures, fixed sizes and the field
 * offsets the port reads or writes, shared by rd_zip.c (reading APK assets) and rd_zipview.c
 * (the mod overlay archive). Little-endian helpers included.
 * MIT (see LICENSE). */
#ifndef RD_ZIPFMT_H
#define RD_ZIPFMT_H

#include <stdint.h>

#define ZIP_EOCD_SIG         0x06054b50u   /* end of central directory */
#define ZIP_EOCD_SIZE        22
#define ZIP_EOCD_MAX_COMMENT 65535
#define ZIP_EOCD_DISK_ENTRIES 8
#define ZIP_EOCD_ENTRIES     10
#define ZIP_EOCD_CD_SIZE     12
#define ZIP_EOCD_CD_OFFSET   16
#define ZIP_EOCD_COMMENT_LEN 20

#define ZIP_CDH_SIG          0x02014b50u   /* central directory file header */
#define ZIP_CDH_SIZE         46
#define ZIP_CDH_VERSION_NEED 6
#define ZIP_CDH_FLAGS        8
#define ZIP_CDH_METHOD       10
#define ZIP_CDH_TIME         12            /* time + date, 4 bytes */
#define ZIP_CDH_CRC          16
#define ZIP_CDH_COMP_SIZE    20
#define ZIP_CDH_SIZE_UNCOMP  24
#define ZIP_CDH_NAME_LEN     28
#define ZIP_CDH_EXTRA_LEN    30
#define ZIP_CDH_COMMENT_LEN  32
#define ZIP_CDH_LOCAL_OFF    42

#define ZIP_LFH_SIG          0x04034b50u   /* local file header */
#define ZIP_LFH_SIZE         30
#define ZIP_LFH_VERSION_NEED 4
#define ZIP_LFH_TIME         10
#define ZIP_LFH_CRC          14
#define ZIP_LFH_COMP_SIZE    18
#define ZIP_LFH_SIZE_UNCOMP  22
#define ZIP_LFH_NAME_LEN     26
#define ZIP_LFH_EXTRA_LEN    28

#define ZIP_VERSION_STORED   20            /* "version needed" for a plain stored entry */
#define ZIP_STORED           0
#define ZIP_DEFLATED         8
#define ZIP_U16_MAX          0xFFFFu       /* a 16/32-bit field at its maximum means ZIP64 */
#define ZIP_U32_MAX          0xFFFFFFFFu

/* Little-endian 16-bit value at p (all ZIP fields are little-endian). */
static inline uint16_t zip_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
/* Little-endian 32-bit value at p. */
static inline uint32_t zip_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* Stores v little-endian at p. */
static inline void zip_put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
/* Stores v little-endian at p. */
static inline void zip_put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

#endif
