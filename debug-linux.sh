# build decoder functions
gcc -Og -g -lm -c -Wall -Wextra src/decoder/*.c
mkdir -p o
mv *.o o

# build everything else
gcc -o z64decompress src/*.c o/*.o -Wall -Wextra -Og -g

