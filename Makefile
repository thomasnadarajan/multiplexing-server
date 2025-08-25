DEPS=tp.c message_handling.c compression.c multiplexlist.c
DEPS_OPT=tp_optimized.c message_handling_optimized.c compression_optimized.c multiplexlist.c memory_pool.c

all: server create_config

server: server.c 
	gcc -pthread -fsanitize=address -g -o $@ $< $(DEPS) -lm

server_optimized: server.c
	gcc -pthread -O3 -march=native -o $@ $< $(DEPS_OPT) -lm

create_config: create_config.c
	gcc -o $@ $<

performance_tests: performance_tests.c
	gcc -pthread -g -o $@ $< compression.c multiplexlist.c -lm

performance_tests_optimized: performance_tests_optimized.c
	gcc -pthread -O3 -march=native -o $@ $< compression_optimized.c multiplexlist.c memory_pool.c -lm

clean:
	rm -f server server_optimized create_config config.bin performance_tests performance_tests_optimized baseline_performance.txt optimized_performance.txt