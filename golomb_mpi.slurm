#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=x64cpu
#SBATCH --time=0-00:30:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=32
#SBATCH --cpus-per-task=6
#SBATCH --mem=8G
#SBATCH --job-name=golomb
#SBATCH --output=job.%J.out
#SBATCH --error=job.%J.err

# =============================================================================
# Golomb Ruler MPI+OpenMP Benchmark - PROD mode
# =============================================================================
# Ce script compile et exécute en mode PROD (configuration complète).
# Pour le développement local (Windows), utilisez: scripts\build_mpi.bat dev
# =============================================================================

set -euo pipefail

romeo_load_x64cpu_env
spack load openmpi@4.1.7 %aocc

echo "=========================================="
echo "Golomb Ruler MPI Benchmark - PROD MODE"
echo "=========================================="
echo "PWD at start: $(pwd)"
echo "SLURM_SUBMIT_DIR: ${SLURM_SUBMIT_DIR}"
echo "NodeList: ${SLURM_NODELIST}"
echo "Tasks: ${SLURM_NTASKS}"
echo "CPUs per task: ${SLURM_CPUS_PER_TASK}"

export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

# Travail dans un répertoire scratch dédié
WORK=/scratch_p/$USER/$SLURM_JOBID
mkdir -p "$WORK/OptimalGolombRuler"

# Copier les sources depuis le répertoire de soumission
cp -r "$SLURM_SUBMIT_DIR"/* "$WORK/OptimalGolombRuler/"
cd "$WORK/OptimalGolombRuler"

echo "Building in: $(pwd)"
# Build en mode PROD (sans -DDEV_MODE)
make mpi CXX=mpicxx CC=mpicc

echo "Contents of build/:"
ls -lah build || true

# Vérifier la présence du binaire
if [[ ! -x ./build/golomb_mpi ]]; then
  echo "ERROR: ./build/golomb_mpi not found or not executable" >&2
  exit 1
fi

# Lancer via srun
srun ./build/golomb_mpi
