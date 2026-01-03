#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=x64cpu
#SBATCH --time=0-00:30:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=32G
#SBATCH --job-name=golomb
#SBATCH --output=job.%J.out
#SBATCH --error=job.%J.err

# =============================================================================
# Golomb Ruler MPI+OpenMP Hybrid Benchmark - PROD mode
# =============================================================================
# Ce script teste différentes combinaisons MPI × OpenMP sur un nœud x64cpu
# (192 cores = 2 sockets × 96 cores)
#
# Configurations testées :
#   MPI=1   × OMP=192  (pure OpenMP)
#   MPI=2   × OMP=96   (1 proc/socket)
#   MPI=4   × OMP=48
#   MPI=8   × OMP=24
#   MPI=16  × OMP=12
#   MPI=32  × OMP=6
#   MPI=64  × OMP=3
#   MPI=192 × OMP=1    (pure MPI)
# =============================================================================

set -euo pipefail

romeo_load_x64cpu_env
spack load openmpi@4.1.7 %aocc

echo "=========================================="
echo "Golomb Ruler MPI+OpenMP Hybrid Benchmark"
echo "=========================================="
echo "PWD at start: $(pwd)"
echo "SLURM_SUBMIT_DIR: ${SLURM_SUBMIT_DIR}"
echo "NodeList: ${SLURM_NODELIST}"
echo "Total CPUs: ${SLURM_CPUS_PER_TASK}"

# Travail dans un répertoire scratch dédié
WORK=/scratch_p/$USER/$SLURM_JOBID
mkdir -p "$WORK/OptimalGolombRuler"

# Copier les sources depuis le répertoire de soumission
cp -r "$SLURM_SUBMIT_DIR"/* "$WORK/OptimalGolombRuler/"
cd "$WORK/OptimalGolombRuler"

echo "Building in: $(pwd)"
# Build en mode PROD (sans -DDEV_MODE)
make clean
make mpi CXX=mpicxx CC=mpicc

echo "Contents of build/:"
ls -lah build || true

# Vérifier la présence du binaire
if [[ ! -x ./build/golomb_mpi ]]; then
  echo "ERROR: ./build/golomb_mpi not found or not executable" >&2
  exit 1
fi

# Configurations à tester : (MPI_PROCS, OMP_THREADS)
# Note: Master-worker model - rank 0 distribue, ranks 1+ calculent
# Donc MPI=2 signifie 1 worker, MPI=4 signifie 3 workers, etc.
# Total workers = MPI - 1, chacun avec OMP threads
declare -a CONFIGS=(
    "2 96"
    "4 48"
    "8 24"
    "16 12"
    "32 6"
    "64 3"
    "128 1"
)

echo ""
echo "=========================================="
echo "Starting hybrid benchmark tests"
echo "=========================================="

for config in "${CONFIGS[@]}"; do
    read -r MPI_PROCS OMP_THREADS <<< "$config"

    echo ""
    echo ">>> Configuration: MPI=$MPI_PROCS × OMP=$OMP_THREADS (Total=$((MPI_PROCS * OMP_THREADS)) cores)"
    echo "-------------------------------------------"

    export OMP_NUM_THREADS=$OMP_THREADS
    export OMP_PROC_BIND=close
    export OMP_PLACES=cores

    # Lancer avec srun
    srun --ntasks=$MPI_PROCS --cpus-per-task=$OMP_THREADS ./build/golomb_mpi

    echo "-------------------------------------------"
done

echo ""
echo "=========================================="
echo "All configurations completed!"
echo "=========================================="
