/* <z64.me> oot style zlib decompression using intermediate buffer */

#include "private.h"

/*
 * tinflate.c -- tiny inflate library
 * http://achurch.org/tinflate.c
 *
 * Written by Andrew Church <achurch@achurch.org>
 * This source code is public domain.
 *
 * ==============================================
 *
 * Version history:
 *
 * Version 1.7, 2017-06-04
 *   - Fixed two cases in which the Microsoft Visual Studio C compiler
 *     reported a signed/unsigned comparison mismatch due to integer
 *     promotions (from unsigned short to signed int).
 *
 * Version 1.6, 2016-02-18
 *   - Fixed a bug that caused incorrect output on systems with a 16-bit
 *     "int" type.  Thanks to Martijn Schipper for reporting the bug.
 *   - Fixed a slightly inaccurate comment.
 *
 * Version 1.5b, 2013-01-21
 *   - Style tweaks; no functional changes.
 *
 * Version 1.5a, 2012-03-16
 *   - Removed checks on function parameters which can be proven to always
 *     pass, instead documenting them as function preconditions; no
 *     functional changes.
 *   - As of this version, tinflate.c has been tested to 100% line and
 *     branch coverage, as reported by GCC's gcov tool (adjusted for an
 *     apparent failure to record data for certain return-to-branch cases).
 *
 * Version 1.5, 2011-12-04
 *   - Fixed failure to detect an RFC 1950 header in tinflate_partial()
 *     if the data was fed in one byte at a time.
 *   - Improved robustness against invalid Huffman table data.
 *   - Removed some redundant value checks.
 *   - Changed some "int" fields in the state block to "char" to reduce
 *     the structure size.
 *   - Fixed incompatibilities with the ANSI C (C89) standard.
 *   - Cleaned up some comments.
 *
 * Version 1.4a, 2010-11-03
 *   - Fixed warnings from GCC about comparisons between signed and
 *     unsigned values; no functional changes.
 *
 * Version 1.4, 2010-08-21
 *   - Fixed a decompression bug which would cause a crash or memory
 *     corruption when a repeated string was encountered after the output
 *     buffer size had been exceeded.  (This error did not occur when the
 *     output buffer was large enough for the decompressed data.)
 *   - Made tinflate_block() slightly more robust against a corrupted
 *     state block.
 *   - Renamed the INVALID state to INITIAL for clarity.
 *   - Changed C++-style comments to C-style for maximum compiler
 *     compatibility.
 *   - Cleaned up some comments.
 *
 * Version 1.3a, 2010-03-26
 *   - Minor comment clarifications and whitespace cleanup; no functional
 *     changes.
 *
 * Version 1.3, 2009-04-12
 *   - Fixed bugs in computing the CRC of decompressed data on systems
 *     where the "long" type is more than 32 bits wide (e.g. x86-64).
 *     The decompressed data itself is unaffected by the bug.
 *
 * Version 1.2, 2008-05-03
 *   - Fixed a bug in which tinflate() and tinflate_partial() could treat
 *     a non-final block as final if it ended on the last byte of input
 *     data.
 *   - Fixed a bug in which a dynamic Huffman table containing no symbols
 *     (a dynamic-table block with only literal codes) would cause a
 *     decompression error.
 *   - Added support for compiler-specific branch hinting (such support
 *     must be enabled externally).
 *   - Performed various minor optimizations.
 *
 * Version 1.1, 2007-08-31
 *   - Added tinflate_partial() and tinflate_state_size().
 *
 * Version 1.0, 2007-07-22
 *   - Initial release.
 *
 * ==============================================
 *
 * This file implements a simple, portable decompressor for the "deflate"
 * compression algorithm, as specified in RFC 1951.  The decompressor is
 * designed to be both independent of external libraries and as compact as
 * possible: it uses only around 2k of stack space (on the Intel x86
 * platform; other platforms may differ), and it does not require dynamic
 * memory management functions such as malloc() to be available.  Because
 * of this emphasis on size, however, the decompressor is not as fast as
 * other implementations, such as that in the "zlib" library.
 *
 * To decompress a stream of data compressed with the "deflate" algorithm,
 * call:
 *     tinflate(compressed_data, compressed_size,
 *              output_buffer, output_size,
 *              &crc_ret)
 * where:
 *     - "compressed_data" is a pointer to the first byte of the compressed
 *          data;
 *     - "compressed_size" is the number of bytes of compressed data;
 *     - "output_buffer" is a pointer to the first byte of the buffer into
 *          which the decompressed data will be stored;
 *     - "output_size" is the number of bytes available in the output
 *          buffer; and
 *     - "crc_ret" is a pointer to a variable which will receive the CRC-32
 *          of the decompressed data, or NULL if the CRC is not needed.
 *
 * The tinflate() function returns the number of bytes occupied by the
 * decompressed data (a nonnegative value), or an unspecified negative
 * number if the data cannot be decompressed.  Note that a too-small output
 * buffer is _not_ considered an error; the buffer will be filled with
 * decompressed data up to the number of bytes specified by "output_size",
 * and the function will return the number of bytes that would have been
 * stored given a buffer of infinite size.  (Thus, the decompressed size of
 * the data can be obtained by passing NULL for "output_buffer" and zero
 * for "output_size", though this incurs the computational expense of
 * actually decompressing the data once.)
 *
 * The tinflate() function automatically detects and skips past the 2-byte
 * header added by the "zlib" library's deflate() function, as defined in
 * RFC 1950.  However, preset dictionaries are not supported, and
 * tinflate() will return an error if the zlib header indicates that the
 * stream requires a preset dictionary.
 *
 * For cases where the compressed data is not available in its entirety,
 * the tinflate_partial() routine can be used instead to decompress the
 * stream one part at a time.  Call:
 *     tinflate_partial(compressed_data, compressed_size,
 *                      output_buffer, output_size,
 *                      &size_ret, &crc_ret,
 *                      state_buffer, state_buffer_size)
 * where "state_buffer" is a pointer to a region of memory which will be
 * used to store decompression state information, and "state_buffer_size"
 * is the size in bytes of the region.  The buffer must be zeroed before
 * the first tinflate_partial() call for a particular stream of data, and
 * it must not be changed between calls.  The required size of the region
 * in bytes can be obtained by calling the tinflate_state_size() function:
 *     int state_buffer_size = tinflate_state_size();
 *
 * tinflate_partial() will return zero (not the uncompressed data size) on
 * success, or a negative value on error.  If all input data is consumed
 * before the end of the compressed stream is reached, tinflate_partial()
 * will return a positive value; the function should then be called again,
 * passing the next block of input data in "compressed_data".  The total
 * size of the uncompressed data will be stored in the variable pointed to
 * by "size_ret" (if not NULL) when the end of the compressed stream is
 * reached.
 *
 * After tinflate_partial() returns, all data pointed to by
 * "compressed_data" will have been consumed, and the caller may freely
 * dispose of it.  The output buffer may also be freely moved and resized
 * between calls; however, any data discarded due to a full output buffer
 * cannot be recovered by resizing the output buffer after the fact (the
 * decompression must be restarted from the beginning to recover the lost
 * data).
 */

