// malformed_test_generator.c
// Generates the small malformed archives used by the test cases under test/testfiles/malformed_*.
//
// Every archive is built from scratch, byte by byte, from the layouts documented in docs/internals
// (sit.md, sit13.md, cpt.md). Nothing is derived from an existing archive. Each archive holds at most
// one file called "file" with type and creator 'TEST', and breaks exactly one rule of the format.
// The comment above each generator says which field is malformed and what a decoder should do.
//
// Build and run (from this directory):
//   cc -std=c99 -o malformed_test_generator malformed_test_generator.c
//   ./malformed_test_generator ../testfiles
// This rewrites ../testfiles/malformed_<case>/testfile.<ext> for every case.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_ARCHIVE 4096

// Growable-enough byte buffer with a bit cursor, used to assemble one archive
struct buffer {
    uint8_t bytes[MAX_ARCHIVE];
    size_t len; // number of whole bytes in use
    int bit; // number of bits already used in bytes[len - 1] (0 = byte aligned)
};

static void put_u8(struct buffer *b, unsigned v) {
    if (b->len >= MAX_ARCHIVE) {
        fprintf(stderr, "archive too large\n");
        exit(1);
    }
    b->bytes[b->len++] = (uint8_t)v;
    b->bit = 0;
}

static void put_be16(struct buffer *b, unsigned v) {
    put_u8(b, v >> 8);
    put_u8(b, v & 0xff);
}

static void put_be32(struct buffer *b, uint32_t v) {
    put_be16(b, v >> 16);
    put_be16(b, v & 0xffff);
}

static void put_bytes(struct buffer *b, const void *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        put_u8(b, ((const uint8_t *)p)[i]);
}

static void put_zeros(struct buffer *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        put_u8(b, 0);
}

static void set_be16(struct buffer *b, size_t at, unsigned v) {
    b->bytes[at] = (uint8_t)(v >> 8);
    b->bytes[at + 1] = (uint8_t)v;
}

static void set_be32(struct buffer *b, size_t at, uint32_t v) {
    set_be16(b, at, v >> 16);
    set_be16(b, at + 2, v & 0xffff);
}

// Append one bit. lsb_first selects the StuffIt method 13 order (bit 0 of each byte first);
// otherwise the Compact Pro order (bit 7 of each byte first).
static void put_bit(struct buffer *b, int value, int lsb_first) {
    if (b->bit == 0) {
        put_u8(b, 0);
        b->bit = 0;
    }
    if (value)
        b->bytes[b->len - 1] |= (uint8_t)(lsb_first ? 1 << b->bit : 0x80 >> b->bit);
    b->bit = (b->bit + 1) & 7;
}

// CRC-16 used by StuffIt (reflected polynomial 0xA001, initial value 0), see sit.md "CRC Calculation"
static unsigned sit_crc16(const uint8_t *p, size_t n) {
    unsigned crc = 0;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

// Write the buffer to <outdir>/malformed_<name>/testfile.<ext>
static void save(const char *outdir, const char *name, const char *ext, const struct buffer *b) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/malformed_%s", outdir, name);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/malformed_%s/testfile.%s", outdir, name, ext);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(b->bytes, 1, b->len, f) != b->len) {
        perror(path);
        exit(1);
    }
    fclose(f);
    printf("Generated %s (%zu bytes)\n", path, b->len);
}

// --- Classic StuffIt container (sit.md "Classic Archive Layout") ---

#define SIT_ARCHIVE_HEADER 22
#define SIT_ENTRY_HEADER   112

// Start a classic archive holding one file with a data fork only. The fork data is appended by the
// caller, who then calls sit_classic_finish().
static void sit_classic_begin(struct buffer *b, int method) {
    memset(b, 0, sizeof(*b));
    put_bytes(b, "SIT!", 4);
    put_be16(b, 1); // number of entries
    put_be32(b, 0); // archive length, patched in sit_classic_finish()
    put_bytes(b, "rLau", 4);
    put_u8(b, 1); // version
    put_zeros(b, 7);

    put_u8(b, 0); // resource fork method
    put_u8(b, (unsigned)method); // data fork method
    put_u8(b, 4); // name length
    put_bytes(b, "file", 4);
    put_zeros(b, 59); // rest of the 63-byte name field
    put_bytes(b, "TEST", 4); // type
    put_bytes(b, "TEST", 4); // creator
    put_zeros(b, SIT_ENTRY_HEADER - 74); // flags, dates, lengths and CRCs; patched below
}

