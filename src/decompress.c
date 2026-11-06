/* decompress94.asm: graphics decompression. DecompressGraphics sends packed tiles to
 * VRAM through DecompressBytecode and its 256 byte ring buffer. */
#include "game.h"
#include "hal.h"
#include "rom.h"
#include "symbols_gen.h"

/* put one byte in the ring buffer; a full buffer goes to VRAM */
static void ring_put(struct Unpack *u, uint8_t b)
{
    u->ring[u->idx] = b;
    if (++u->idx == 0) FlushOutputBuffer(u);
}
static uint8_t op_byte(const struct Unpack *u) { return m8(u->src - 1); }        /* -1(a0) */
static uint8_t next_byte(struct Unpack *u) { return m8(u->src++); }              /* (a0)+ */

/* opcodes 9 and F: count = low 4 bits and the top bit of the next byte (asl.b / roxl.b),
 * distance = the next byte's low 7 bits + 1 */
static uint16_t long_count(const struct Unpack *u)
{
    uint8_t next = m8(u->src);
    return (uint16_t)((((op_byte(u) << 1) | (next >> 7)) & 0x1F) + 2);
}

/* decompress94.asm:DecompressGraphicsWithRemap. DecompressGraphics with the 8-byte colour table
 * that follows the call (cb, its 68k address) as the remap table. */
uint16_t DecompressGraphicsWithRemap(uint32_t tiles, uint16_t start, uint32_t cb)
{
    g.remapTablePtr = cb;
    return DecompressGraphics(tiles, start);
}

/* decompress94.asm:DecompressGraphicsPlain. DecompressGraphics without a remap table. */
uint16_t DecompressGraphicsPlain(uint32_t tiles, uint16_t start)
{
    g.remapTablePtr = 0;
    return DecompressGraphics(tiles, start);
}

/* decompress94.asm:DecompressGraphics. a2 = graphics (a word count of tiles, bit 15 = packed, then
 * the data), d4 = first char: load them at char d4 (DMA, remap through remapTablePtr, or
 * DecompressBytecode) -> d4 = next char. */
uint16_t DecompressGraphics(uint32_t tiles, uint16_t start)
{
    uint16_t vram = (uint16_t)(start << 5);
    uint16_t n = m16(tiles);
    uint32_t data = tiles + 2;
    if (n == 0) return start;
    if (n & 0x8000) {
        n &= 0x7FFF;
        start += n;
        DecompressBytecode(data, vram);
        return start;
    }
    start += n;
    uint16_t words = (uint16_t)(n << 4);
    if (g.remapTablePtr == 0) DoDMApro(data, words, vram);
    else remap(data, g.remapTablePtr, words, vram);
    return start;
}

/* decompress94.asm:DecompressBytecode. a0 = packed tiles, d1 = VRAM address: run the byte code
 * (one handler per high nibble, jump_table) into the ring buffer, which goes to VRAM each time
 * it fills and at the end code. */
void DecompressBytecode(uint32_t src, uint16_t vram)
{
    static int (*const jump_table[16])(struct Unpack *) = {
        Opcode_CopyLiteral, Opcode_CopyLiteral, Opcode_ClearBytes, Opcode_FillBytes,
        Opcode_CopyBackwardShort, Opcode_CopyBackwardShort, Opcode_CopyBackwardShort, Opcode_CopyBackwardShort,
        Opcode_CopyBackwardMedium, Opcode_CopyBackwardLong, Opcode_CopyBackwardExtended1, Opcode_CopyBackwardExtended2,
        Opcode_CopyBackwardReverseShort, Opcode_CopyBackwardReverseShort, Opcode_CopyBackwardReverseMedium, Opcode_CopyBackwardReverseLong,
    };
    struct Unpack u = { src, 0, vram, unpack_ring() };
    for (;;) {
        uint8_t op = m8(u.src++);
        if (jump_table[op >> 4](&u)) return;
    }
}

/* decompress94.asm:Opcode_CopyLiteral. Opcodes 0-1: copy (low 5 bits)+1 bytes from the data. */
int Opcode_CopyLiteral(struct Unpack *u)
{
    for (int n = (op_byte(u) & 0x1F) + 1; n; n--) ring_put(u, next_byte(u));
    return 0;
}

/* decompress94.asm:Opcode_ClearBytes. Opcode 2: (low 4 bits)+1 zero bytes. */
int Opcode_ClearBytes(struct Unpack *u)
{
    for (int n = (op_byte(u) & 0xF) + 1; n; n--) ring_put(u, 0);
    return 0;
}

/* decompress94.asm:Opcode_FillBytes. Opcode 3: the next byte (low 4 bits)+3 times. */
int Opcode_FillBytes(struct Unpack *u)
{
    int n = (op_byte(u) & 0xF) + 3;
    uint8_t b = next_byte(u);
    for (; n; n--) ring_put(u, b);
    return 0;
}

