#include <assert.h>

#include "wow.h"
#undef   fopen
#undef   fread
#undef   fwrite
#undef   remove
#define  fopen   wow_fopen
#define  fread   wow_fread
#define  fwrite  wow_fwrite
#define  remove  wow_remove

/* get size of a file; returns 0 if fopen fails */
unsigned file_size(const char *fn)
{
	FILE *fp;
	unsigned sz;
	
	fp = fopen(fn, "rb");
	if (!fp)
		return 0;
	
	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	fclose(fp);
	
	return sz;
}

/* load a file into an existing buffer */
void *file_load_into(const char *fn, size_t *sz, void *dst)
{
	FILE *fp;
	
	assert(fn);
	assert(sz);
	assert(dst);
	
	*sz = 0;
	
	fp = fopen(fn, "rb");
	if (!fp)
		die("failed to open '%s' for reading", fn);
	
	fseek(fp, 0, SEEK_END);
	*sz = ftell(fp);
	
	if (!*sz)
		die("size of file '%s' is zero", fn);
	
	fseek(fp, 0, SEEK_SET);
	
	if (fread(dst, 1, *sz, fp) != *sz)
		die("failed to read contents of '%s'", fn);
	
	fclose(fp);
	
	return dst;
}

/* load a file */
void *file_load(const char *fn, size_t *sz)
{
	unsigned char *dst;
	
	assert(fn);
	assert(sz);
	
	*sz = file_size(fn);
	if (!*sz)
		die("failed to get size of file '%s'", fn);
	
	dst = malloc_safe(*sz);
	
	return file_load_into(fn, sz, dst);
}

/* write file */
unsigned file_write(const char *fn, void *data, unsigned data_sz)
{
	FILE *fp;
	
	assert(fn);
	assert(data);
	assert(data_sz);
	
	fp = fopen(fn, "wb");
	if (!fp)
		die("failed to open '%s' for writing", fn);
	
	if (fwrite(data, 1, data_sz, fp) != data_sz)
		die("failed to write contents of '%s'", fn);
	
	fclose(fp);
	
	return data_sz;
}

