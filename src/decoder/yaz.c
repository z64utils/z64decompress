/* <z64.me> yaz decompression using intermediate buffer */

#include "private.h"

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

/* initialize yaz */
static inline unsigned char *init(void)
{
	unsigned int size;
	
	dec.buf_limit = dec.buf_end - 25;
	
	/* default size = decompression buffer size */
	size = dec.buf_end - dec.buf;
	
	/* if remaining file size is less than default, use that */
	if (dec.remaining < size)
		size = dec.remaining;
	
	DMARomToRam(dec.pstart, dec.buf, size);
	
	/* advance pstart */
	dec.pstart += size;
	
	/* decrease remaining sz */
	dec.remaining -= size;
	
	return dec.buf;
}

/* request more yaz data */
static inline unsigned char *refill(unsigned char *src)
{
	unsigned int    size;
	unsigned int    length;
	unsigned char  *dst;
	
	/* length = bytes remaining in buffer */
	length = dec.buf_end - src;
	
	/* bcopy src and dst must be aligned */
	if ((length & 7) == 0)
		dst = dec.buf;
	else
		dst = (dec.buf + 8) - (length & 7);
	
	/* copy remainder of current buffer back to beginning */
	Bcopy(src, dst, length);
	
	/* calculate size for next read */
	size = (dec.buf_end - dst) - length;
	
	/* if it exceeds remaining file size, use that */
	if (dec.remaining < size)
		size = dec.remaining;
	
	/* read file from rom */
	if (size != 0)
	{
		DMARomToRam(dec.pstart, dst + length, size);
		
		dec.pstart += size;
		dec.remaining -= size;
		
		if (dec.remaining == 0)
			dec.buf_limit = dst + length + size;
	}
	
	return dst;
}

/* decompress yaz data */
/* yaz0dec by thakis was referenced for this */
static inline size_t decompress(unsigned char *src, unsigned char *_dst)
{
	unsigned char *dst = _dst;
	unsigned int currCodeByte;
	unsigned int nmult;
	int validBitCount = 0;
	int uncomp_sz;
	
	/* get decompressed size from header */
	uncomp_sz = BE32(src + 4);
	
	/* skip header */
	src += 16;
	
	/* infinite loop */
	do
	{
		if (validBitCount == 0)
		{
			/* refill intermediate buffer if needed */
			if (dec.buf_limit < src && dec.remaining != 0)
				src = refill(src);
			
			currCodeByte = *src;
			validBitCount = 8;
			src++;
		}
		
		/* is not uncompressed */
		if (!(currCodeByte & 0x80))
		{
			unsigned char   byte1 = src[0];
			unsigned char   byte2 = src[1];
			
			unsigned int    dist = ((byte1 & 0xF) << 8) | byte2;
			unsigned char  *copySrc = dst - (dist + 1);
			
			unsigned int    numBytes = byte1 >> 4;
			
			src += 2;
			
			if (numBytes == 0)
			{
				numBytes = *src + 0x12;
				src++;
			}
			else
				numBytes += 2;
			
		/* NOTE: this is unrolled to maximize performance */
			
			/* get remaining bytes to a multiple of 4 */
			nmult = numBytes - (numBytes & 3);
			if (numBytes & 3)
			{
				do
				{
					*dst = *copySrc;
					dst++;
					copySrc++;
					numBytes -= 1;
				} while (numBytes != nmult);
				
				if (numBytes == 0)
					goto L_skip;
			}
			
			/* transfer remaining block four bytes at a time */
			do
			{
				dst[0] = copySrc[0];
				dst[1] = copySrc[1];
				dst[2] = copySrc[2];
				dst[3] = copySrc[3];
				numBytes -= 4;
				copySrc += 4;
				dst += 4;
			} while (numBytes != 0);
		}
		
		/* straight copy */
		else
			*dst++ = *src++;
		
L_skip:
		validBitCount -= 1;
		currCodeByte <<= 1;
	} while (dst != _dst + uncomp_sz);
	
#if MAJORA
	dec.dst_end = dst;
#endif

	return uncomp_sz;
}

/* main driver */
size_t yazdec(void *src, void *dst, size_t sz)
{
	size_t uncomp_sz;

	/* initialize decoder structure */
	dec.buf_end = dec.buf + sizeof(dec.buf);
	dec.pstart = src;
	dec.remaining = sz;
	
	/* decompress file */
	uncomp_sz = decompress(init(), dst);
	
#if MAJORA
	dec.buf_end = 0;
#endif

	return uncomp_sz;
}

