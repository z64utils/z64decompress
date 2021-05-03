/*
 * z64decompress <z64.me>
 *
 * simple z64 ocarina/majora rom decompressor
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "decoder/decoder.h"
#include "n64crc.h"
#include "file.h"
#include "wow.h"

#define STRIDE 16 /* bytes per dmadata entry */
#define IDX    2  /* dmadata references itself at table[IDX] */
#define STR32(X) (unsigned)((X[0]<<24)|(X[1]<<16)|(X[2]<<8)|X[3])
#define DMA_DELETED 0xffffffff /* aka UINT32_MAX */

/* big-endian bytes to u32 */
static inline unsigned beU32(void *bytes)
{
	unsigned char *b = bytes;
	return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

/* write u32 as big-endian bytes */
static inline void wbeU32(void *bytes, unsigned v)
{
	unsigned char *b = bytes;
	b[0] = v >> 24;
	b[1] = v >> 16;
	b[2] = v >>  8;
	b[3] = v;
}

/* decompress a file (returns non-zero if unknown codec) */
static inline int decompress(void *dst, void *src, int sz)
{
	unsigned codec;
	
	assert(src);
	assert(dst);
	assert(sz);
	
	codec = beU32(src);
	if (codec == STR32("Yaz0"))
		yazdec(src, dst, sz);
	else if (codec == STR32("LZO0"))
		lzodec(src, dst, sz);
	else if (codec == STR32("UCL0"))
		ucldec(src, dst, sz);
	else if (codec == STR32("APL0"))
		apldec(src, dst, sz);
	else
		return 1;
	
	return 0;
}

/* decompress rom (returns pointer to decompressed rom) */
static inline void *romdec(void *rom, unsigned romSz, unsigned *dstSz)
{
	unsigned char *comp = rom; /* compressed rom */
	unsigned char *dec;
	unsigned char *dma;
	unsigned char *dmaStart;
	unsigned char *dmaEnd = 0;
	unsigned dmaNum = 0;
	
	/* find dmadata in rom */
	dmaStart = 0;
	for (dma = comp; (unsigned)(dma - comp) < romSz - 32; dma += 16)
	{
		/* TODO iQue has the magic value x1050 instead of x1060 */
		const unsigned char magic[] = { /* table always starts like so */
			0x00,0x00,0x00,0x00   /* Vstart */
			, 0x00,0x00,0x10,0x60 /* Vend   */
			, 0x00,0x00,0x00,0x00 /* Pstart */
			, 0x00,0x00,0x00,0x00 /* Pend   */
			, 0x00,0x00,0x10,0x60 /* Vstart (next) */
		};
		
		/* data doesn't match */
		if (memcmp(dma, magic, sizeof(magic)))
			continue;
		
		/* table[IDX].Vstart isn't current rom offset */
		if (beU32(dma + STRIDE * IDX) != (unsigned)(dma - comp))
			continue;
		
		/* all tests passed; this is dmadata */
		dmaStart = dma;
		dmaNum = (beU32(dma + STRIDE * IDX + 4) - (dma - comp)) / STRIDE;
		dmaEnd = dmaStart + dmaNum * STRIDE;
	}
	
	/* failed to locate dmadata in rom */
	if (!dmaStart)
		die("failed to locate dmadata in rom");
	
	/* determine distal end of decompressed rom */
	*dstSz = romSz;
	for (dma = dmaStart; dma < dmaEnd; dma += STRIDE)
	{
		unsigned Vend = beU32(dma + 4);
		if (Vend > *dstSz)
			*dstSz *= 2;
	}
	
	/* allocate decompressed rom */
	dec = calloc_safe(*dstSz, 1);
	
	/* transfer files from comp to dec */
	for (dma = dmaStart; dma < dmaEnd; dma += STRIDE)
	{
		unsigned Vstart = beU32(dma +  0); /* virtual addresses */
		unsigned Vend   = beU32(dma +  4);
		unsigned Pstart = beU32(dma +  8); /* physical addresses */
		unsigned Pend   = beU32(dma + 12);
		
		/* unused or invalid entry */
		if (Pstart == DMA_DELETED
			|| Vstart == DMA_DELETED
			|| Pend == DMA_DELETED
			|| Vend == DMA_DELETED
			|| Vend <= Vstart /* sizes must be > 0 */
			|| (Pend && Pend == Pstart)
		)
			continue;
		
		/* compressed */
		if (Pend)
		{
			int err = decompress(
				dec + Vstart     /* dst */
				, comp + Pstart  /* src */
				, Pend - Pstart  /* sz  */
			);
			
			if (err)
				die("dma entry at %08x: compressed file, unknown encoding"
					, (unsigned)(dma - comp)
				);
		}
		
		/* not compressed */
		else
			memcpy(dec + Vstart, comp + Pstart, Vend - Vstart);
		
		/* update dma entry */
		wbeU32(dma +  8, Vstart);
		wbeU32(dma + 12, 0);
	}
	
	/* copy modified dmadata to decompressed rom */
	memcpy(dec + (dmaStart - comp), dmaStart, dmaNum * STRIDE);
	
	/* update crc */
	n64crc(dec);
	
	return dec;
}

/* take "infile.z64" and make "infile.decompressed.z64" */
static char *quickOutname(char *in)
{
	const char *append = ".decompressed.z64";
	char *out;
	char *ss;
	char *slash;
	
	out = malloc_safe(strlen(in) + 1 + strlen(append));
	strcpy(out, in);
	
	/* find last directory character (if there are any) */
	slash = strrchr(out, '/');
	if (!slash)
		slash = strrchr(out, '\\');
	if (!slash)
		slash = out;
	
	/* eliminate extension, if there is one */
	if ((ss = strrchr(out, '.')) && ss > slash)
		*ss = '\0';
	/* otherwise, so use end of string */
	ss = slash + strlen(slash);
	
	/* extension magic */
	strcpy(ss, append);
	
	return out;
}

static void showargs(void)
{
#define P(X) fprintf(stderr, X "\n")
	P("args: z64decompress \"in-file.z64\" \"out-file.z64\"");
	P("");
	P("The \"out-file.z64\" argument is optional. If not specified,");
	P("\"in-file.decompressed.z64\" will be generated.");
#ifdef _WIN32 /* helps users unfamiliar with command line */
	P("");
	P("Alternatively, Windows users can close this window and drop");
	P("a rom file directly onto the z64decompress executable.");
	getchar();
#endif
#undef P
}

wow_main
{
	char *infile;
	char *outfile;
	void *dec = 0;
	void *rom = 0;
	unsigned romSz;
	unsigned decSz;
	int exitCode = EXIT_SUCCESS;
	wow_main_argv;
	
	#define ARG_INFILE  argv[1]
	#define ARG_OUTFILE argv[2]
	
	fprintf(stderr, "welcome to z64decompress <z64.me>\n");
	if (argc < 2)
	{
		showargs();
		return EXIT_FAILURE;
	}
	
	infile = ARG_INFILE;
	if (argc < 3)
		outfile = quickOutname(infile);
	else
		outfile = ARG_OUTFILE;
	
	/* attempt to load rom */
	rom = file_load(infile, &romSz);
	
	/* attempt to decompress rom */
	dec = romdec(rom, romSz, &decSz);
	
	/* write out rom */
	file_write(outfile, dec, decSz);
	
	fprintf(
		stderr
		, "decompressed rom '%s' written successfully\n"
		, outfile
	);
	
	/* cleanup */
	free(rom);
	free(dec);
	if (outfile != ARG_OUTFILE)
	{
		free(outfile);
#ifdef _WIN32 /* assume user dropped file onto z64decompress.exe */
		getchar();
#endif
	}
	
	return exitCode;
}

