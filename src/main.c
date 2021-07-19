/*
 * z64decompress <z64.me>
 * extra features by @zel640
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

typedef enum {
	CODEC_NONE = -1,
	CODEC_YAZ0,
	CODEC_LZO,
	CODEC_UCL,
	CODEC_APLIB,
	CODEC_MAX
} Codec;

typedef struct {
	const char *name; /* name used for program args */
	const char *header; /* identifer used in the headers of compressed files */
	size_t (*decode)(void *src, void *dst, size_t sz); /* decompression handler function */
} CodecInfo;

static CodecInfo decCodecInfo[CODEC_MAX] = {
	[CODEC_YAZ0]  = { "yaz"  , "Yaz0", yazdec },
	[CODEC_LZO]   = { "lzo"  , "LZO0", lzodec },
	[CODEC_UCL]   = { "ucl"  , "UCL0", ucldec },
	[CODEC_APLIB] = { "aplib", "APL0", apldec },
};

Codec get_codec_type_from_name(const char *name)
{
	for (int i = 0; i < CODEC_MAX; i++)
	{
		if (!strcmp(name, decCodecInfo[i].name))
		{
			return (Codec)i;
		}
	}
	return CODEC_NONE;
}

Codec get_codec_type_from_header(const void *header) {
    for (int i = 0; i < CODEC_MAX; i++)
    {
        if (!memcmp(decCodecInfo[i].header, header, 4))
        {
            return (Codec)i;
        }
    }
    return CODEC_NONE;
}

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
static size_t decompress(void *dst, void *src, size_t sz, Codec codecOverride)
{
	Codec codecHeader;

	assert(src != NULL);
	assert(dst != NULL);
	assert(sz != 0);

	/* override codec if requested rather than autodetecting it */
	if (codecOverride != CODEC_NONE)
	{
		return decCodecInfo[codecOverride].decode(src, dst, sz);
	}

	/* the codec header is the first 4 bytes of the file */
	codecHeader = get_codec_type_from_header(src);

	if (codecHeader != CODEC_NONE)
	{
		return decCodecInfo[codecHeader].decode(src, dst, sz);
	}

	die("ERROR: compressed file, unknown encoding");
	return 0;
}

/* decompress rom that uses the ZZRTL dmaext hack (returns pointer to decompressed rom) */
static inline void *romdec_dmaext(unsigned char *rom, size_t romSz, size_t *dstSz, Codec codecOverride)
{
	#define COMPRESSED (1 << 31)
	#define OVERLAP (1 <<  0)
	#define HEADER (1 <<  1)
	#define PMASK (~(COMPRESSED | OVERLAP | HEADER))
	
	/* macros for accessing dma entries */
	#define Vstart(X)   (beU32(((unsigned char *)X) + 0 * 4))
	#define Pbits(X)    (beU32(((unsigned char *)X) + 1 * 4))
	#define Pstart(X)   (Pbits(X) & PMASK)
	#define Vend(X)     (beU32(((unsigned char *)X) + 2 * 4))

	#define Traverse(X) X = (((unsigned char*)X) + ((Pbits(X) & OVERLAP) ? 2 : 3) * 4)
	
	unsigned char* dmaStart = NULL;
	unsigned char* dmaEnd = NULL;
	unsigned char *dmaCur;
	unsigned char *dec; // decompressed rom in ram

	/* ensure that dstSz is at least the size of the rom itself, but we will correct the value later */
	*dstSz = romSz;

	/* check to make sure a codec is provided since with dmaext the autodetection will fail */
	if (codecOverride == CODEC_NONE)
	{
		die("ERROR: dmaext requires a codec to to be provided");
		return NULL;
	}
	
	/* find dmadata in rom */
	for (dmaCur = rom; (unsigned)(dmaCur - rom) < romSz - 32; dmaCur += 0x10)
	{
		/* it is expected that dmaext dmadata will start with this entry */
		static unsigned char dmaExtStartMagic[] = {
			0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x01,
			0x00, 0x00, 0x10, 0x60,
			0x00, 0x00, 0x10, 0x61,
		};

		/* check if the magic value is found */
		if (!memcmp(dmaCur, dmaExtStartMagic, sizeof(dmaExtStartMagic)))
		{
			/* found the start */
			dmaStart = dmaCur;

			/* dmadata is confirmed to be found, now let's find the end of dmadata */
			/* we will also determine the end of the rom in this loop by finding the
			   largest decompressed end address of all the files */
			for (dmaCur = dmaStart, Traverse(dmaCur); Vstart(dmaCur) != 0; Traverse(dmaCur))
			{
				/* determine the "distal" end of the rom */
				if (*dstSz < Vend(dmaCur)) {
					*dstSz *= 2;
				}
			}
			dmaEnd = dmaCur;
			break;
		}
	}

	/* check if the start and end of dmadata was found */
	if (dmaStart == NULL) {
		die("ERROR: Could not find the start of dmadata!");
		return NULL;
	} else if (dmaEnd == NULL) {
		die("ERROR: Could not find the end of dmadata!");
		return NULL;
	}

	/* allocate decompressed rom */
	dec = calloc_safe(*dstSz, 1);

	/* transfer files from comp to dec, and decompress them if needed */
	for (dmaCur = dmaStart; dmaCur < dmaEnd; ) 
	{
		/* if file is compressed, decompress it! */
		if (Pbits(dmaCur) & COMPRESSED)
		{
            if (Pbits(dmaCur) & HEADER)
            {
                /* copy z64ext header */
                memmove(dec + Vstart(dmaCur), rom + Pstart(dmaCur), 0x10);

                /* decompress file while accounting for the header*/
                decompress(
                    dec + Vstart(dmaCur) + 0x10, /* dst */
                    rom + Pstart(dmaCur) + 0x10, /* src */
                    beU32(rom + Pstart(dmaCur) + 0x10), /* sz */
                    codecOverride
                );
            }
			else
			{
				/* no z64ext header */
				decompress(
					dec + Vstart(dmaCur), /* dst */
					rom + Pstart(dmaCur), /* src */
					beU32(rom + Pstart(dmaCur)), /* sz */
					codecOverride
				);
			}
		}
		else
		{
			/* not compressed */
			memcpy(dec + Vstart(dmaCur), rom + Pstart(dmaCur), Vend(dmaCur) - Vstart(dmaCur));
		}

		/* Update dma entries */
		wbeU32(dmaCur + (4 * 1), (Pbits(dmaCur) & (OVERLAP | HEADER)) | Vstart(dmaCur));

		/* find the next dma table entry */
		Traverse(dmaCur);
	}

	/* copy modified dmadata to decompressed rom */
	memcpy(dec + (dmaStart - rom), dmaStart, dmaEnd - dmaStart);
	
	/* update crc */
	n64crc(dec);

	/* return the pointer to the decompressed rom */
	return dec;
}