/*************************************************************************/
/************************ Local data declarations ************************/
/*************************************************************************/

/* Define the NULL symbol for convenience, if it's not already defined. */
#ifndef NULL
# define NULL 0
#endif

/*
 * If desired, LIKELY() and UNLIKELY() can be defined to take advantage of
 * compiler-specific branch hinting features.  The parameter "x" is an
 * expression which will be used as a truth value in a conditional test.
 * For example, the following definitions could be used with GCC:
 *     #define LIKELY(x)   __builtin_expect(!!(x), 1)
 *     #define UNLIKELY(x) __builtin_expect(!!(x), 0)
 * The default is to not perform any branch hinting.
 */
#ifndef LIKELY
# define LIKELY(x) (x)
#endif
#ifndef UNLIKELY
# define UNLIKELY(x) (x)
#endif

/* Structure of the decompression state buffer. */
typedef struct DecompressionState_ {
    /* state: Parsing state.  Used to resume processing at the appropriate
     * point after running out of data while decompressing a block. */
    enum {
        INITIAL = 0,  /* Initial state of a new state block (must be zero). */
        PARTIAL_ZLIB_HEADER,  /* Waiting for a second data byte. */
        HEADER,
        UNCOMPRESSED_LEN,
        UNCOMPRESSED_ILEN,
        UNCOMPRESSED_DATA,
        LITERAL_COUNT,
        DISTANCE_COUNT,
        CODELEN_COUNT,
        READ_CODE_LENGTHS,
        READ_LENGTHS,
        READ_LENGTHS_16,
        READ_LENGTHS_17,
        READ_LENGTHS_18,
        READ_SYMBOL,
        READ_LENGTH,
        READ_DISTANCE,
        READ_DISTANCE_EXTRA
    } state;

    /* in_ptr: Pointer to the next byte to be read from the input buffer. */
    const unsigned char *in_ptr;
    /* in_top: Pointer to one byte past the last byte of the input buffer. */
    const unsigned char *in_top;
    /* out_base: Pointer to the beginning of the output buffer. */
    unsigned char *out_base;
    /* out_ofs: Offset of the next byte to be stored in the output buffer. */
    unsigned long out_ofs;
    /* out_size: Total number of bytes in the output buffer. */
    unsigned long out_size;

    /* crc: Current CRC value. */
#ifdef WANT_CRC
    unsigned long crc;
#endif
    /* bit_accum: Bit accumulator. */
    unsigned long bit_accum;
    /* num_bits: Number of valid bits in accumulator. */
    unsigned char num_bits;
    /* final: Nonzero to indicate that the current block is the last one. */
    unsigned char final;

    /* first_byte: First byte read from the input stream.  Used to detect
     * an RFC 1950 (zlib) header when the input data is passed in one byte
     * at a time.  Only valid when state == PARTIAL_ZLIB_HEADER. */
    unsigned char first_byte;

    /* block_type: Compressed block type. */
    unsigned char block_type;
    /* counter: Generic counter. */
    unsigned int counter;
    /* symbol: Symbol corresponding to the code currently being processed. */
    unsigned int symbol;
    /* last_value: Last value read for length/distance table construction. */
    unsigned int last_value;
    /* repeat_length: Length of a repeated string. */
    unsigned int repeat_length;

    /* len: Length of an uncompressed block. */
    unsigned int len;
    /* ilen: Inverted length of an uncompressed block. */
    unsigned int ilen;
    /* nread: Number of bytes copied from an uncompressed block. */
    unsigned int nread;

    /* literal_table: Code-to-symbol conversion table for the alphabet used
     * for literals and length values.  Elements 0 and 1 correspond to a
     * one-bit code of 0 or 1, respectively; other elements are linked
     * (directly or indirectly) from these to represent the Huffman tree.
     * The value of each element is:
     *    - for terminal codes, the symbol corresponding to the code (a
     *      nonnegative value);
     *    - for nonterminal codes, the one's complement of the array index
     *      corresponding to the code with a zero appended (the following
     *      array element corresponds to the code with a one appended).
     * For an alphabet of N symbols, a Huffman tree will have N-1 non-leaf
     * nodes (including the root node, which is not represented in the
     * array).  In the case of the literal/length alphabet, there are
     * normally 286 symbols; however, the default (static) Huffman table
     * uses a 288-symbol alphabet with two unused symbols, so we reserve
     * enough space for that alphabet. */
    short literal_table[288*2-2];
    /* distance_table: Code-to-symbol conversion table for the alphabet
     * used for distances.  This alphabet consists of 32 symbols, 2 of
     * which are unused. */
    short distance_table[32*2-2];
    /* literal_count: Number of literal codes in the Huffman table (HLIT in
     * RFC 1951). */
    unsigned int literal_count;
    /* distance_count: Number of distance codes in the Huffman table (HDIST
     * in RFC 1951). */
    unsigned int distance_count;
    /* codelen_count: Number of code length codes in the Huffman table used
     * for decompressing the main Huffman tables (HCLEN in RFC 1951). */
    unsigned int codelen_count;
    /* codelen_table: Code-to-symbol conversion table for the alphabet used
     * for code lengths. */
    short codelen_table[19*2-2];
    /* literal_len, distance_len, codelen_len: Code length of the code for
     * each symbol in each alphabet. */
    unsigned char literal_len[288], distance_len[32], codelen_len[19];

} DecompressionState;

/* Local function declarations. */
static inline int tinflate_block(DecompressionState *state);
static int gen_huffman_table(unsigned int symbols,
                             const unsigned char *lengths,
                             int allow_no_symbols,
                             short *table);
