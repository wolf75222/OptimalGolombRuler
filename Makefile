# Makefile for Optimal Golomb Ruler
# Supports: Sequential, OpenMP (g++) and MPI+OpenMP (mpicxx)
#
# Usage:
#   make sequential      # Build Sequential version (PROD mode)
#   make sequential-dev  # Build Sequential version (DEV mode)
#   make openmp          # Build OpenMP version (PROD mode)
#   make mpi             # Build MPI version (PROD mode)
#   make openmp-dev      # Build OpenMP version (DEV mode - reduced sizes)
#   make mpi-dev         # Build MPI version (DEV mode - reduced sizes)
#   make test            # Run correctness tests
#   make bench           # Run full benchmark

# Directories
SRC_DIR     = src
INC_DIR     = include
BUILD_DIR   = build

# Compilers
CXX         = g++
MPICXX      = mpicxx

# =============================================================================
# PERFORMANCE FLAGS - Maximum optimization
# =============================================================================
# -O3              : Aggressive optimization
# -march=native    : Optimize for current CPU architecture
# -mtune=native    : Tune for current CPU
# -ffast-math      : Fast floating point (not critical here)
# -funroll-loops   : Unroll loops for better ILP
# -flto            : Link-time optimization
# -fno-exceptions  : Disable exceptions (not used)
# -fomit-frame-pointer : Free up a register
# =============================================================================

OPTFLAGS = -O3 -march=native -mtune=native -funroll-loops -fomit-frame-pointer -flto
CXXFLAGS_BASE = -std=c++20 $(OPTFLAGS) -fopenmp -I$(INC_DIR) -Wall -Wextra -DNDEBUG
CXXFLAGS      = $(CXXFLAGS_BASE)
CXXFLAGS_DEV  = $(CXXFLAGS_BASE) -DDEV_MODE
LDFLAGS       = -fopenmp $(OPTFLAGS) -flto

# Sources (golomb.cpp removed - all code is header-only or in search files)
SRCS_SEQ    = $(SRC_DIR)/search_sequential.cpp $(SRC_DIR)/main_sequential.cpp
SRCS_OPENMP = $(SRC_DIR)/search.cpp $(SRC_DIR)/main_openmp.cpp
SRCS_OPENMP_V2 = $(SRC_DIR)/search_v2.cpp $(SRC_DIR)/main_openmp_v2.cpp
SRCS_OPENMP_V3 = $(SRC_DIR)/search_v3.cpp $(SRC_DIR)/main_openmp_v3.cpp
SRCS_OPENMP_V4 = $(SRC_DIR)/search_v4.cpp $(SRC_DIR)/main_openmp_v4.cpp
SRCS_OPENMP_V5 = $(SRC_DIR)/search_v5.cpp $(SRC_DIR)/main_openmp_v5.cpp
SRCS_SEQ_V2 = $(SRC_DIR)/search_sequential_v2.cpp $(SRC_DIR)/main_sequential_v2.cpp
SRCS_SEQ_V3 = $(SRC_DIR)/search_sequential_v3.cpp $(SRC_DIR)/main_sequential_v3.cpp
SRCS_SEQ_V4 = $(SRC_DIR)/search_sequential_v4.cpp $(SRC_DIR)/main_sequential_v4.cpp
SRCS_MPI    = $(SRC_DIR)/search_mpi.cpp $(SRC_DIR)/main_mpi.cpp
SRCS_MPI_V2 = $(SRC_DIR)/search_mpi_v2.cpp $(SRC_DIR)/main_mpi_v2.cpp
SRCS_MPI_V3 = $(SRC_DIR)/search_mpi_v3.cpp $(SRC_DIR)/main_mpi_v3.cpp
SRCS_COMPARE = $(SRC_DIR)/search.cpp $(SRC_DIR)/search_v2.cpp $(SRC_DIR)/search_v3.cpp $(SRC_DIR)/main_benchmark_compare.cpp

# Objects
OBJS_SEQ    = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/seq_%.o,$(SRCS_SEQ))
OBJS_OPENMP = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS_OPENMP))
OBJS_OPENMP_V2 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/v2_%.o,$(SRCS_OPENMP_V2))
OBJS_OPENMP_V3 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/v3_%.o,$(SRCS_OPENMP_V3))
OBJS_OPENMP_V4 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/v4_%.o,$(SRCS_OPENMP_V4))
OBJS_OPENMP_V5 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/v5_%.o,$(SRCS_OPENMP_V5))
OBJS_SEQ_V2 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/seq2_%.o,$(SRCS_SEQ_V2))
OBJS_SEQ_V3 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/seq3_%.o,$(SRCS_SEQ_V3))
OBJS_SEQ_V4 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/seq4_%.o,$(SRCS_SEQ_V4))
OBJS_MPI    = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/mpi_%.o,$(SRCS_MPI))
OBJS_MPI_V2 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/mpi2_%.o,$(SRCS_MPI_V2))
OBJS_MPI_V3 = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/mpi3_%.o,$(SRCS_MPI_V3))
OBJS_COMPARE = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/cmp_%.o,$(SRCS_COMPARE))

