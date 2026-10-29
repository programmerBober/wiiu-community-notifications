#ifndef NEWS_STORE_H
#define NEWS_STORE_H

/*
 * SD-card backed SpotPass news store (zero MLC / NAND writes).
 *
 * Layout under <base>/news/:
 *   <serial>.bin   zlib-compressed <Message> XML payload (8 hex digit serial)
 *   <serial>.read  zero-byte marker -> item has been read
 *
 * The directory itself is the index: nothing to corrupt on a power cut.
 * Payloads are written to <serial>.tmp and renamed into place (atomic on FAT).
 */

#include <stdint.h>
#include <stdbool.h>

#define NEWS_STORE_MAX        64          // FIFO: oldest items are trimmed beyond this
#define NEWS_STORE_NAME_PREFIX "sdcn"     // nn::boss DataName prefix for SD-backed items

#ifdef __cplusplus
extern "C" {
#endif

void     NewsStore_Init(const char* base_dir);
bool     NewsStore_Save(const uint8_t* buf, uint32_t len, uint32_t* out_serial);
uint32_t NewsStore_List(uint32_t* serials, uint32_t max);   // ascending (oldest first)
int32_t  NewsStore_Size(uint32_t serial);                   // -1 if missing
int32_t  NewsStore_Read(uint32_t serial, uint32_t offset, void* buf, uint32_t len);
bool     NewsStore_GetRead(uint32_t serial);
void     NewsStore_SetRead(uint32_t serial, bool read);
bool     NewsStore_Delete(uint32_t serial);
uint32_t NewsStore_ClearAll(void);

// "sdcn%08x" <-> serial (DataName is char[32])
void     NewsStore_MakeName(uint32_t serial, char out[32]);
bool     NewsStore_ParseName(const char* name, uint32_t* out_serial);

#ifdef __cplusplus
}
#endif

#endif // NEWS_STORE_H
