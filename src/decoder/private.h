#ifndef Z64DECOMPRESS_DECODER_PRIVATE_H_INCLUDED
#define Z64DECOMPRESS_DECODER_PRIVATE_H_INCLUDED

#include <string.h> /* memcpy */

#define Bcopy(SRC, DST, LEN) memcpy(DST, SRC, LEN)
#define DMARomToRam(SRC, DST, LEN) memcpy(DST, SRC, LEN)

/* XXX casting like *(unsigned int*) is used in n64 code but
 *     that assumes a big-endian build target; adapt to BE32()
 */
#define BE32(X) ( ((X)[0]<<24) | ((X)[1]<<16) | ((X)[2]<<8) | (X)[3] )

#endif /* Z64DECOMPRESS_DECODER_PRIVATE_H_INCLUDED */