// Fill in the data fork lengths and CRC. comp_len is passed separately from the number of bytes that
// were appended so that a case can declare less (or more) compressed data than it stores.
static void sit_classic_finish(struct buffer *b, uint32_t uncomp_len, uint32_t comp_len, const uint8_t *expected,
                               size_t expected_len) {
    size_t entry = SIT_ARCHIVE_HEADER;
    set_be32(b, entry + 88, uncomp_len);
    set_be32(b, entry + 96, comp_len);
    set_be16(b, entry + 102, sit_crc16(expected, expected_len));
    set_be16(b, entry + 110, sit_crc16(b->bytes + entry, 110));
    set_be32(b, 6, (uint32_t)b->len);
}

// --- StuffIt method 13 bit stream (sit13.md) ---

// Meta-code symbols used to transmit code lengths (sit13.md "Meta-Code Output")
#define META_SET_LENGTH_1 0 // symbols 0..30 set the current length to symbol + 1
#define META_SET_LENGTH_8 7
#define META_ZERO         31 // current length = 0
#define META_INCREMENT    32 // current length + 1
#define META_DECREMENT    33 // current length - 1
#define META_REPEAT_FLAG  34 // 1 extra bit: one more store if set
#define META_REPEAT_SHORT 35 // 3 extra bits: (value + 2) more stores
#define META_REPEAT_LONG  36 // 6 extra bits: (value + 10) more stores

// Emit a meta-code symbol. The code words are those of the fixed table in sit13.md; they are sent
// most significant bit first, one stream bit at a time.
static void m13_meta(struct buffer *b, int symbol) {
    unsigned word;
    int length;
    switch (symbol) {
    case META_SET_LENGTH_1: word = 0x00dd, length = 11; break;
    case META_SET_LENGTH_8: word = 0x0005, length = 5; break;
    case META_ZERO: word = 0x0004, length = 5; break;
    case META_INCREMENT: word = 0x0002, length = 2; break;
    case META_DECREMENT: word = 0x0001, length = 2; break;
    case META_REPEAT_FLAG: word = 0x0007, length = 3; break;
    case META_REPEAT_SHORT: word = 0x000c, length = 4; break;
    default: word = 0x0002, length = 5; break; // META_REPEAT_LONG
    }
    while (length--)
        put_bit(b, (word >> length) & 1, 1);
}

// Emit an n-bit field, least significant bit first
static void m13_bits(struct buffer *b, unsigned value, int n) {
    for (int i = 0; i < n; i++)
        put_bit(b, (value >> i) & 1, 1);
}

// Emit a long repeat command that stores the current length `stores` times in total (11..74):
// (stores - 1) repeats plus the one store that follows every command.
static void m13_repeat_long(struct buffer *b, int stores) {
    m13_meta(b, META_REPEAT_LONG);
    m13_bits(b, (unsigned)(stores - 1 - 10), 6);
}

// Method 13 header byte: dynamic code set, second literal tree shares the first, 10 offset codes
static void m13_header(struct buffer *b) { put_u8(b, 0x08); }

// Emit an offset tree of 10 unused codes (all lengths zero); no test case emits a match
static void m13_empty_offset_tree(struct buffer *b) {
    m13_meta(b, META_ZERO); // store 1
    m13_meta(b, META_REPEAT_SHORT); // 8 repeats + 1 store = 10 entries
    m13_bits(b, 8 - 2, 3);
}

// Emit a complete literal/length tree of 321 codes: symbols 0..190 get 8-bit codes and symbols
// 191..320 get 9-bit codes, so byte value v (below 191) is coded as the 8-bit number v.
// If overrun is set the last repeat command asks for 73 more entries than the table has room for.
static void m13_complete_tree(struct buffer *b, int overrun) {
    m13_meta(b, META_SET_LENGTH_8); // entry 0
    m13_repeat_long(b, 74); // entries 1..74
    m13_repeat_long(b, 74); // entries 75..148
    m13_repeat_long(b, 42); // entries 149..190
    m13_meta(b, META_INCREMENT); // entry 191, length 9
    m13_repeat_long(b, 74); // entries 192..265
    if (!overrun) {
        m13_repeat_long(b, 55); // entries 266..320
    } else {
        m13_repeat_long(b, 54); // entries 266..319
        m13_repeat_long(b, 74); // entry 320, then 73 stores past the end of the table
    }
}

