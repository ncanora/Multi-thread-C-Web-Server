CC = gcc
CFLAGS = -g -Og
LDFLAGS = -lm -lpthread

EXEC = web_server
OBJS = web_server.o web_lib.o

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(EXEC) $(OBJS)
