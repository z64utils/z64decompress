#ifndef Z64DECOMPRESS_DECODER_H_INCLUDED
#define Z64DECOMPRESS_DECODER_H_INCLUDED

void yazdec(void *src, void *dst, size_t sz);
void lzodec(void *src, void *dst, size_t sz);
void ucldec(void *src, void *dst, size_t sz);
void apldec(void *src, void *dst, size_t sz);

#endif /* Z64DECOMPRESS_DECODER_H_INCLUDED */