// Emit code lengths of zero (unused) for every remaining entry of a 321-entry tree, given the number
// of entries emitted so far (at most 309)
static void m13_fill_unused(struct buffer *b, int entries) {
    m13_meta(b, META_ZERO);
    entries++;
    while (321 - entries >= 74 + 11) {
        m13_repeat_long(b, 74);
        entries += 74;
    }
    if (321 - entries > 74) {
        m13_repeat_long(b, 11);
        entries += 11;
    }
    m13_repeat_long(b, 321 - entries);
}

// Emit a literal byte with the code of m13_complete_tree()
static void m13_literal(struct buffer *b, unsigned byte) {
    for (int i = 7; i >= 0; i--)
        put_bit(b, (byte >> i) & 1, 1);
}

// --- Test cases: classic StuffIt ---

// Stored (method 0) fork that declares 16 bytes of output but holds only 8 bytes of data.
// Correct behaviour: the fork ends early after the 8 available bytes.
static void gen_sit_stored_short(const char *outdir) {
    struct buffer b;
    sit_classic_begin(&b, 0);
    put_bytes(&b, "testdata", 8);
    sit_classic_finish(&b, 16, 8, (const uint8_t *)"testdata", 8);
    save(outdir, "sit_stored_short", "sit", &b);
}

// Method 13 fork whose last code-length repeat command names 73 more stores than the 321-entry table
// has room for. Correct behaviour: returns an error.
static void gen_sit13_repeat_overrun(const char *outdir) {
    struct buffer b;
    sit_classic_begin(&b, 13);
    size_t start = b.len;
    m13_header(&b);
    m13_complete_tree(&b, 1);
    m13_empty_offset_tree(&b);
    for (const char *p = "testdata"; *p; p++)
        m13_literal(&b, (unsigned)*p);
    sit_classic_finish(&b, 8, (uint32_t)(b.len - start), (const uint8_t *)"testdata", 8);
    save(outdir, "sit13_repeat_overrun", "sit", &b);
}

// Method 13 fork whose code lengths are decremented below the "unused" value (zero, then two
// decrements). Correct behaviour: returns an error.
static void gen_sit13_negative_length(const char *outdir) {
    struct buffer b;
    sit_classic_begin(&b, 13);
    size_t start = b.len;
    m13_header(&b);
    m13_meta(&b, META_ZERO); // entry 0: length 0
    m13_meta(&b, META_DECREMENT); // entry 1: length -1
    m13_meta(&b, META_DECREMENT); // entry 2: length -2
    for (int entries = 3; entries < 321; entries += 53)
        m13_repeat_long(&b, 53); // 6 * 53 = 318 more entries of length -2
    m13_empty_offset_tree(&b);
    put_zeros(&b, 4);
    sit_classic_finish(&b, 8, (uint32_t)(b.len - start), (const uint8_t *)"testdata", 8);
    save(outdir, "sit13_negative_length", "sit", &b);
}

// Method 13 fork whose code lengths over-subscribe the code space: byte values 0 and 1 have 1-bit
// codes, which use up the whole code space, and byte value 2 has a 2-bit code.
// Correct behaviour: returns an error.
static void gen_sit13_oversubscribed(const char *outdir) {
    // What a decoder yields if it ignores the third code: every 0 bit is byte value 0
    static const uint8_t if_accepted[8] = {0};
    struct buffer b;
    sit_classic_begin(&b, 13);
    size_t start = b.len;
    m13_header(&b);
    m13_meta(&b, META_SET_LENGTH_1); // entry 0: length 1
    m13_meta(&b, META_REPEAT_FLAG); // flag clear: entry 1: length 1
    m13_bits(&b, 0, 1);
    m13_meta(&b, META_INCREMENT); // entry 2: length 2
    m13_fill_unused(&b, 3);
    m13_empty_offset_tree(&b);
    put_zeros(&b, 4);
    // The CRC matches that output, so only rejecting the code lengths makes this an error
    sit_classic_finish(&b, 8, (uint32_t)(b.len - start), if_accepted, 8);
    save(outdir, "sit13_oversubscribed", "sit", &b);
}

