DEPS=tp.c message_handling.c compression.c multiplexlist.c
DEPS_OPT=tp_optimized.c message_handling_optimized.c compression_optimized.c multiplexlist.c memory_pool.c

all: server create_config

server: server.c 
	gcc -pthread -g -o $@ $< $(DEPS) -lm

server_optimized_standalone: server_optimized.c
	gcc -pthread -O3 -march=native -o $@ $< message_handling.c compression.c multiplexlist.c memory_pool.c -lm

create_config: create_config.c
	gcc -o $@ $<

performance_tests: performance_tests.c
	gcc -pthread -g -o $@ $< compression.c multiplexlist.c -lm

performance_tests_optimized: performance_tests_optimized.c
	gcc -pthread -O3 -march=native -o $@ $< compression_optimized.c multiplexlist.c memory_pool.c -lm

benchmark_comparison: benchmark_comparison.c
	gcc -pthread -O2 -o $@ $< memory_pool.c compression.c multiplexlist.c -lm

benchmark_realistic: benchmark_realistic.c
	gcc -pthread -O2 -o $@ $< memory_pool.c -lm

benchmark_server: benchmark_server.c
	gcc -pthread -O2 -o $@ $< -lm

stress_test: stress_test.c
	gcc -pthread -O2 -o $@ $< -lm

clean:
	rm -f server server_optimized create_config config.bin performance_tests performance_tests_optimized baseline_performance.txt optimized_performance.txt benchmark_comparison benchmark_results.txt