#ifdef WANT_CRC
/* CRC-32 lookup table. */
static const unsigned long crc32_table[256] = {
    0x00000000UL, 0x77073096UL, 0xEE0E612CUL, 0x990951BAUL, 0x076DC419UL,
    0x706AF48FUL, 0xE963A535UL, 0x9E6495A3UL, 0x0EDB8832UL, 0x79DCB8A4UL,
    0xE0D5E91EUL, 0x97D2D988UL, 0x09B64C2BUL, 0x7EB17CBDUL, 0xE7B82D07UL,
    0x90BF1D91UL, 0x1DB71064UL, 0x6AB020F2UL, 0xF3B97148UL, 0x84BE41DEUL,
    0x1ADAD47DUL, 0x6DDDE4EBUL, 0xF4D4B551UL, 0x83D385C7UL, 0x136C9856UL,
    0x646BA8C0UL, 0xFD62F97AUL, 0x8A65C9ECUL, 0x14015C4FUL, 0x63066CD9UL,
    0xFA0F3D63UL, 0x8D080DF5UL, 0x3B6E20C8UL, 0x4C69105EUL, 0xD56041E4UL,
    0xA2677172UL, 0x3C03E4D1UL, 0x4B04D447UL, 0xD20D85FDUL, 0xA50AB56BUL,
    0x35B5A8FAUL, 0x42B2986CUL, 0xDBBBC9D6UL, 0xACBCF940UL, 0x32D86CE3UL,
    0x45DF5C75UL, 0xDCD60DCFUL, 0xABD13D59UL, 0x26D930ACUL, 0x51DE003AUL,
    0xC8D75180UL, 0xBFD06116UL, 0x21B4F4B5UL, 0x56B3C423UL, 0xCFBA9599UL,
    0xB8BDA50FUL, 0x2802B89EUL, 0x5F058808UL, 0xC60CD9B2UL, 0xB10BE924UL,
    0x2F6F7C87UL, 0x58684C11UL, 0xC1611DABUL, 0xB6662D3DUL, 0x76DC4190UL,
    0x01DB7106UL, 0x98D220BCUL, 0xEFD5102AUL, 0x71B18589UL, 0x06B6B51FUL,
    0x9FBFE4A5UL, 0xE8B8D433UL, 0x7807C9A2UL, 0x0F00F934UL, 0x9609A88EUL,
    0xE10E9818UL, 0x7F6A0DBBUL, 0x086D3D2DUL, 0x91646C97UL, 0xE6635C01UL,
    0x6B6B51F4UL, 0x1C6C6162UL, 0x856530D8UL, 0xF262004EUL, 0x6C0695EDUL,
    0x1B01A57BUL, 0x8208F4C1UL, 0xF50FC457UL, 0x65B0D9C6UL, 0x12B7E950UL,
    0x8BBEB8EAUL, 0xFCB9887CUL, 0x62DD1DDFUL, 0x15DA2D49UL, 0x8CD37CF3UL,
    0xFBD44C65UL, 0x4DB26158UL, 0x3AB551CEUL, 0xA3BC0074UL, 0xD4BB30E2UL,
    0x4ADFA541UL, 0x3DD895D7UL, 0xA4D1C46DUL, 0xD3D6F4FBUL, 0x4369E96AUL,
    0x346ED9FCUL, 0xAD678846UL, 0xDA60B8D0UL, 0x44042D73UL, 0x33031DE5UL,
    0xAA0A4C5FUL, 0xDD0D7CC9UL, 0x5005713CUL, 0x270241AAUL, 0xBE0B1010UL,
    0xC90C2086UL, 0x5768B525UL, 0x206F85B3UL, 0xB966D409UL, 0xCE61E49FUL,
    0x5EDEF90EUL, 0x29D9C998UL, 0xB0D09822UL, 0xC7D7A8B4UL, 0x59B33D17UL,
    0x2EB40D81UL, 0xB7BD5C3BUL, 0xC0BA6CADUL, 0xEDB88320UL, 0x9ABFB3B6UL,
    0x03B6E20CUL, 0x74B1D29AUL, 0xEAD54739UL, 0x9DD277AFUL, 0x04DB2615UL,
    0x73DC1683UL, 0xE3630B12UL, 0x94643B84UL, 0x0D6D6A3EUL, 0x7A6A5AA8UL,
    0xE40ECF0BUL, 0x9309FF9DUL, 0x0A00AE27UL, 0x7D079EB1UL, 0xF00F9344UL,
    0x8708A3D2UL, 0x1E01F268UL, 0x6906C2FEUL, 0xF762575DUL, 0x806567CBUL,
    0x196C3671UL, 0x6E6B06E7UL, 0xFED41B76UL, 0x89D32BE0UL, 0x10DA7A5AUL,
    0x67DD4ACCUL, 0xF9B9DF6FUL, 0x8EBEEFF9UL, 0x17B7BE43UL, 0x60B08ED5UL,
    0xD6D6A3E8UL, 0xA1D1937EUL, 0x38D8C2C4UL, 0x4FDFF252UL, 0xD1BB67F1UL,
    0xA6BC5767UL, 0x3FB506DDUL, 0x48B2364BUL, 0xD80D2BDAUL, 0xAF0A1B4CUL,
    0x36034AF6UL, 0x41047A60UL, 0xDF60EFC3UL, 0xA867DF55UL, 0x316E8EEFUL,
    0x4669BE79UL, 0xCB61B38CUL, 0xBC66831AUL, 0x256FD2A0UL, 0x5268E236UL,
    0xCC0C7795UL, 0xBB0B4703UL, 0x220216B9UL, 0x5505262FUL, 0xC5BA3BBEUL,
    0xB2BD0B28UL, 0x2BB45A92UL, 0x5CB36A04UL, 0xC2D7FFA7UL, 0xB5D0CF31UL,
    0x2CD99E8BUL, 0x5BDEAE1DUL, 0x9B64C2B0UL, 0xEC63F226UL, 0x756AA39CUL,
    0x026D930AUL, 0x9C0906A9UL, 0xEB0E363FUL, 0x72076785UL, 0x05005713UL,
    0x95BF4A82UL, 0xE2B87A14UL, 0x7BB12BAEUL, 0x0CB61B38UL, 0x92D28E9BUL,
    0xE5D5BE0DUL, 0x7CDCEFB7UL, 0x0BDBDF21UL, 0x86D3D2D4UL, 0xF1D4E242UL,
    0x68DDB3F8UL, 0x1FDA836EUL, 0x81BE16CDUL, 0xF6B9265BUL, 0x6FB077E1UL,
    0x18B74777UL, 0x88085AE6UL, 0xFF0F6A70UL, 0x66063BCAUL, 0x11010B5CUL,
    0x8F659EFFUL, 0xF862AE69UL, 0x616BFFD3UL, 0x166CCF45UL, 0xA00AE278UL,
    0xD70DD2EEUL, 0x4E048354UL, 0x3903B3C2UL, 0xA7672661UL, 0xD06016F7UL,
    0x4969474DUL, 0x3E6E77DBUL, 0xAED16A4AUL, 0xD9D65ADCUL, 0x40DF0B66UL,
    0x37D83BF0UL, 0xA9BCAE53UL, 0xDEBB9EC5UL, 0x47B2CF7FUL, 0x30B5FFE9UL,
    0xBDBDF21CUL, 0xCABAC28AUL, 0x53B39330UL, 0x24B4A3A6UL, 0xBAD03605UL,
    0xCDD70693UL, 0x54DE5729UL, 0x23D967BFUL, 0xB3667A2EUL, 0xC4614AB8UL,
    0x5D681B02UL, 0x2A6F2B94UL, 0xB40BBE37UL, 0xC30C8EA1UL, 0x5A05DF1BUL,
    0x2D02EF8DUL
};
#endif