// Method 13 fork with an incomplete code: only byte value 0 has a code (the single bit 0), and the
// data then contains a 1 bit, which is not the start of any code. Correct behaviour: returns an error.
static void gen_sit13_missing_code(const char *outdir) {
    static const uint8_t zeros[8] = {0};
    struct buffer b;
    sit_classic_begin(&b, 13);
    size_t start = b.len;
    m13_header(&b);
    m13_meta(&b, META_SET_LENGTH_1); // entry 0: length 1
    m13_fill_unused(&b, 1);
    m13_empty_offset_tree(&b);
    m13_bits(&b, 0, 3); // three bytes of value 0
    m13_bits(&b, 1, 1); // no such code
    put_zeros(&b, 4);
    sit_classic_finish(&b, 8, (uint32_t)(b.len - start), zeros, 8);
    save(outdir, "sit13_missing_code", "sit", &b);
}

// Method 13 fork that declares 8 bytes of output, but whose compressed length covers the code for
// the first 2 bytes only. The code for the other 6 bytes is stored after the end of the fork, where
// a decoder must not look. Correct behaviour: returns an error.
static void gen_sit13_short_data(const char *outdir) {
    struct buffer b;
    sit_classic_begin(&b, 13);
    size_t start = b.len;
    m13_header(&b);
    m13_complete_tree(&b, 0);
    m13_empty_offset_tree(&b);
    m13_literal(&b, 't');
    m13_literal(&b, 'e');
    uint32_t comp_len = (uint32_t)(b.len - start) - 1; // the fork ends inside the second literal
    for (const char *p = "stdata"; *p; p++)
        m13_literal(&b, (unsigned)*p);
    put_zeros(&b, 4);
    sit_classic_finish(&b, 8, comp_len, (const uint8_t *)"testdata", 8);
    save(outdir, "sit13_short_data", "sit", &b);
}

// --- Test cases: StuffIt 5 ---

// StuffIt 5 archive whose only entry header declares a header size of 0, less than the 48 bytes of
// fixed fields that every entry header has. Correct behaviour: returns an error.
static void gen_sit5_short_header(const char *outdir) {
    struct buffer b;
    memset(&b, 0, sizeof(b));
    // 80-byte signature required by the format, then the archive header fields (sit.md "SIT5")
    put_bytes(&b, "StuffIt (c)1997-", 16);
    put_bytes(&b, "0000", 4);
    put_bytes(&b, " Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/", 58);
    put_bytes(&b, "\r\n", 2);
    put_be16(&b, 0);
    put_u8(&b, 5); // version
    put_u8(&b, 0); // flags
    put_be32(&b, 164); // total size
    put_be32(&b, 0);
    put_be16(&b, 1); // entry count
    put_be32(&b, 100); // offset of the first entry
    put_be16(&b, 0);

    // Entry header at offset 100
    put_be32(&b, 0xa5a5a5a5); // id
    put_u8(&b, 1); // version
    put_u8(&b, 0);
    put_be16(&b, 0); // header size: malformed, must be at least 48
    put_u8(&b, 0);
    put_u8(&b, 0x40); // flags: folder
    put_zeros(&b, 22); // dates, offsets, name length
    put_be16(&b, 0); // header CRC (the CRC of zero bytes is 0)
    put_be32(&b, 0xffffffff); // data length: folder marker
    put_zeros(&b, 10);
    put_zeros(&b, 16); // room for the fields that follow an entry header
    save(outdir, "sit5_short_header", "sit", &b);
}

// --- Compact Pro container (cpt.md "Archive structure") ---

#define CPT_FLAG_DATA_LZH 0x0004

