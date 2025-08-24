DEPS=tp.c message_handling.c compression.c multiplexlist.c

all: server create_config

server: server.c 
	gcc -pthread -fsanitize=address -g -o $@ $< $(DEPS) -lm

create_config: create_config.c
	gcc -o $@ $<

clean:
	rm -f server create_config config.bin