/*************************************************************************/
/************************* Function definitions **************************/
/*************************************************************************/

/**
 * tinflate_partial:  Decompress a portion of a stream of data compressed
 * with the "deflate" algorithm.
 *
 * Parameters:
 *       compressed_data: Pointer to the portion of the compressed data to
 *                           process.
 *       compressed_size: Number of bytes of compressed data.
 *         output_buffer: Pointer to the buffer to receive uncompressed data.
 *           output_size: Size of the output buffer, in bytes.
 *              size_ret: Pointer to variable to receive the size of the
 *                           uncompressed data, or NULL if the size is not
 *                           needed.
 *               crc_ret: Pointer to variable to receive the CRC-32 of the
 *                           uncompressed data, or NULL if the CRC is not
 *                           needed.
 *          state_buffer: Pointer to a buffer to hold state information,
 *                           which must be zeroed before the first call for
 *                           the stream.
 *     state_buffer_size: Size of the state buffer, in bytes.  Must be no
 *                           less than the value returned by
 *                           tinflate_state_size().
 * Return value:
 *     Zero when the data has been completely decompressed; an unspecified
 *     positive value if the end of the input data is reached before
 *     decompression is complete; or an unspecified negative value if an
 *     error occurs.  (A full output buffer is not considered an error.)
 * Notes:
 *     The returned CRC value is only valid after the entire stream of data
 *     has been decompressed, and only when the decompressed data fits
 *     within the output buffer, i.e. when the value stored in *size_ret is
 *     no greater than the value of output_size.
 */
static
inline
int tinflate_partial(const void *compressed_data, long compressed_size,
                     void *output_buffer, long output_size,
                     unsigned long *size_ret, unsigned long *crc_ret,
                     void *state_buffer, long state_buffer_size)
{
    /* state: Decompression state buffer, cast to the structured type. */
    DecompressionState *state = (DecompressionState *)state_buffer;
    (void)crc_ret;

    /*-------------------------------------------------------------------*/

    /**** Check parameter validity. ****/

    if (compressed_data == NULL
     || compressed_size < 0
     || output_size < 0
     || (output_size > 0 && output_buffer == NULL)
     || state_buffer == NULL
     || state_buffer_size < (long)sizeof(DecompressionState)) {
        return -1;
    }

    /**** Insert data pointers into decompression state block. ****/

    state->in_ptr   = (const unsigned char *)compressed_data;
    state->in_top   = state->in_ptr + compressed_size;
    state->out_base = output_buffer;
    state->out_size = output_size;

    /**** If this is the first call, check for a zlib header, then ****
     **** initialize the processing state.                         ****/

    if (state->state == INITIAL || state->state == PARTIAL_ZLIB_HEADER) {
        /*
         * A zlib header is a big-endian 16-bit integer, composed of the
         * following fields:
         *     0xF000: Window size (log2(maximum_distance), 8..15) minus 8
         *     0x0F00: Compression method (always 8)
         *     0x00C0: Compression level
         *     0x0020: Custom dictionary flag
         *     0x001F: Check bits (set so the header is a multiple of 31)
         */
        unsigned int zlib_header;
        if (compressed_size == 0) {
            /* We didn't get any data at all, so there's no change in state. */
            return 1;
        }
        if (state->state == INITIAL && compressed_size == 1) {
            /* Save the single byte in the state block for next time. */
            state->first_byte = state->in_ptr[0];
            state->state = PARTIAL_ZLIB_HEADER;
            return 1;
        }
        if (state->state == PARTIAL_ZLIB_HEADER) {
            zlib_header = state->first_byte<<8 | state->in_ptr[0];
        } else {
            zlib_header = state->in_ptr[0]<<8 | state->in_ptr[1];
        }
        if ((zlib_header & 0x8F00) == 0x0800 && zlib_header % 31 == 0) {
            if (zlib_header & 0x0020) {
                /* This library does not support custom dictionaries. */
                return -1;
            }
            state->in_ptr += (state->state == PARTIAL_ZLIB_HEADER ? 1 : 2);
        } else if (state->state == PARTIAL_ZLIB_HEADER) {
            /* Return the first byte to the bitstream so we can process it
             * as part of the compressed data. */
            state->bit_accum = state->first_byte;
            state->num_bits = 8;
        }
        state->state = HEADER;
    }

    /**** Decompress blocks until either the end of the compressed ****
     **** data is reached, an error occurs, or a block with the    ****
     **** "final" bit is set.  ****/

    do {
        int res = tinflate_block(state);
        if (res != 0) {
            /* This will catch the out-of-data case, so we don't need to
             * check for out-of-data separately. */
            return res;
        }
        /* Ensure that the total output size has not rolled over to a
         * negative value; if it has, return an error. */
        if ((long)state->out_ofs < 0) {
            return -1;
        }
        /* Check for end-of-stream.  (Note that this flag will be set at
         * the beginning of processing the final block, but since we
         * already checked for end-of-block, we do not need to do so again
         * here.) */
    } while (!state->final);

    /**** Return the total decompressed size and CRC (if requested). ****/

    if (size_ret) {
        *size_ret = state->out_ofs;
    }
#ifdef WANT_CRC
    if (crc_ret) {
        *crc_ret = state->crc;
    }
#endif
    return 0;
}

/*************************************************************************/

/**
 * tinflate_block:  Decompress a single block of data compressed with the
 * "deflate" algorithm.
 *
 * Parameters:
 *     state: Decompression state buffer.
 * Return value:
 *     Zero on success, an unspecified positive value if the end of the
 *     input data is reached before decompression of the block is complete,
 *     or an unspecified negative value if an error other than reaching
 *     the end of the input data occurs.  (A full output buffer is not
 *     considered an error.)
 * Preconditions:
 *     state != NULL
 */
