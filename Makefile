CXX ?= clang++
CXXFLAGS ?= -O3 -std=c++17 -pthread
MARCH ?= native

ifeq ($(MARCH),)
  CXXFLAGS +=
else
  CXXFLAGS += -march=$(MARCH)
endif

BUILD_DIR := build
INCLUDES := -I.

BENCH_SRC := bench/bench_main.cpp
MEMPOOL_SRC := memory_pool.cpp

JEMALLOC_LIBS ?= $(shell pkg-config --libs jemalloc 2>/dev/null)
JEMALLOC_CFLAGS ?= $(shell pkg-config --cflags jemalloc 2>/dev/null)
TCMALLOC_LIBS ?= $(shell pkg-config --libs libtcmalloc_minimal 2>/dev/null)
TCMALLOC_CFLAGS ?= $(shell pkg-config --cflags libtcmalloc_minimal 2>/dev/null)
ifeq ($(TCMALLOC_LIBS),)
  TCMALLOC_LIBS := $(shell pkg-config --libs tcmalloc 2>/dev/null)
  TCMALLOC_CFLAGS := $(shell pkg-config --cflags tcmalloc 2>/dev/null)
endif

BENCH_OPS ?= 200000
BENCH_TIMEOUT ?= 1200
BENCH_THREADS ?= 1,2,4,8,16
BENCH_WORKLOADS ?= rl_small,rl_medium,fragmentation_mix,alignment64

BENCH_BINARIES := $(BUILD_DIR)/bench_system $(BUILD_DIR)/bench_mempool_baseline $(BUILD_DIR)/bench_mempool_sharded
ifneq ($(JEMALLOC_LIBS),)
  BENCH_BINARIES += $(BUILD_DIR)/bench_jemalloc
endif
ifneq ($(TCMALLOC_LIBS),)
  BENCH_BINARIES += $(BUILD_DIR)/bench_tcmalloc
endif
empty :=
space := $(empty) $(empty)
comma := ,
BENCH_BINS_CSV := $(subst $(space),$(comma),$(BENCH_BINARIES))

all: bench

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/bench_system: $(BENCH_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DALLOCATOR_SYSTEM -o $@ $<

$(BUILD_DIR)/bench_mempool_baseline: $(BENCH_SRC) $(MEMPOOL_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DALLOCATOR_MEMPOOL -DMEMPOOL_SHARDED_SEGREGATED=0 -DMEMPOOL_VARIANT_LABEL=\"mempool_baseline\" -o $@ $^

$(BUILD_DIR)/bench_mempool_sharded: $(BENCH_SRC) $(MEMPOOL_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DALLOCATOR_MEMPOOL -DMEMPOOL_SHARDED_SEGREGATED=1 -DMEMPOOL_VARIANT_LABEL=\"mempool_sharded\" -o $@ $^

$(BUILD_DIR)/bench_jemalloc: $(BENCH_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(JEMALLOC_CFLAGS) -DALLOCATOR_JEMALLOC -o $@ $< $(JEMALLOC_LIBS)

$(BUILD_DIR)/bench_tcmalloc: $(BENCH_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TCMALLOC_CFLAGS) -DALLOCATOR_TCMALLOC -o $@ $< $(TCMALLOC_LIBS)

bench: $(BENCH_BINARIES)
	python3 scripts/run_bench.py --bins $(BENCH_BINS_CSV) --threads $(BENCH_THREADS) --ops $(BENCH_OPS) --workloads $(BENCH_WORKLOADS) --timeout $(BENCH_TIMEOUT)
	python3 scripts/plot.py --csv bench/results/bench.csv --out bench/plots

bench_resume: $(BENCH_BINARIES)
	python3 scripts/run_bench.py --bins $(BENCH_BINS_CSV) --threads $(BENCH_THREADS) --ops $(BENCH_OPS) --workloads $(BENCH_WORKLOADS) --resume --timeout $(BENCH_TIMEOUT)
	python3 scripts/plot.py --csv bench/results/bench.csv --out bench/plots

bench_smoke: $(BENCH_BINARIES)
	python3 scripts/run_bench.py --bins $(BENCH_BINS_CSV) --threads 1,2 --ops 20000 --workloads rl_small,rl_medium
	python3 scripts/plot.py --csv bench/results/bench.csv --out bench/plots

bench-fast: bench_smoke

clean:
	rm -rf $(BUILD_DIR) bench/results/bench.csv bench/results/bench_meta.txt bench/plots/*.png

.PHONY: all bench bench_resume bench_smoke bench-fast clean