# Targets
TARGET_SEQ    = $(BUILD_DIR)/golomb_sequential
TARGET_OPENMP = $(BUILD_DIR)/golomb_openmp
TARGET_OPENMP_V2 = $(BUILD_DIR)/golomb_openmp_v2
TARGET_OPENMP_V3 = $(BUILD_DIR)/golomb_openmp_v3
TARGET_OPENMP_V4 = $(BUILD_DIR)/golomb_openmp_v4
TARGET_OPENMP_V5 = $(BUILD_DIR)/golomb_openmp_v5
TARGET_SEQ_V2 = $(BUILD_DIR)/golomb_sequential_v2
TARGET_SEQ_V3 = $(BUILD_DIR)/golomb_sequential_v3
TARGET_SEQ_V4 = $(BUILD_DIR)/golomb_sequential_v4
TARGET_MPI    = $(BUILD_DIR)/golomb_mpi
TARGET_MPI_V2 = $(BUILD_DIR)/golomb_mpi_v2
TARGET_MPI_V3 = $(BUILD_DIR)/golomb_mpi_v3
TARGET_COMPARE = $(BUILD_DIR)/golomb_compare

# Default target
all: sequential openmp

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# =============================================================================
# SEQUENTIAL TARGET (no OpenMP dependency for pure sequential)
# =============================================================================
# Note: We still use -fopenmp in CXXFLAGS for compatibility,
#       but the sequential code doesn't use any OpenMP constructs
CXXFLAGS_SEQ = -std=c++20 $(OPTFLAGS) -I$(INC_DIR) -Wall -Wextra -DNDEBUG
LDFLAGS_SEQ  = $(OPTFLAGS) -flto

sequential: $(BUILD_DIR) $(TARGET_SEQ)

$(TARGET_SEQ): $(OBJS_SEQ)
	$(CXX) $(LDFLAGS_SEQ) -o $@ $^

$(BUILD_DIR)/seq_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_SEQ) -c -o $@ $<

# OpenMP target (V1 - original)
openmp: $(BUILD_DIR) $(TARGET_OPENMP)

$(TARGET_OPENMP): $(OBJS_OPENMP)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# OpenMP V2 target (bitset shift)
openmp_v2: $(BUILD_DIR) $(TARGET_OPENMP_V2)

$(TARGET_OPENMP_V2): $(OBJS_OPENMP_V2)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/v2_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# OpenMP V3 target (hybrid)
openmp_v3: $(BUILD_DIR) $(TARGET_OPENMP_V3)

$(TARGET_OPENMP_V3): $(OBJS_OPENMP_V3)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/v3_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# OpenMP V4 target (prefix-based)
openmp_v4: $(BUILD_DIR) $(TARGET_OPENMP_V4)

$(TARGET_OPENMP_V4): $(OBJS_OPENMP_V4)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/v4_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# OpenMP V5 target (uint64_t ops)
openmp_v5: $(BUILD_DIR) $(TARGET_OPENMP_V5)

$(TARGET_OPENMP_V5): $(OBJS_OPENMP_V5)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/v5_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Sequential V2 target (BitSet128 shift-based)
sequential_v2: $(BUILD_DIR) $(TARGET_SEQ_V2)

$(TARGET_SEQ_V2): $(OBJS_SEQ_V2)
	$(CXX) $(LDFLAGS_SEQ) -o $@ $^

$(BUILD_DIR)/seq2_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_SEQ) -c -o $@ $<

# Sequential V3 target (SIMD SSE2 optimized)
sequential_v3: $(BUILD_DIR) $(TARGET_SEQ_V3)

$(TARGET_SEQ_V3): $(OBJS_SEQ_V3)
	$(CXX) $(LDFLAGS_SEQ) -msse4.1 -o $@ $^

$(BUILD_DIR)/seq3_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_SEQ) -msse4.1 -c -o $@ $<

# Sequential V4 target (maximum pruning + all optimizations)
sequential_v4: $(BUILD_DIR) $(TARGET_SEQ_V4)

$(TARGET_SEQ_V4): $(OBJS_SEQ_V4)
	$(CXX) $(LDFLAGS_SEQ) -o $@ $^

