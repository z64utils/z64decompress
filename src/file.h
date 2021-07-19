#ifndef Z64DECOMPRESS_FILE_H_INCLUDED
#define Z64DECOMPRESS_FILE_H_INCLUDED

/* get size of a file; returns 0 if fopen fails */
unsigned file_size(const char *fn);

/* load a file into an existing buffer */
void *file_load_into(const char *fn, size_t *sz, void *dst);

/* load a file */
void *file_load(const char *fn, size_t *sz);

/* write file */
unsigned file_write(const char *fn, void *data, unsigned data_sz);

#endif /* Z64DECOMPRESS_FILE_H_INCLUDED */