/* decompress rom (returns pointer to decompressed rom) */
static inline void *romdec(void *rom, size_t romSz, size_t *dstSz, Codec codecOverride)
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
		/* TODO iQue has the dmaStartMagic value x1050 instead of x1060 */
		static const unsigned char dmaStartMagic[] = { /* table always starts like so */
			0x00,0x00,0x00,0x00   /* Vstart */
			, 0x00,0x00,0x10,0x60 /* Vend   */
			, 0x00,0x00,0x00,0x00 /* Pstart */
			, 0x00,0x00,0x00,0x00 /* Pend   */
			, 0x00,0x00,0x10,0x60 /* Vstart (next) */
		};

		/* data doesn't match */
		if (memcmp(dma, dmaStartMagic, sizeof(dmaStartMagic)))
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
			decompress(
				dec + Vstart     /* dst */
				, comp + Pstart  /* src */
				, Pend - Pstart  /* sz  */
				, codecOverride  /* codecOverride */
			);
		}
		else
		{
			/* not compressed */
			memcpy(dec + Vstart, comp + Pstart, Vend - Vstart);
		}
		
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

static inline void *filedec(void *file, size_t fileSz, size_t *dstSz, Codec codecOverride) {
	unsigned char *dec;

	/* allocate file */
	dec = calloc_safe(1024 * 1024 * 8, 1);
	
	/* decompress */
	*dstSz = decompress(
		dec             /* dst */
		, file          /* src */
		, fileSz        /* sz  */
		, codecOverride /* codecOverride */
	);

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
	
	/* extension dmaStartMagic */
	strcpy(ss, append);
	
	return out;
}

static void showargs(void)
{
#define P(X) fprintf(stderr, X "\n")
	P("");
	P("Usage: z64decompress [file-in] [file-out] [options]");
	P("      The [file-out] argument is optional if you do not use any options.");
	P("      If not specified, \"file-in.decompressed.extension\" will be generated.");
	P("");
	P("Options:");
	P("  -h, --help            show help information");
	P("  -c, --codec           manually choose the decompression codec");
	P("  -i, --individual      decompress a compressed file-in into file-out (rather than a full rom)");
	P("  -d, --dmaext         decompress rom using the ZZRTL dmaext hack");
	P("");
	P("Example Usage:");
	P("   z64decompress \"rom-in.z64\" \"rom-out.z64\"");
	P("   z64decompress \"file-in.yaz\" \"file-out.bin\" -c yaz -i");
#ifdef _WIN32 /* helps users unfamiliar with command line */
	P("");
	P("Alternatively, Windows users can close this window and drop");
	P("a rom file directly onto the z64decompress executable.");
	getchar();
#endif
#undef P
}