// Append the directory for a single file whose data fork starts right after the 8-byte archive
// header, and point the archive header at it.
static void cpt_directory(struct buffer *b, unsigned flags, uint32_t uncomp_len, uint32_t comp_len) {
    set_be32(b, 4, (uint32_t)b->len); // directory offset
    put_be32(b, 0); // directory CRC
    put_be16(b, 1); // entry count
    put_u8(b, 0); // comment length
    put_u8(b, 4); // name length, file entry
    put_bytes(b, "file", 4);
    put_u8(b, 1); // volume
    put_be32(b, 8); // offset of the fork data
    put_bytes(b, "TEST", 4); // type
    put_bytes(b, "TEST", 4); // creator
    put_be32(b, 0); // creation date
    put_be32(b, 0); // modification date
    put_be16(b, 0); // Finder flags
    put_be32(b, 0); // data CRC
    put_be16(b, flags);
    put_be32(b, 0); // resource fork length
    put_be32(b, uncomp_len); // data fork length
    put_be32(b, 0); // compressed resource fork length
    put_be32(b, comp_len); // compressed data fork length
}

// Start a Compact Pro archive: identifier, volume number, cross-volume magic, directory offset
static void cpt_begin(struct buffer *b) {
    memset(b, 0, sizeof(*b));
    put_u8(b, 0x01);
    put_u8(b, 0x01);
    put_be16(b, 0);
    put_be32(b, 8);
}

// --- Test cases: Compact Pro ---

// Compact Pro archive whose directory lists no entries. Correct behaviour: nothing is extracted and
// no error is reported.
static void gen_cpt_no_entries(const char *outdir) {
    struct buffer b;
    cpt_begin(&b);
    put_be32(&b, 0); // directory CRC
    put_be16(&b, 0); // entry count
    put_u8(&b, 0); // comment length
    save(outdir, "cpt_no_entries", "cpt", &b);
}

// Compact Pro LZH fork whose literal code lengths over-subscribe the code space: byte values 250 and
// 251 have 1-bit codes and byte value 252 has a 2-bit code. Correct behaviour: the code table is
// rejected and the fork ends early with no output.
static void gen_cpt_lzh_oversubscribed(const char *outdir) {
    struct buffer b;
    cpt_begin(&b);
    size_t start = b.len;
    put_u8(&b, 127); // literal table: 127 bytes, two 4-bit lengths each
    put_zeros(&b, 125); // byte values 0..249 unused
    put_u8(&b, 0x11); // 250 and 251: length 1
    put_u8(&b, 0x20); // 252: length 2, 253: unused
    put_u8(&b, 0); // match length table: empty
    put_u8(&b, 0); // match offset table: empty
    // Four literals (flag bit 1, then a 1-bit code) for a decoder that accepts the table
    put_u8(&b, 0xbb);
    put_zeros(&b, 4);
    uint32_t comp_len = (uint32_t)(b.len - start);
    cpt_directory(&b, CPT_FLAG_DATA_LZH, 4, comp_len);
    save(outdir, "cpt_lzh_oversubscribed", "cpt", &b);
}

// Compact Pro run-length fork containing a run marker (0x81 0x82) with a count of 1, in a fork that
// declares 65536 bytes of output but holds only six input bytes. A count of N means N - 1 copies of
// the previous byte in total, so a count of 1 adds nothing after the copy that every run marker
// produces (cpt.md: repeat_count = max(0, count - 2)). Correct behaviour: the six input bytes decode
// to the four bytes "ttes" and the fork ends; a count of 1 does not expand to fill the declared size.
static void gen_cpt_rle_count_one(const char *outdir) {
    static const uint8_t fork[] = {'t', 0x81, 0x82, 0x01, 'e', 's'};
    struct buffer b;
    cpt_begin(&b);
    put_bytes(&b, fork, sizeof(fork));
    cpt_directory(&b, 0, 65536, sizeof(fork));
    save(outdir, "cpt_rle_count_one", "cpt", &b);
}

int main(int argc, char *argv[]) {
    const char *outdir = argc > 1 ? argv[1] : "../testfiles";

    gen_sit_stored_short(outdir);
    gen_sit13_repeat_overrun(outdir);
    gen_sit13_negative_length(outdir);
    gen_sit13_oversubscribed(outdir);
    gen_sit13_missing_code(outdir);
    gen_sit13_short_data(outdir);
    gen_sit5_short_header(outdir);
    gen_cpt_no_entries(outdir);
    gen_cpt_lzh_oversubscribed(outdir);
    gen_cpt_rle_count_one(outdir);

    printf("\nAll malformed test archives generated.\n");
    return 0;
}
