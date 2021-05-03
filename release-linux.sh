# build decoder functions
gcc -DNDEBUG -s -Ofast -flto -lm -c -Wall -Wextra src/decoder/*.c
mkdir -p o
mv *.o o

# build everything else
gcc -o z64decompress -DNDEBUG src/*.c o/*.o -Wall -Wextra -s -Os -flto

# move to bin directory
mkdir -p bin/linux64
mv z64decompress bin/linux64



