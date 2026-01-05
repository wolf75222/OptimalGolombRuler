#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=x64cpu
#SBATCH --time=0-01:00:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=32G
#SBATCH --job-name=golomb_n12
#SBATCH --output=job.%J.out
#SBATCH --error=job.%J.err

# =============================================================================
# Golomb Ruler n=12 Benchmark - Sequential, OpenMP, MPI+OpenMP
# =============================================================================
# Ce script teste la recherche de la règle de Golomb optimale pour n=12
# sur un nœud x64cpu (192 cores = 2 sockets × 96 cores)
#
# Tests effectués :
#   1. Sequential        - 1 core, baseline
#   2. OpenMP            - 192 cores, pure shared memory
#   3. MPI+OpenMP hybrid - différentes configurations
#
# Configurations MPI+OpenMP testées :
#   MPI=2   × OMP=96   (1 proc/socket)
#   MPI=4   × OMP=48
#   MPI=8   × OMP=24
#   MPI=16  × OMP=12
#   MPI=32  × OMP=6
#   MPI=64  × OMP=3
#   MPI=128 × OMP=1    (pure MPI)
# =============================================================================

set -euo pipefail

romeo_load_x64cpu_env
spack load openmpi@4.1.7 %aocc

echo "=========================================="
echo "Golomb Ruler n=12 Benchmark"
echo "=========================================="
echo "PWD at start: $(pwd)"
echo "SLURM_SUBMIT_DIR: ${SLURM_SUBMIT_DIR}"
echo "NodeList: ${SLURM_NODELIST}"
echo "Total CPUs: ${SLURM_CPUS_PER_TASK}"
echo "Date: $(date)"

# Travail dans un répertoire scratch dédié
WORK=/scratch_p/$USER/$SLURM_JOBID
mkdir -p "$WORK/OptimalGolombRuler"

# Copier les sources depuis le répertoire de soumission
cp -r "$SLURM_SUBMIT_DIR"/* "$WORK/OptimalGolombRuler/"
cd "$WORK/OptimalGolombRuler"

echo "Building in: $(pwd)"

# =============================================================================
# BUILD ALL VERSIONS
# =============================================================================
echo ""
echo "=========================================="
echo "Building all versions (PROD mode)"
echo "=========================================="

make clean

# Build Sequential
echo "Building Sequential..."
make sequential CXX=g++

# Build OpenMP
echo "Building OpenMP..."
make openmp CXX=g++

# Build MPI+OpenMP
echo "Building MPI+OpenMP..."
make mpi CXX=mpicxx CC=mpicc

echo ""
echo "Contents of build/:"
ls -lah build || true

# Vérifier les binaires
for bin in golomb_sequential golomb_openmp golomb_mpi; do
    if [[ ! -x ./build/$bin ]]; then
        echo "ERROR: ./build/$bin not found or not executable" >&2
        exit 1
    fi
done

echo "All binaries built successfully!"

# =============================================================================
# 1. SEQUENTIAL BENCHMARK (n=12)
# =============================================================================
echo ""
echo "=========================================="
echo "1. SEQUENTIAL BENCHMARK (n=12)"
echo "=========================================="
echo "Running on 1 core..."

export OMP_NUM_THREADS=1

time ./build/golomb_sequential

echo "-------------------------------------------"

# =============================================================================
# 2. OPENMP BENCHMARK (n=12)
# =============================================================================
echo ""
echo "=========================================="
echo "2. OPENMP BENCHMARK (n=12, 192 threads)"
echo "=========================================="

export OMP_NUM_THREADS=192
export OMP_PROC_BIND=spread
export OMP_PLACES=cores
export OMP_STACKSIZE=8M

time ./build/golomb_openmp

echo "-------------------------------------------"

# =============================================================================
# 3. MPI+OPENMP HYBRID BENCHMARK (n=12)
# =============================================================================
echo ""
echo "=========================================="
echo "3. MPI+OPENMP HYBRID BENCHMARK (n=12)"
echo "=========================================="

# Configurations à tester : (MPI_PROCS, OMP_THREADS)
# Note: Master-worker model - rank 0 distribue, ranks 1+ calculent
# Donc MPI=2 signifie 1 worker, MPI=4 signifie 3 workers, etc.
declare -a CONFIGS=(
    "2 96"
    "4 48"
    "8 24"
    "16 12"
    "32 6"
    "64 3"
    "128 1"
)

for config in "${CONFIGS[@]}"; do
    read -r MPI_PROCS OMP_THREADS <<< "$config"

    echo ""
    echo ">>> Configuration: MPI=$MPI_PROCS × OMP=$OMP_THREADS (Total=$((MPI_PROCS * OMP_THREADS)) cores)"
    echo "-------------------------------------------"

    export OMP_NUM_THREADS=$OMP_THREADS
    export OMP_PROC_BIND=spread
    export OMP_PLACES=cores
    export OMP_STACKSIZE=8M

    time srun --ntasks=$MPI_PROCS --cpus-per-task=$OMP_THREADS ./build/golomb_mpi

    echo "-------------------------------------------"
done

echo ""
echo "=========================================="
echo "All benchmarks completed!"
echo "=========================================="
echo "End time: $(date)"
