#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=x64cpu
#SBATCH --time=0-00:30:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=12
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --job-name=golomb_v3v4
#SBATCH --output=job_v3v4.%J.out
#SBATCH --error=job_v3v4.%J.err

# =============================================================================
# Golomb Ruler MPI V3 vs V4 Quick Comparison
# =============================================================================

set -euo pipefail

romeo_load_x64cpu_env
spack load openmpi@4.1.7 %aocc

echo "=========================================="
echo "Golomb Ruler MPI V3 vs V4"
echo "=========================================="
echo "Node: ${SLURM_NODELIST}"
echo "Date: $(date)"
echo ""

# Build in scratch
WORK=/scratch_p/$USER/$SLURM_JOBID
mkdir -p "$WORK/OptimalGolombRuler"
cp -r "$SLURM_SUBMIT_DIR"/* "$WORK/OptimalGolombRuler/"
cd "$WORK/OptimalGolombRuler"

echo "Building V3 and V4..."
make clean 2>/dev/null || true
make mpi_v3 mpi_v4 CXX=g++ MPICXX=mpicxx OPTFLAGS="-O3 -march=native -mtune=native -funroll-loops -fomit-frame-pointer -flto"

for bin in golomb_mpi_v3 golomb_mpi_v4; do
    [[ -x ./build/$bin ]] || { echo "ERROR: $bin not built"; exit 1; }
done

echo "Build OK!"
echo ""

export OMP_NUM_THREADS=12
export OMP_PLACES=cores
export OMP_PROC_BIND=close

CSV_FILE="results_v3_v4.csv"
echo "n,version,time_s,length,states" > "$CSV_FILE"

parse_output() {
    local output="$1"
    local field="$2"
    echo "$output" | grep -E "^${field}\s*:" | sed -E "s#^${field}\s*:\s*##" | awk '{print $1}'
}

run_bench() {
    local binary="$1"
    local n="$2"
    OMP_NUM_THREADS=12 srun \
        --ntasks=8 \
        --cpus-per-task=$SLURM_CPUS_PER_TASK \
        --cpu-bind=cores \
        --distribution=block:block \
        "$binary" "$n" 2>&1
}

echo "=========================================="
echo "Config: 8 MPI x 12 threads = 96 cores"
echo "=========================================="
echo ""
printf "%-4s %-8s %-12s %-8s %-10s\n" "n" "Version" "Time(s)" "Length" "Speedup"
echo "------------------------------------------------"

for n in 12 13 14; do
    # V3
    OUT3=$(run_bench ./build/golomb_mpi_v3 $n)
    T3=$(parse_output "$OUT3" "Time")
    L3=$(parse_output "$OUT3" "Length")
    S3=$(parse_output "$OUT3" "States")
    echo "$n,V3,$T3,$L3,$S3" >> "$CSV_FILE"
    printf "%-4s %-8s %-12s %-8s %-10s\n" "$n" "V3" "$T3" "$L3" "-"

    # V4
    OUT4=$(run_bench ./build/golomb_mpi_v4 $n)
    T4=$(parse_output "$OUT4" "Time")
    L4=$(parse_output "$OUT4" "Length")
    S4=$(parse_output "$OUT4" "States")
    echo "$n,V4,$T4,$L4,$S4" >> "$CSV_FILE"

    # Speedup
    if [[ -n "$T3" && -n "$T4" && "$T4" != "0" ]]; then
        SPEEDUP=$(python3 -c "print(f'{$T3/$T4:.2f}')" 2>/dev/null || echo "?")
        printf "%-4s %-8s %-12s %-8s %-10s\n" "$n" "V4" "$T4" "$L4" "${SPEEDUP}x"
    else
        printf "%-4s %-8s %-12s %-8s %-10s\n" "$n" "V4" "$T4" "$L4" "?"
    fi
    echo ""
done

cp "$CSV_FILE" "$SLURM_SUBMIT_DIR/"

echo "=========================================="
echo "Results: $CSV_FILE"
echo "Done: $(date)"
echo "=========================================="