static inline int tinflate_block(DecompressionState *state)
{
    /* in_ptr, in_top, out_base, out_ofs, out_top, bit_accum, num_bits:
     * Local copies of state variables, to aid compiler optimization. */
    const unsigned char *in_ptr    = state->in_ptr;
    const unsigned char *in_top    = state->in_top;
          unsigned char *out_base  = state->out_base;
          unsigned long  out_ofs   = state->out_ofs;
          unsigned long  out_size  = state->out_size;
          unsigned long  bit_accum = state->bit_accum;
          unsigned int   num_bits  = state->num_bits;

#ifdef WANT_CRC
    /* icrc: Inverted (one's-complement) value of the running CRC for the
     * stream. */
    unsigned long icrc = ~(state->crc);
#endif

    /*-------------------------------------------------------------------*/

    /* The GETBITS macro retrieves the specified number of bits (n) from
     * the block, returning from the function if no more data is available,
     * and stores the value in the given variable (var).  The number of
     * bits to retrieve (n) must be no greater than 25. */

#define GETBITS(n,var)                                          \
    do {                                                        \
        const unsigned int __n = (n);  /* Avoid multiple evaluations. */ \
        while (UNLIKELY(num_bits < __n)) {                      \
            if (in_ptr >= in_top) {                             \
                goto out_of_data;                               \
            }                                                   \
            bit_accum |= ((unsigned long) *in_ptr) << num_bits; \
            num_bits += 8;                                      \
            in_ptr++;                                           \
        }                                                       \
        var = bit_accum & ((1UL << __n) - 1);                   \
        bit_accum >>= __n;                                      \
        num_bits -= __n;                                        \
    } while (0)

    /* The GETHUFF macro retrieves enough bits from the block to form a
     * Huffman code according to the given Huffman table (table), storing
     * the corresponding symbol into the given variable (var). */
#define GETHUFF(var,table)                              \
    do {                                                \
        unsigned int bits_used = 0;                     \
        unsigned int index = 0;                         \
        for (;;) {                                      \
            if (UNLIKELY(num_bits <= bits_used)) {      \
                if (in_ptr >= in_top) {                 \
                    goto out_of_data;                   \
                }                                       \
                bit_accum |= (unsigned long) *in_ptr << num_bits; \
                num_bits += 8;                          \
                in_ptr++;                               \
            }                                           \
            index += (bit_accum >> bits_used) & 1;      \
            bits_used++;                                \
            if ((table)[index] >= 0) {                  \
                break;                                  \
            }                                           \
            index = ~(table)[index];                    \
        }                                               \
        bit_accum >>= bits_used;                        \
        num_bits -= bits_used;                          \
        var = (table)[index];                           \
    } while (0)

    /* The PUTBYTE macro stores a byte into the output buffer, if any space
     * is available, and updates the decompressed byte count and CRC. */
#define PUTBYTE(byte)                           \
    do {                                        \
        const unsigned char __byte = (byte);    \
        if (LIKELY(out_ofs < out_size)) {       \
            out_base[out_ofs] = __byte;         \
        }                                       \
        out_ofs++;                              \
        UPDATECRC(__byte);                      \
    } while (0)

    /* The PUTBYTE_SAFE macro stores a byte into the output buffer and
     * updates the byte count and CRC, like PUTBYTE; however, the output
     * buffer is assumed to have space available.  This macro is used to
     * avoid the space-available test when it has already been performed
     * externally. */
#define PUTBYTE_SAFE(byte)                      \
    do {                                        \
        const unsigned char __byte = (byte);    \
        out_base[out_ofs] = __byte;             \
        out_ofs++;                              \
        UPDATECRC(__byte);                      \
    } while (0)

    /* The UPDATECRC macro updates the Adler32 CRC value stored in the
     * "crc" variable for the given output byte.  Note that the local
     * variable "__val" is renamed from "__byte" to avoid a name conflict
     * when used with the PUTBYTE macro above.  Also, the final "&"
     * operation is a no-op on systems where "unsigned long" is a 32-bit
     * integer, but is required when "unsigned long" is larger than 32 bits
     * to prevent incorrect results. */
#ifdef WANT_CRC
	#define UPDATECRC(byte)                         \
		 do {                                        \
		     const unsigned char __val = (byte);     \
		     icrc = crc32_table[(icrc & 0xFF) ^ __val] ^ ((icrc >> 8) & 0xFFFFFFUL);\
		 } while (0)
#else
	#define UPDATECRC(byte) do{}while(0)
#endif

    /*-------------------------------------------------------------------*/

    /**** If continuing processing from an interrupted block, jump to ****
     **** the appropriate location.                                   ****/

#define CHECK_STATE(s)  case s: goto state_##s
    switch (state->state) {
        CHECK_STATE(HEADER);
        CHECK_STATE(UNCOMPRESSED_LEN);
        CHECK_STATE(UNCOMPRESSED_ILEN);
        CHECK_STATE(UNCOMPRESSED_DATA);
        CHECK_STATE(LITERAL_COUNT);
        CHECK_STATE(DISTANCE_COUNT);
        CHECK_STATE(CODELEN_COUNT);
        CHECK_STATE(READ_CODE_LENGTHS);
        CHECK_STATE(READ_LENGTHS);
        CHECK_STATE(READ_LENGTHS_16);
        CHECK_STATE(READ_LENGTHS_17);
        CHECK_STATE(READ_LENGTHS_18);
        CHECK_STATE(READ_SYMBOL);
        CHECK_STATE(READ_LENGTH);
        CHECK_STATE(READ_DISTANCE);
        CHECK_STATE(READ_DISTANCE_EXTRA);
        case INITIAL:
        case PARTIAL_ZLIB_HEADER:
          /* Both of these are impossible, since tinflate_partial() handles
           * them on its own.  We include them here to avoid triggering a
           * compiler warning due to missing enumeration cases. */
          /* Empty statement to avoid syntax errors. */ ;
    }
    /* The state value is invalid, so return an error. */
    return -1;
#undef CHECK_STATE

    /**** Process the block header.  If the block is not a compressed ****
     **** block, process it and return from the function.             ****/

    /* Retrieve the block header. */
  state_HEADER:
    GETBITS(3, state->block_type);
    state->final = state->block_type & 1;
    state->block_type >>= 1;

    /* Check for blocks with an invalid block code. */
    if (state->block_type == 3) {
        goto error_return;
    }

    /* Check for uncompressed blocks, and just copy them to the output
     * buffer. */
    if (state->block_type == 0) {
        num_bits = 0;  /* Skip remaining bits in the previous byte. */
        state->state = UNCOMPRESSED_LEN;
      state_UNCOMPRESSED_LEN:
        GETBITS(16, state->len);
        state->state = UNCOMPRESSED_ILEN;
      state_UNCOMPRESSED_ILEN:
        GETBITS(16, state->ilen);
        if (state->ilen != (~state->len & 0xFFFF)) {
            /* Length values don't match, so the stream must be corrupted. */
            goto error_return;
        }
        /* Copy bytes to the output buffer. */
        state->nread = 0;
        state->state = UNCOMPRESSED_DATA;
      state_UNCOMPRESSED_DATA:
        while (state->nread < state->len) {
            if (in_ptr >= in_top) {
                goto out_of_data;
            }
            PUTBYTE(*in_ptr++);
            state->nread++;
        }
        /* Update the state buffer and return success. */
        state->in_ptr    = in_ptr;
        state->out_ofs   = out_ofs;
        /* This "& 0xFFFFFFFFUL" is a no-op on systems where the "long"
         * type is 32 bits wide; but where it is wider, the ~ operation
         * will set the high bits of the variable, so we need to clear
         * them out. */
#ifdef WANT_CRC
        state->crc       = ~icrc & 0xFFFFFFFFUL;
#endif
        state->bit_accum = bit_accum;
        state->num_bits  = num_bits;
        state->state     = HEADER;
        return 0;
    }  /* if (state->block_type == 0) */

    /**** Initialize the decoding tables. ****/

    if (state->block_type == 2) {  /* Dynamic tables. */

        /* codelen_order: Order of code lengths in the block header for the
         * code length alphabet. */
        static const unsigned char codelen_order[19] = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
        };

        /* Retrieve the three code counts from the block header. */
        state->state = LITERAL_COUNT;
      state_LITERAL_COUNT:
        GETBITS(5, state->literal_count);
        state->literal_count += 257;
        state->state = DISTANCE_COUNT;
      state_DISTANCE_COUNT:
        GETBITS(5, state->distance_count);
        state->distance_count += 1;
        state->state = CODELEN_COUNT;
      state_CODELEN_COUNT:
        GETBITS(4, state->codelen_count);
        state->codelen_count += 4;

        /* Retrieve the specified number of code lengths for the code
         * length alphabet, clearing the rest to zero. */
        state->counter = 0;
        state->state = READ_CODE_LENGTHS;
      state_READ_CODE_LENGTHS:
        while (state->counter < state->codelen_count) {
            GETBITS(3, state->codelen_len[codelen_order[state->counter]]);
            state->counter++;
        }
        for (; state->counter < 19; state->counter++) {
            state->codelen_len[codelen_order[state->counter]] = 0;
        }

        /* Generate the code length Huffman table. */
        if (!gen_huffman_table(19, state->codelen_len, 0,
                               state->codelen_table)) {
            goto error_return;
        }

        /* Read code lengths for the literal/length and distance alphabets. */
        {
            /* repeat_count: Number of times remaining to repeat value.
             * (We cannot run out of data while repeating values, so there
             * is no need to store this counter in the state buffer.) */
            unsigned int repeat_count;

            state->last_value = 0;
            state->counter = 0;
            state->state = READ_LENGTHS;
          state_READ_LENGTHS:
            repeat_count = 0;
            while (state->counter < state->literal_count + state->distance_count) {
                if (repeat_count == 0) {
                    /* Get the next value and/or repeat count from the
                     * bitstream. */
                    GETHUFF(state->symbol, state->codelen_table);
                    if (state->symbol < 16) {
                        /* Literal bit length. */
                        state->last_value = state->symbol;
                        repeat_count = 1;
                    } else if (state->symbol == 16) {
                        /* Repeat last bit length 3-6 times. */
                        state->state = READ_LENGTHS_16;
                      state_READ_LENGTHS_16:
                        GETBITS(2, repeat_count);
                        repeat_count += 3;
                    } else if (state->symbol == 17) {
                        /* Repeat "0" 3-10 times. */
                        state->last_value = 0;
                        state->state = READ_LENGTHS_17;
                      state_READ_LENGTHS_17:
                        GETBITS(3, repeat_count);
                        repeat_count += 3;
                    } else {  /* symbol == 18 */
                        /* Repeat "0" 11-138 times. */
                        state->last_value = 0;
                        state->state = READ_LENGTHS_18;
                      state_READ_LENGTHS_18:
                        GETBITS(7, repeat_count);
                        repeat_count += 11;
                    }
                }  /* if (repeat_count == 0) */
                if (state->counter < state->literal_count) {
                    state->literal_len[state->counter] = state->last_value;
                } else {
                    state->distance_len[state->counter - state->literal_count]
                        = state->last_value;
                }
                state->counter++;
                repeat_count--;
                state->state = READ_LENGTHS;
            }  /* while (counter < literal_count + distance_count) */
        }

        /* Generate the literal/length and distance Huffman tables.  The
         * distance table is allowed to have no symbols (as may happen if
         * the data is all literals). */
        if (!gen_huffman_table(state->literal_count, state->literal_len, 0,
                               state->literal_table)
         || !gen_huffman_table(state->distance_count, state->distance_len, 1,
                               state->distance_table)) {
            goto error_return;
        }

    } else {  /* Static tables. */

        int next_free = 2;  /* Next free index. */
        int i;

        /* All 1-6 bit codes are nonterminal. */
        for (i = 0; i < 0x7E; i++) {
            state->literal_table[i] = ~next_free;
            next_free += 2;
        }
        /* 7-bit codes 000 0000 through 001 0111 correspond to symbols 256
         * through 279. */
        for (; i < 0x96; i++) {
            state->literal_table[i] = (short)i + (256 - 0x7E);
        }
        /* All other 7-bit codes are nonterminal. */
        for (; i < 0xFE; i++) {
            state->literal_table[i] = ~next_free;
            next_free += 2;
        }
        /* 8-bit codes 0011 0000 through 1011 1111 correspond to symbols 0
         * through 143. */
        for (; i < 0x18E; i++) {
            state->literal_table[i] = (short)i + (0 - 0xFE);
        }
        /* 8-bit codes 1100 0000 through 1100 0111 correspond to symbols
         * 280 through 287.  (Symbols 286 and 287 are not used in the
         * compressed data itself, but they take part in the construction
         * of the code table.) */
        for (; i < 0x196; i++) {
            state->literal_table[i] = (short)i + (280 - 0x18E);
        }
        /* 8-bit codes 1100 1000 through 1111 1111 are nonterminal. */
        for (; i < 0x1CE; i++) {
            state->literal_table[i] = ~next_free;
            next_free += 2;
        }
        /* 9-bit codes 1 1001 0000 through 1 1111 1111 correspond to
         * symbols 144 through 255. */
        for (; i < 0x23E; i++) {
            state->literal_table[i] = (short)i + (144 - 0x1CE);
        }

        /* Distance codes are represented as 5-bit integers for static
         * tables; we treat them as Huffman codes, and set up a table here
         * so that they can be processed in the same manner as for dynamic
         * Huffman coding. */
        for (i = 0; i < 0x1E; i++) {
            state->distance_table[i] = ~(i*2+2);
        }
        for (i = 0x1E; i < 0x3E; i++) {
            state->distance_table[i] = i - 0x1E;
        }

    }  /* if (dynamic vs. static codes) */

    /**** Read and process codes from the compressed stream until we  ****
     **** find an end-of-stream symbol (256).  If we run out of data, ****
     **** the GETHUFF or GETBITS macro will exit the function, so we  ****
     **** do not need a separate check for an out-of-data condition.  ****/

    for (;;) {

        /* distance: The distance backward to the beginning of a repeated
         * string. */
        unsigned int distance;


        /* Ensure that the output offset has not rolled over to a negative
         * value; if it has, return an error.  (The "out_ofs" state field
         * is unsigned, so a rollover will not cause any improper memory
         * accesses, but this check ensures that (1) a caller who treats
         * the value as signed will not suffer negative rollover, and (2)
         * processing the next symbol will not cause the unsigned offset
         * value to roll over to zero.  The interface routines also treat
         * a potential negative rollover as an error, so this check will
         * not generate any spurious errors.) */
        if (UNLIKELY((long)out_ofs < 0)) {
            goto error_return;
        }

        state->state = READ_SYMBOL;
      state_READ_SYMBOL:
        /* Read a compressed symbol from the block. */
        GETHUFF(state->symbol, state->literal_table);

        /* If the symbol is a literal, add it to the buffer and continue
         * with the next code. */
        if (state->symbol < 256) {
            PUTBYTE(state->symbol);
            continue;
        }

        /* If the symbol indicates end-of-block, exit the decompression
         * loop. */
        if (state->symbol == 256) {
            break;
        }

        /* The symbol must indicate a repeated string length, so determine
         * the length, reading extra bits from the stream as necessary. */
        if (state->symbol <= 264) {
            state->repeat_length = (state->symbol-257) + 3;
        } else if (state->symbol <= 284) {
            state->state = READ_LENGTH;
          state_READ_LENGTH:
            /* Empty statement to avoid syntax errors. */ ;
            {
                const unsigned int length_bits = (state->symbol-261) / 4;
                GETBITS(length_bits, state->repeat_length);
                state->repeat_length +=
                    3 + ((4 + ((state->symbol-265) & 3)) << length_bits);
            }
        } else if (state->symbol == 285) {
            state->repeat_length = 258;
        } else {
            /* Invalid symbol. */
            goto error_return;
        }

        /* Read the distance symbol from the bitstream and determine the
         * backward distance to the string. */
        state->state = READ_DISTANCE;
      state_READ_DISTANCE:
        GETHUFF(state->symbol, state->distance_table);
        if (state->symbol <= 3) {
            distance = state->symbol + 1;
        } else if (state->symbol <= 29) {
            state->state = READ_DISTANCE_EXTRA;
          state_READ_DISTANCE_EXTRA:
            /* Empty statement to avoid syntax errors. */ ;
            {
                const unsigned int distance_bits = (state->symbol-2) / 2;
                GETBITS(distance_bits, distance);
                distance += 1 + ((2 + (state->symbol & 1)) << distance_bits);
            }
        } else {
            /* Invalid symbol. */
            goto error_return;
        }

        /* Ensure that the distance does not exceed the amount of data in
         * the output buffer.  If it does, return an error. */
        if (UNLIKELY(out_ofs < distance)) {
            goto error_return;
        }

        /* Copy bytes from the input buffer to the output buffer.  Since
         * the output pointer advances with each byte written, we can
         * simply use a constant offset (the value of "distance") from the
         * output pointer to retrieve the byte to copy.  If the output
         * buffer becomes full during the copy, the CRC value will not be
         * updated with the remaining bytes (as the source offset could
         * subsequently run past the end of the output buffer as well), but
         * since the CRC is explicitly undefined in this case, this is not
         * a problem. */
        {
            unsigned int repeat_length = state->repeat_length;
            unsigned int overflow = 0;
            if (UNLIKELY(out_ofs + repeat_length > out_size)) {
                if (out_ofs > out_size) {
                    overflow = repeat_length;
                } else {
                    overflow = (out_ofs - out_size) + repeat_length;
                }
                repeat_length -= overflow;
            }
            for (; repeat_length > 0; repeat_length--) {
                PUTBYTE_SAFE(out_base[out_ofs - distance]);
            }
            out_ofs += overflow;
        }

    }  /* End of decompression loop. */

    /**** Update the state buffer with our local state variables, ****
     **** and return success.                                     ****/

    state->in_ptr    = in_ptr;
    state->out_ofs   = out_ofs;