$(BUILD_DIR)/seq4_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_SEQ) -c -o $@ $<

# MPI target (V1 - original with hypercube)
mpi: $(BUILD_DIR) $(TARGET_MPI)

$(TARGET_MPI): $(OBJS_MPI)
	$(MPICXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/mpi_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(MPICXX) $(CXXFLAGS) -c -o $@ $<

# MPI V2 target (hypercube + BitSet128 shift)
mpi_v2: $(BUILD_DIR) $(TARGET_MPI_V2)

$(TARGET_MPI_V2): $(OBJS_MPI_V2)
	$(MPICXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/mpi2_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(MPICXX) $(CXXFLAGS) -c -o $@ $<

# MPI V3 target (no hypercube, standard MPI_Allreduce + BitSet128)
mpi_v3: $(BUILD_DIR) $(TARGET_MPI_V3)

$(TARGET_MPI_V3): $(OBJS_MPI_V3)
	$(MPICXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/mpi3_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(MPICXX) $(CXXFLAGS) -c -o $@ $<

# Compare V1 vs V2 benchmark target
compare: $(BUILD_DIR) $(TARGET_COMPARE)

$(TARGET_COMPARE): $(OBJS_COMPARE)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/cmp_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Clean
clean:
	rm -rf $(BUILD_DIR)

# ========== DEV mode targets (reduced sizes for quick testing) ==========
TARGET_SEQ_DEV    = $(BUILD_DIR)/golomb_sequential_dev
TARGET_OPENMP_DEV = $(BUILD_DIR)/golomb_openmp_dev
TARGET_MPI_DEV    = $(BUILD_DIR)/golomb_mpi_dev

OBJS_SEQ_DEV    = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev_seq_%.o,$(SRCS_SEQ))
OBJS_OPENMP_DEV = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev_%.o,$(SRCS_OPENMP))
OBJS_MPI_DEV    = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev_mpi_%.o,$(SRCS_MPI))

CXXFLAGS_SEQ_DEV = -std=c++20 $(OPTFLAGS) -I$(INC_DIR) -Wall -Wextra -DNDEBUG -DDEV_MODE

sequential-dev: $(BUILD_DIR) $(TARGET_SEQ_DEV)

$(TARGET_SEQ_DEV): $(OBJS_SEQ_DEV)
	$(CXX) $(LDFLAGS_SEQ) -o $@ $^

$(BUILD_DIR)/dev_seq_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_SEQ_DEV) -c -o $@ $<

openmp-dev: $(BUILD_DIR) $(TARGET_OPENMP_DEV)

$(TARGET_OPENMP_DEV): $(OBJS_OPENMP_DEV)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/dev_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS_DEV) -c -o $@ $<

mpi-dev: $(BUILD_DIR) $(TARGET_MPI_DEV)

$(TARGET_MPI_DEV): $(OBJS_MPI_DEV)
	$(MPICXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/dev_mpi_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(MPICXX) $(CXXFLAGS_DEV) -c -o $@ $<

# Run targets
run: $(TARGET_OPENMP)
	./$(TARGET_OPENMP)

run-dev: $(TARGET_OPENMP_DEV)
	./$(TARGET_OPENMP_DEV)

run_mpi_2: $(TARGET_MPI)
	mpiexec -n 2 ./$(TARGET_MPI)

run_mpi_4: $(TARGET_MPI)
	mpiexec -n 4 ./$(TARGET_MPI)

run_mpi_8: $(TARGET_MPI)
	mpiexec -n 8 ./$(TARGET_MPI)

run_mpi_dev_2: $(TARGET_MPI_DEV)
	mpiexec -n 2 ./$(TARGET_MPI_DEV)

# =============================================================================
# TESTING AND BENCHMARKING
# =============================================================================
test: sequential-dev
	./$(TARGET_SEQ_DEV)

bench: sequential
	./$(TARGET_SEQ)

run-seq: $(TARGET_SEQ)
	./$(TARGET_SEQ)

run-seq-dev: $(TARGET_SEQ_DEV)
	./$(TARGET_SEQ_DEV)

.PHONY: all sequential sequential_v2 sequential_v3 sequential_v4 sequential-dev openmp openmp_v2 openmp_v3 openmp_v4 openmp_v5 \
        mpi mpi_v2 mpi_v3 openmp-dev mpi-dev clean \
        run run-dev run_mpi_2 run_mpi_4 run_mpi_8 run_mpi_dev_2 \
        test bench run-seq run-seq-dev compare run-compare

run-compare: $(TARGET_COMPARE)
	./$(TARGET_COMPARE)
