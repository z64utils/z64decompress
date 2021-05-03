# build decoder functions
~/c/mxe/usr/bin/i686-w64-mingw32.static-gcc -DNDEBUG -s -Ofast -flto -lm -c -Wall src/decoder/*.c
mkdir -p o
mv *.o o

# build everything else
~/c/mxe/usr/bin/i686-w64-mingw32.static-gcc -o z64decompress.exe -DNDEBUG src/*.c o/*.o -Wall -Wextra -s -Os -flto -mconsole -municode

# move to bin directory
mkdir -p bin/win32
mv z64decompress.exe bin/win32