#ifdef WANT_CRC
    state->crc       = ~icrc & 0xFFFFFFFFUL;
   #endif
    state->bit_accum = bit_accum;
    state->num_bits  = num_bits;
    state->state     = HEADER;
    return 0;

    /*-------------------------------------------------------------------*/

    /**** Update the state buffer with our local state variables, ****
     **** and return an out-of-data result.                       ****/

  out_of_data:
    state->in_ptr    = in_ptr;
    state->out_ofs   = out_ofs;
#ifdef WANT_CRC
    state->crc       = ~icrc & 0xFFFFFFFFUL;
#endif
    state->bit_accum = bit_accum;
    state->num_bits  = num_bits;
    return 1;

    /**** Update the state buffer with our local state variables, ****
     **** and return an error result.                             ****/

  error_return:
    state->in_ptr    = in_ptr;
    state->out_ofs   = out_ofs;
#ifdef WANT_CRC
    state->crc       = ~icrc & 0xFFFFFFFFUL;
#endif
    state->bit_accum = bit_accum;
    state->num_bits  = num_bits;
    return -1;
}

/*************************************************************************/

/**
 * gen_huffman_table:  Generate a Huffman table from a set of code lengths,
 * using the algorithm described in RFC 1951.  The table format is as
 * described for the literal_table[] array in tinflate_block().
 *
 * Parameters:
 *              symbols: Number of symbols in the alphabet.
 *              lengths: Bit lengths of the codes for each symbol
 *                          (0 = symbol not used).
 *     allow_no_symbols: True (nonzero) if a table with no symbols (i.e.,
 *                          no nonzero code lengths) should be allowed.
 *                table: Array into which the Huffman table will be stored.
 * Return value:
 *     Nonzero on success, zero on failure (erroneous data).
 * Preconditions:
 *     symbols > 0 && symbols <= 288
 *     lengths != NULL
 *     table != NULL
 * Notes:
 *     lengths[] must contain the number of elements specified by
 *     "symbols"; table[] must have enough room for symbols*2-2 elements,
 *     or for 2 elements if "symbols" is 1; and all code lengths must be
 *     no greater than 15.
 */
