# Makefile for Optimal Golomb Ruler
# Supports: OpenMP (g++) and MPI+OpenMP (mpicxx)
#
# Usage:
#   make openmp          # Build OpenMP version (PROD mode)
#   make mpi             # Build MPI version (PROD mode)
#   make openmp-dev      # Build OpenMP version (DEV mode - reduced sizes)
#   make mpi-dev         # Build MPI version (DEV mode - reduced sizes)

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
SRCS_OPENMP = $(SRC_DIR)/search.cpp $(SRC_DIR)/main_openmp.cpp
SRCS_MPI    = $(SRC_DIR)/search_mpi.cpp $(SRC_DIR)/main_mpi.cpp

# Objects
OBJS_OPENMP = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS_OPENMP))
OBJS_MPI    = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/mpi_%.o,$(SRCS_MPI))

# Targets
TARGET_OPENMP = $(BUILD_DIR)/golomb_openmp
TARGET_MPI    = $(BUILD_DIR)/golomb_mpi

# Default target
all: openmp

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# OpenMP target
openmp: $(BUILD_DIR) $(TARGET_OPENMP)

$(TARGET_OPENMP): $(OBJS_OPENMP)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# MPI target
mpi: $(BUILD_DIR) $(TARGET_MPI)

$(TARGET_MPI): $(OBJS_MPI)
	$(MPICXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/mpi_%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(MPICXX) $(CXXFLAGS) -c -o $@ $<

# Clean
clean:
	rm -rf $(BUILD_DIR)

# ========== DEV mode targets (reduced sizes for Windows testing) ==========
TARGET_OPENMP_DEV = $(BUILD_DIR)/golomb_openmp_dev
TARGET_MPI_DEV    = $(BUILD_DIR)/golomb_mpi_dev

OBJS_OPENMP_DEV = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev_%.o,$(SRCS_OPENMP))
OBJS_MPI_DEV    = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/dev_mpi_%.o,$(SRCS_MPI))

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

.PHONY: all openmp mpi openmp-dev mpi-dev clean run run-dev run_mpi_2 run_mpi_4 run_mpi_8 run_mpi_dev_2
