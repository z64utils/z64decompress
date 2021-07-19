/* <z64.me> ucl decompression using intermediate buffer */

#include "private.h"

struct decoder
{
	unsigned char   buf[1024];   /* intermediate buffer for loading  */
	unsigned int    bb;          /* ucl: bit buffer                  */
	unsigned char  *pstart;      /* offset of next read from rom     */
	unsigned int    remaining;   /* compressed bytes not yet read    */
	unsigned int    ilen;        /* ucl: bytes processed in `buf`    */
#if MAJORA
	unsigned char  *dst_end;     /* end of decompressed block        */
#endif
};

static struct decoder dec;

/* these are used often, so shorten their names with a macro */
#define ilen   dec.ilen
#define bb     dec.bb

/* get next bit in bit buffer */
#define getbit(bb) getbit_dma()

/* unsafe, inline version of above, for speed */
#define getbit_unsafe(bb) \
	(((bb = bb & 0x7f \
		? (unsigned)(bb*2) \
		: (unsigned)(dec.buf[ilen++]*2+1) \
	) >> 8) & 1)

/* function version of above, for saving bytes in final binary */
#define getbit_unsafe_F(bb) getbit_dma_unsafe()

/* refill intermediate buffer if needed, and return new bit buffer */
static inline unsigned int refill(void)
{
	/* if we have exceeded the intermediate buffer, refill it */
	if (ilen >= sizeof(dec.buf) - 32)
	{
		unsigned size = sizeof(dec.buf);
		int offset = sizeof(dec.buf) - ilen;
		int Nilen;
		
		/* bcopy src and dst must be aligned */
		Nilen = 8 - (offset & 7);
		Nilen &= 7;
		
		/* the last block wraps around */
		if (offset)
		{
			size -= offset + Nilen;
			Bcopy(dec.buf + ilen, dec.buf + Nilen, offset);
		}
		ilen = Nilen;
		
		/* if it exceeds remaining file size, use that */
		if (dec.remaining < size)
			size = dec.remaining;
		
		/* read file from rom */
		if (size != 0)
		{
			DMARomToRam(dec.pstart, dec.buf + offset + Nilen, size);
			
			dec.pstart += size;
			dec.remaining -= size;
		}
	}
	
	return dec.buf[(ilen)++];
}

/* get next bit in bit buffer */
static int getbit_dma(void)
{
	if (bb & 0x7f)
	{
		bb *= 2;
		/* NOTE return is faster than goto, but costs 8 bytes */
		return (bb >> 8) & 1;		
//		goto early;
	}
	
	bb = refill() * 2 + 1;
//early:
	return (bb >> 8) & 1;
}

/* unsafe version of above, for speed */
static int (getbit_dma_unsafe)(void)
{
	if (bb & 0x7f)
	{
		bb *= 2;
		/* NOTE return is faster than goto, but costs 8 bytes */
		return (bb >> 8) & 1;		
//		goto early;
	}
	
	bb = dec.buf[(ilen)++] * 2 + 1;
//early:
	return (bb >> 8) & 1;
}

/* adapted from ucl/n2b_d.c */
size_t ucldec(void *_src, void *_dst, size_t sz)
{
	unsigned char *pstart = _src;
	unsigned char *dst = _dst;
	int last_m_off = 1;
	
	/* skip the 8-byte header */
	pstart += 8;
	sz -= 8;
	
	/* initialize decoder structure */
	dec.pstart = pstart;
	dec.remaining = sz;
	bb = 0;
	ilen = sizeof(dec.buf);

	for (;;)
	{
		int m_off;
		int m_len;

		while (getbit(bb))
			*dst++ = dec.buf[ilen++];
		
		m_off = 1;
		do {
			m_off = m_off*2 + getbit(bb);
		} while (!getbit(bb));
		if (m_off == 2)
			m_off = last_m_off;
		else
		{
			m_off = (m_off-3)*256 + dec.buf[ilen++];
			if (m_off == -1)
				break;
			last_m_off = ++m_off;
		}
		
		m_len = getbit_unsafe(bb);
		m_len = m_len*2 + getbit_unsafe_F(bb);
		if (m_len == 0)
		{
			m_len++;
			do {
				m_len = m_len*2 + getbit_unsafe_F(bb);
			} while (!getbit_unsafe_F(bb));
			m_len += 2;
		}
		m_len += (m_off > 0xd00);
		{
			unsigned char *m_pos;
			m_pos = dst - m_off;
			m_len += 1;
			
#if 0
			do *dst++ = *m_pos++; while (--m_len);
#else
		/* NOTE the unrolled variant below is 10% faster on Wii VC */
			
			/* get remaining bytes to a multiple of 4 */
			while (m_len & 3)
			{
				*dst++ = *m_pos++;
				m_len -= 1;
			}
			
			/* transfer remaining block four bytes at a time */
			while (m_len)
			{
				dst[0] = m_pos[0];
				dst[1] = m_pos[1];
				dst[2] = m_pos[2];
				dst[3] = m_pos[3];
				m_pos += 4;
				m_len -= 4;
				dst += 4;
			}
#endif /* unrolled variant */
		}
	}
	
#if MAJORA
	dec.dst_end = dst;
	bb = 0;
#endif

	/* get the final decompressed size */
	return dst - (unsigned char*)_dst;
}