/* decompress94.asm:Opcode_CopyBackwardShort. Opcodes 4-7: count bits 0-2, distance bits 3-5. */
int Opcode_CopyBackwardShort(struct Unpack *u)
{
    uint8_t op = op_byte(u);
    return CopyBackwardRun(u, (uint16_t)((op & 7) + 1), (uint8_t)(((op >> 3) & 7) + 1));
}

/* decompress94.asm:CopyBackwardRun. Copy count+1 bytes from dist back in the ring buffer, forward. */
int CopyBackwardRun(struct Unpack *u, uint16_t count, uint8_t dist)
{
    uint8_t from = (uint8_t)(u->idx - dist);
    do ring_put(u, u->ring[from++]); while (count-- != 0);
    return 0;
}

/* decompress94.asm:Opcode_CopyBackwardMedium. Opcode 8: count low 4 bits, distance the next byte. */
int Opcode_CopyBackwardMedium(struct Unpack *u)
{
    uint16_t count = (uint16_t)((op_byte(u) & 0xF) + 2);
    return CopyBackwardRun(u, count, next_byte(u));
}

/* decompress94.asm:Opcode_CopyBackwardLong. Opcode 9. */
int Opcode_CopyBackwardLong(struct Unpack *u)
{
    uint16_t count = long_count(u);
    return CopyBackwardRun(u, count, (uint8_t)((next_byte(u) & 0x7F) + 1));
}

/* decompress94.asm:Opcode_CopyBackwardExtended1. Opcode A: 6-bit count, 6-bit distance. */
int Opcode_CopyBackwardExtended1(struct Unpack *u)
{
    uint16_t w = (uint16_t)(op_byte(u) << 8 | m8(u->src));
    uint16_t count = (uint16_t)(((w >> 6) & 0x3F) + 2);
    return CopyBackwardRun(u, count, (uint8_t)((next_byte(u) & 0x3F) + 1));
}

/* decompress94.asm:Opcode_CopyBackwardExtended2. Opcode B: 7-bit count, 5-bit distance. */
int Opcode_CopyBackwardExtended2(struct Unpack *u)
{
    uint16_t w = (uint16_t)(op_byte(u) << 8 | m8(u->src));
    uint16_t count = (uint16_t)(((w >> 5) & 0x7F) + 2);
    return CopyBackwardRun(u, count, (uint8_t)((next_byte(u) & 0x1F) + 1));
}

/* decompress94.asm:Opcode_CopyBackwardReverseShort. Opcodes C-D: count bits 0-1, distance bits 2-4. */
int Opcode_CopyBackwardReverseShort(struct Unpack *u)
{
    uint8_t op = op_byte(u);
    return CopyBackwardReverseRun(u, (uint16_t)((op & 3) + 1), (uint8_t)(((op >> 2) & 7) + 1));
}

/* decompress94.asm:CopyBackwardReverseRun. The same, walking the source backwards. */
int CopyBackwardReverseRun(struct Unpack *u, uint16_t count, uint8_t dist)
{
    uint8_t from = (uint8_t)(u->idx - dist);
    do ring_put(u, u->ring[from--]); while (count-- != 0);
    return 0;
}

/* decompress94.asm:Opcode_CopyBackwardReverseMedium. Opcode E: count low 4 bits, distance the next
 * byte. Distance 0 is the end code: send what is left in the buffer and stop (1). */
int Opcode_CopyBackwardReverseMedium(struct Unpack *u)
{
    uint16_t count = (uint16_t)((op_byte(u) & 0xF) + 2);
    uint8_t dist = next_byte(u);
    if (dist) return CopyBackwardReverseRun(u, count, dist);
    if (u->idx) FlushOutputBuffer(u);
    return 1;
}

/* decompress94.asm:Opcode_CopyBackwardReverseLong. Opcode F. */
int Opcode_CopyBackwardReverseLong(struct Unpack *u)
{
    uint16_t count = long_count(u);
    return CopyBackwardReverseRun(u, count, (uint8_t)((next_byte(u) & 0x7F) + 1));
}

/* decompress94.asm:FlushOutputBuffer. Send the ring buffer (all 256 bytes when it wrapped, else the
 * idx bytes in it) to the VRAM address d3, through remap when remapTablePtr is set, and advance d3. */
void FlushOutputBuffer(struct Unpack *u)
{
    uint16_t words = (uint16_t)((u->idx ? u->idx : 0x100) >> 1);
    uint16_t vram = u->vram;
    u->vram = (uint16_t)(u->vram + 2 * words);
    if (g.remapTablePtr) remap(ram_addr(u->ring), g.remapTablePtr, words, vram);
    else DoDMApro(ram_addr(u->ring), words, vram);
}