/**************************************
 **         argument handlers        **
 **************************************/

/**
 * Returns a program argument with a field.
 * For example: `--codec "yaz0"`
 * This would return "yaz0" if "--codec" is searched for.
 * 
 * The alternate arg name can either be NULL if unused, or a shortened alias, such as
 * `--codec` or `-c`.
 */
static const char* get_arg_field(char** argv, const char* argName, const char* altArgName)
{
	/* initialize i at 1 to skip program name */
	for (int i = 1; argv[i] != NULL; i++)
	{
		if (!strcmp(argv[i], argName) || !strcmp(argv[i], altArgName))
		{
			/* found the arg */
			return argv[i + 1];
		}
	}
	return NULL;
}

/**
 * Returns non-zero if the provided argument was passed to the program.
 * For example: `--dmaext`
 * This would return true if `--dmaext` was passed to the program and was searched for.
 * 
 * The alternate arg name can either be NULL if unused, or a shortened alias, such as
 * `--codec` or `-c`.
 */
static int get_arg_bool(char** argv, const char* argName, const char* altArgName)
{
	/* initialize i at 1 to skip program name */
	for (int i = 1; argv[i] != NULL; i++)
	{
		if (!strcmp(argv[i], argName) || !strcmp(argv[i], altArgName))
		{
			/* found the arg */
			return 1;
		}
	}
	return 0;
}

wow_main
{
	/* input and output file names */
	char *inFileName, *outfileName;

	/* flag that determines if options are allowed (disabled when no output name is given) */
	int optionsFlag;

	/* flag that determines if individual files are decompressed or a whole rom */
	int individualFlag = 0;

	/* flag that determines if dmaext hack is used */
	int dmaExtFlag = 0;

	/* name of codec to use (for use with decCodecInfo.name) */
	Codec codecType = CODEC_NONE;

	/* decompressed file and size */
	void *dec;
	size_t decSz;

	/* compressed file and size */
	void *comp;
	size_t compSz;
	
	int exitCode = EXIT_SUCCESS;
	wow_main_argv;
	
	/* Always expect the first and second arguments to be the input and output filenames */
	#define ARG_INFILE  argv[1]
	#define ARG_OUTFILE argv[2]
	
	/* welcome message */
	fprintf(stderr, "welcome to z64decompress 1.0.1 <z64.me>\n");
	fprintf(stderr, "extra features by @zel640\n");

	/* no arguments given to the program or user request help */
	if (argc <= 1 || get_arg_bool(argv, "--help", "-h"))
	{
		showargs();
		return EXIT_FAILURE;
	}
	
	/* get the input and output files */
	inFileName = ARG_INFILE;
	if (argc <= 2) 
	{
		/* user did not specify output file */
		optionsFlag = 0;
		outfileName = quickOutname(inFileName);
	}
	else
	{
		/* user specified output file */
		optionsFlag = 1;
		outfileName = ARG_OUTFILE;
	}

	/* parse options */
	if (optionsFlag)
	{
		const char *codecName;

		/* booleans */
		individualFlag = get_arg_bool(argv, "--individual", "-i");
		dmaExtFlag = get_arg_bool(argv, "--dmaext", "-d");

		/* fields */
		codecName = get_arg_field(argv, "--codec", "-c");
		
		if (codecName)
		{
			codecType = get_codec_type_from_name(codecName);

			if (codecType == CODEC_NONE)
			{
				die("ERROR: invalid codec name: %s\n", codecName);
			}
		}
	}

	/* attempt to load file */
	comp = file_load(inFileName, &compSz);
	
	if (!individualFlag)
	{
		/* attempt to decompress rom */
		if (dmaExtFlag)
		{
			dec = romdec_dmaext(comp, compSz, &decSz, codecType);
		}
		else
		{
			dec = romdec(comp, compSz, &decSz, codecType);
		}
	} 
	else
	{
		if (dmaExtFlag)
		{
			die("ERROR: dmaext can not be used with individual files!");
		}
		/* attempt to decompress individual file */
		dec = filedec(comp, compSz, &decSz, codecType);
	}

	/* write out file */
	file_write(outfileName, dec, decSz);

	fprintf(
		stderr
		, "decompressed file '%s' written successfully\n"
		, outfileName
	);

	/* cleanup */
	free(comp);
	free(dec);

	if (outfileName != ARG_OUTFILE)
	{
		free(outfileName);
#ifdef _WIN32 /* assume user dropped file onto z64decompress.exe */
		getchar();
#endif
	}
	
	return exitCode;
}

