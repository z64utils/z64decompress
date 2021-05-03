# z64decompress

`z64decompress` is a program for decompressing retail Zelda 64 rom files. It also supports roms compressed with [`z64compress`](https://github.com/z64me/z64compress) and its many encoding formats (`yaz`, `lzo`, `ucl`, and `aplib`). The decoders are adapted from those in my [`z64enc`](https://github.com/z64me/z64enc) repo.

## Usage
```
  z64decompress "in-file.z64" "out-file.z64"
  
  The "out-file.z64" argument is optional. If not specified,
  "in-file.decompressed.z64" will be generated.
  
  Alternatively, Windows users can drop an input rom directly
  onto the executable.
```

## Building
I have included shell scripts for building Linux and Windows binaries. Windows binaries are built using a cross compiler ([I recommend `MXE`](https://mxe.cc/)).

