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

set -euo pipefail

romeo_load_x64cpu_env
spack load openmpi@4.1.7 %aocc

export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK

# (si déjà compilé)
srun ./build/golomb_mpi
