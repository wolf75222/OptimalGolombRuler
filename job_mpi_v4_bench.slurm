#!/bin/bash
#SBATCH --job-name=golomb_v4
#SBATCH --output=job_mpi_v4.%j.out
#SBATCH --error=job_mpi_v4.%j.err
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=12
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --partition=short

# Load environment
source /etc/profile.d/lmod.sh 2>/dev/null || true
module purge 2>/dev/null || true

if command -v spack &> /dev/null; then
    spack load gcc
    spack load openmpi
fi

echo "=========================================="
echo "Golomb Ruler MPI V3 vs V4 Comparison"
echo "=========================================="
echo "Node: $(hostname)"
echo "Date: $(date)"
echo ""

cd $SLURM_SUBMIT_DIR

# Build V3 and V4
echo "Building MPI V3 and V4..."
make clean 2>/dev/null
make mpi_v3 mpi_v4

if [ ! -f build/golomb_mpi_v3 ] || [ ! -f build/golomb_mpi_v4 ]; then
    echo "ERROR: Build failed!"
    exit 1
fi

echo ""
echo "=========================================="
echo "Config: 8 MPI x 12 threads = 96 cores"
echo "=========================================="
echo ""

export OMP_NUM_THREADS=12

# Results file
RESULTS="results/results_v3_v4.csv"
mkdir -p results
echo "n,version,time_s,length,states" > $RESULTS

for n in 12 13 14; do
    echo "--- n = $n ---"

    # V3
    echo "Running V3..."
    OUT3=$(mpirun -np 8 ./build/golomb_mpi_v3 $n 2>&1)
    T3=$(echo "$OUT3" | grep "Time" | awk '{print $3}')
    L3=$(echo "$OUT3" | grep "Length" | awk '{print $3}')
    S3=$(echo "$OUT3" | grep "States   " | awk '{print $3}')
    echo "V3: Time=${T3}s Length=${L3}"
    echo "$n,V3,$T3,$L3,$S3" >> $RESULTS

    # V4
    echo "Running V4..."
    OUT4=$(mpirun -np 8 ./build/golomb_mpi_v4 $n 2>&1)
    T4=$(echo "$OUT4" | grep "Time" | awk '{print $3}')
    L4=$(echo "$OUT4" | grep "Length" | awk '{print $3}')
    S4=$(echo "$OUT4" | grep "States   " | awk '{print $3}')
    echo "V4: Time=${T4}s Length=${L4}"
    echo "$n,V4,$T4,$L4,$S4" >> $RESULTS

    # Speedup
    if [ -n "$T3" ] && [ -n "$T4" ]; then
        SPEEDUP=$(echo "scale=2; $T3 / $T4" | bc 2>/dev/null || python3 -c "print(f'{$T3/$T4:.2f}')" 2>/dev/null || echo "?")
        echo "Speedup V4/V3: ${SPEEDUP}x"
    fi
    echo ""
done

echo "=========================================="
echo "Results saved to: $RESULTS"
echo "Completed: $(date)"
echo "=========================================="
