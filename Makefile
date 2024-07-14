CFLAGS =-std=c17 -Wall -Wextra -Werror
LIBS =-L src\lib -lmingw32 -lSDL2main -lSDL2
INCLUDES =-I src\include

all:
	gcc chip8.c -o chip8 $(CFLAGS) $(LIBS) $(INCLUDES)

debug:
	gcc chip8.c -o chip8 -DDEBUG $(CFLAGS) $(LIBS) $(INCLUDES)