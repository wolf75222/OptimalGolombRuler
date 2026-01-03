# Makefile for Optimal Golomb Ruler
# Supports: OpenMP (g++) and MPI+OpenMP (mpicxx)

# Directories
SRC_DIR     = src
INC_DIR     = include
BUILD_DIR   = build

# Compilers
CXX         = g++
MPICXX      = mpicxx

# Flags
CXXFLAGS    = -std=c++20 -O3 -fopenmp -I$(INC_DIR) -Wall -Wextra -DNDEBUG
LDFLAGS     = -fopenmp

# Sources
SRCS_COMMON = $(SRC_DIR)/golomb.cpp
SRCS_OPENMP = $(SRCS_COMMON) $(SRC_DIR)/search.cpp $(SRC_DIR)/main_openmp.cpp
SRCS_MPI    = $(SRCS_COMMON) $(SRC_DIR)/search_mpi.cpp $(SRC_DIR)/main_mpi.cpp

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

# Run targets
run: $(TARGET_OPENMP)
	./$(TARGET_OPENMP)

run_mpi_2: $(TARGET_MPI)
	mpiexec -n 2 ./$(TARGET_MPI)

run_mpi_4: $(TARGET_MPI)
	mpiexec -n 4 ./$(TARGET_MPI)

run_mpi_8: $(TARGET_MPI)
	mpiexec -n 8 ./$(TARGET_MPI)

.PHONY: all openmp mpi clean run run_mpi_2 run_mpi_4 run_mpi_8
