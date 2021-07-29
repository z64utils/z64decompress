#ifndef Z64DECOMPRESS_DECODER_H_INCLUDED
#define Z64DECOMPRESS_DECODER_H_INCLUDED

size_t yazdec(void *src, void *dst, size_t sz);
size_t lzodec(void *src, void *dst, size_t sz);
size_t ucldec(void *src, void *dst, size_t sz);
size_t apldec(void *src, void *dst, size_t sz);
size_t zlibdec(void *src, void *dst, size_t sz);

#endif /* Z64DECOMPRESS_DECODER_H_INCLUDED */

