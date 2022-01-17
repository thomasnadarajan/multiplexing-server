DEPS=tp.c message_handling.c compression.c multiplexlist.c
server: server.c 
	gcc -pthread -fsanitize=address -g -o $@ $< $(DEPS) -lm