static int gen_huffman_table(unsigned int symbols,
                             const unsigned char *lengths,
                             int allow_no_symbols,
                             short *table)
{
    /* length_count: Count of symbols with each code length. */
    unsigned short length_count[16];
    /* total_count: Count of all symbols with non-zero lengths. */
    unsigned short total_count;
    /* first_code: First code value to be used for each code length. */
    unsigned short first_code[16];
    /* index: Current table index.  This is guaranteed (modulo hardware
     * errors or memory corruption while this routine is running) to never
     * exceed symbols*2-2 entries, so its value is not bound-checked below.
     * This can be seen by simple induction:  Given a code alphabet of N
     * symbols (N >= 2), adding a new symbol N+1 involves taking a
     * previously terminal code and splitting it into two codes, one with
     * a 0 appended and the other with a 1 appended.  This is equivalent
     * to converting the corresponding leaf node of the Huffman tree to
     * an internal node and adding two new leaf nodes as children, thus
     * increasing the total node count by 2.  Since the table[] array
     * corresponds exactly to the Huffman tree, and index is incremented
     * exactly once for each node, index can never exceed the total number
     * of nodes in the tree (2N-2 for N symbols), which is also the
     * required length of the array. */
    unsigned int index;

    unsigned int i;

    /* Count the number of symbols that have each code length.  If an
     * invalid code length is found, abort. */
    for (i = 0; i < 16; i++) {
        length_count[i] = 0;
    }
    for (i = 0; i < symbols; i++) {
        /* We don't count codes of length 0 since they don't participate
         * in forming the tree.  (It's also convenient to have
         * length_count[0] == 0 for the code range calculation below.) */
        if (lengths[i] > 0) {
            length_count[lengths[i]]++;
        }
    }

    /* Check for a degenerate table of zero or one symbol. */
    total_count = 0;
    for (i = 1; i < 16; i++) {
        total_count += length_count[i];
    }
    if (total_count == 0) {
        return allow_no_symbols;
    } else if (total_count == 1) {
        for (i = 0; i < symbols; i++) {
            if (lengths[i] != 0) {
                table[0] = table[1] = i;
            }
        }
        return 1;
    }

    /* Determine the first code value for each code length, and make sure
     * the code space is completely filled as required.  Note that we rely
     * on length_count[0] being left at 0 above. */
    first_code[0] = 0;
    for (i = 1; i < 16; i++) {
        first_code[i] = (first_code[i-1] + length_count[i-1]) << 1;
        if (first_code[i] + length_count[i] > (unsigned short)1<<i) {
            /* Too many symbols of this code length -- we can't form a
             * valid Huffman tree. */
            return 0;
        }
    }
    if (first_code[15] + length_count[15] != (unsigned short)1<<15) {
        /* The Huffman tree is incomplete and thus invalid. */
        return 0;
    }

    /* Create the Huffman table, assigning codes to symbols sequentially
     * within each code length.  If the code value or table overflows
     * (presumably due to invalid data), abort. */
    index = 0;
    for (i = 1; i < 16; i++) {
        /* code_limit: Maximum code value for this code length, plus one. */
        unsigned int code_limit = 1U << i;
        /* next_code: First free code after all symbols with this code
         * length have been assigned. */
        unsigned int next_code = first_code[i] + length_count[i];
        /* next_index: First array index for the next higher code length. */
        unsigned int next_index = index + (code_limit - first_code[i]);
        unsigned int j;

        /* Fill in any symbols of this code length. */
        for (j = 0; j < symbols; j++) {
            if (lengths[j] == i) {
                table[index++] = j;
            }
        }

        /* Fill in remaining (internal) nodes for this length. */
        for (j = next_code; j < code_limit; j++) {
            table[index++] = ~next_index;
            next_index += 2;
        }
    }  /* for each code length */

    /* Return success. */
    return 1;
}

