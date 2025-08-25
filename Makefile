DEPS=tp.c message_handling.c compression.c multiplexlist.c
DEPS_OPT=tp_optimized.c message_handling_optimized.c compression_optimized.c multiplexlist.c memory_pool.c

all: server create_config

server: server.c 
	gcc -pthread -g -o $@ $< $(DEPS) -lm

server_optimized_standalone: server_optimized.c
	gcc -pthread -O3 -march=native -o $@ $< message_handling.c compression.c multiplexlist.c memory_pool.c -lm

create_config: create_config.c
	gcc -o $@ $<

stress_test: stress_test.c
	gcc -pthread -O2 -o $@ $< -lm

clean:
	rm -f server server_optimized_standalone create_config config.bin stress_test *.bin