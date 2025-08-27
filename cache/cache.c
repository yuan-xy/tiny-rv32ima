#include <stdint.h>
#include <string.h>

#include "../psram/psram.h"
#include "cache.h"

#define ADDR_BITS 24

#include "vm_config.h"

#define OFFSET(addr) (addr & (CACHE_LINE_SIZE - 1))
#define INDEX(addr) ((addr >> OFFSET_BITS) & (CACHE_SET_SIZE - 1))
#define TAG(addr) (addr >> (OFFSET_BITS + INDEX_BITS))
#define BASE(addr) (addr & (~(uint32_t)(CACHE_LINE_SIZE - 1)))

#define LINE_TAG(line) (line->tag)

#define IS_VALID(line) (line->status & 0b01)
#define IS_DIRTY(line) (line->status & 0b10)
#define IS_LRU(line) (line->status & 0b100)

#define SET_VALID(line) line->status = 1
#define SET_DIRTY(line) line->status |= 0b10;
#define SET_LRU(line) line->status |= 0b100;
#define CLEAR_LRU(line) line->status &= ~0b100;

#define psram_write(ofs, p, sz) psram_access(ofs, sz, true, p)
#define psram_read(ofs, p, sz) psram_access(ofs, sz, false, p)

struct Cacheline
{
    uint16_t tag;
    uint8_t data[CACHE_LINE_SIZE];
    uint8_t status;
};
typedef struct Cacheline cacheline_t;

cacheline_t cache[CACHE_SET_SIZE][2];

void cache_reset(void)
{
    memset(cache, 0, sizeof(cache));
}

static inline void flush_line(cacheline_t *line, uint16_t index)
{
    if (IS_DIRTY(line)) // if line is valid and dirty, flush it to RAM
    {
        // flush line to RAM
        uint32_t flush_base =
            (index << OFFSET_BITS) | ((uint32_t)(LINE_TAG(line)) << (INDEX_BITS + OFFSET_BITS));
        psram_write(flush_base, line->data, CACHE_LINE_SIZE);
    }
}

void cache_flush(void)
{
    for (int index = 0; index < CACHE_SET_SIZE; index++)
    {
        flush_line(&cache[index][0], index);
        flush_line(&cache[index][1], index);
    }
}

void cache_read(uint32_t addr, void *ptr, uint8_t size)
{
    uint16_t index = INDEX(addr);
    uint16_t tag = TAG(addr);
    uint8_t offset = OFFSET(addr);

    cacheline_t *line;
    cacheline_t *way1 = &cache[index][0];
    cacheline_t *way2 = &cache[index][1];

    if (tag == LINE_TAG(way1) && IS_VALID(way1))
    {
        line = way1;
        CLEAR_LRU(way1);
        SET_LRU(way2);
    }
    else if (tag == LINE_TAG(way2) && IS_VALID(way2))
    {
        line = way2;
        CLEAR_LRU(way2);
        SET_LRU(way1);
    }

    else // miss
    {
        if (IS_LRU(way1))
        {
            line = way1;
            CLEAR_LRU(way1);
            SET_LRU(way2);
        }

        else
        {
            line = way2;
            CLEAR_LRU(way2);
            SET_LRU(way1);
        }

        if (IS_DIRTY(line)) // if line is valid and dirty, flush it to RAM
        {
            // flush line to RAM
            uint32_t flush_base =
                (index << OFFSET_BITS) | ((uint32_t)(LINE_TAG(line)) << (INDEX_BITS + OFFSET_BITS));
            psram_write(flush_base, line->data, CACHE_LINE_SIZE);
        }

        // get line from RAM
        uint32_t base = BASE(addr);
        psram_read(base, line->data, CACHE_LINE_SIZE);

        line->tag = tag; // set the tag of the line
        SET_VALID(line); // mark the line as valid
    }

    /*
        if (offset + size > CACHE_LINE_SIZE)
        {
            // printf("cross boundary read!\n");
            size = CACHE_LINE_SIZE - offset;
        }
    */

    while (size--)
        *(uint8_t *)(ptr++) = line->data[offset++];
    // for ( int i = 0; i < size; i++ ) ( (uint8_t *)( ptr ) )[i] = line->data[offset + i];
}

void cache_write(uint32_t addr, void *ptr, uint8_t size)
{
    uint16_t index = INDEX(addr);
    uint16_t tag = TAG(addr);
    uint8_t offset = OFFSET(addr);

    cacheline_t *line;
    cacheline_t *way1 = &cache[index][0];
    cacheline_t *way2 = &cache[index][1];

    if (tag == LINE_TAG(way1) && IS_VALID(way1))
    {
        line = way1;
        CLEAR_LRU(way1);
        SET_LRU(way2);
    }
    else if (tag == LINE_TAG(way2) && IS_VALID(way2))
    {
        line = way2;
        CLEAR_LRU(way2);
        SET_LRU(way1);
    }

    else // miss
    {
        if (IS_LRU(way1))
        {
            line = way1;
            CLEAR_LRU(way1);
            SET_LRU(way2);
        }

        else
        {
            line = way2;
            CLEAR_LRU(way2);
            SET_LRU(way1);
        }

        if (IS_DIRTY(line)) // if line is valid and dirty, flush it to RAM
        {
            // flush line to RAM
            uint32_t flush_base =
                (index << OFFSET_BITS) | ((uint32_t)(LINE_TAG(line)) << (INDEX_BITS + OFFSET_BITS));
            psram_write(flush_base, line->data, CACHE_LINE_SIZE);
        }

        // get line from RAM
        uint32_t base = BASE(addr);
        psram_read(base, line->data, CACHE_LINE_SIZE);

        line->tag = tag; // set the tag of the line
        SET_VALID(line); // mark the line as valid
    }
    /*
        if (offset + size > CACHE_LINE_SIZE)
        {
            // printf("cross boundary write!\n");
            size = CACHE_LINE_SIZE - offset;
        }
    */

    while (size--)
        line->data[offset++] = *(uint8_t *)(ptr++);

    // for ( int i = 0; i < size; i++ ) line->data[offset + i] = ( (uint8_t *)( ptr ) )[i];
    SET_DIRTY(line); // mark the line as dirty
}