/*************************************************************************/
/*************************************************************************/

/* End of tinflate.c */

struct decoder
{
	unsigned char   buf[1024];   /* intermediate buffer for loading  */
	unsigned char  *buf_end;     /* pointer that exists for the sole *
	                              * purpose of getting size of `buf` */
	unsigned char  *pstart;      /* offset of next read from rom     */
	unsigned int    remaining;   /* remaining size of file           */
	unsigned char  *buf_limit;   /* points to end of scannable area  *
	                              * of buf; this prevents yaz parser *
	                              * from overflowing                 */
#if MAJORA
	unsigned char  *dst_end;     /* end of decompressed block        */
#endif
};

static struct decoder dec;

/* request more compressed data */
static inline unsigned refill(void)
{
	unsigned int    size;
	unsigned char  *dst = dec.buf;
	
	/* calculate size for next read */
	size = sizeof(dec.buf);
	
	/* if it exceeds remaining file size, use that */
	if (dec.remaining < size)
		size = dec.remaining;
	
	/* read file from rom */
	DMARomToRam(dec.pstart, dst, size);
	
	dec.pstart += size;
	dec.remaining -= size;
	
	return size;
}

/* main driver */
size_t zlibdec(void *src_, void *dst_, size_t sz)
{
	unsigned char *dst = dst_;
	unsigned char *src = src_;
	DecompressionState state;
	
	/* skip header */
	src += 8;
	sz -= 8;
	
	/* initialize decoder structure */
	dec.buf_end = dec.buf + sizeof(dec.buf);
	dec.pstart = src;
	dec.remaining = sz;

	/* clear decompression state buffer */
	state.state	    = INITIAL;
	state.out_ofs   = 0;
#ifdef WANT_CRC
	state.crc	    = 0;
#endif
	state.bit_accum = 0;
	state.num_bits  = 0;
	state.final	    = 0;
	/* no other fields need to be cleared */
	
	while (1)
	{
		int dstMax = 1024 * 1024 * 32; /* max size of any file: 32mb */
		unsigned long size = 0; /* XXX must be zero-initialized */
		unsigned long crc_ret;
		unsigned readSize;
		int result;
		readSize = refill();
		result = tinflate_partial(
			dec.buf, readSize,
			dst, dstMax,
			&size, &crc_ret,
			&state, sizeof(state)
		);
		dst += size;
		if (!result)
			break;
	}
	
#if MAJORA
	dec.buf_end = 0;
#endif
	return dst - (unsigned char *)dst_;
}


