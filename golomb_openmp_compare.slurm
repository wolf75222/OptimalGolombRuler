#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=x64cpu
#SBATCH --time=0-02:00:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=32G
#SBATCH --job-name=golomb_v1v2v3v4v5
#SBATCH --output=job.%J.out
#SBATCH --error=job.%J.err

# =============================================================================
# Golomb Ruler OpenMP Comparison: V1 vs V2 vs V3 vs V4 vs V5
# =============================================================================
# Compare les 5 versions OpenMP :
#   V1: Original (iterative + loop unrolling)
#   V2: Bitset shift (recursive)
#   V3: Hybrid (iterative + bitset shift)
#   V4: Prefix-based + iterative + bitset shift
#   V5: uint64_t ops + prefix-based (fastest)
#
# Tests:
#   Threads: 8, 16, 32, 64, 96, 192
#   Golomb n: 12, 13
# =============================================================================

set -euo pipefail

romeo_load_x64cpu_env

echo "=========================================="
echo "Golomb Ruler OpenMP Comparison: V1 vs V2 vs V3 vs V4 vs V5"
echo "=========================================="
echo "PWD at start: $(pwd)"
echo "SLURM_SUBMIT_DIR: ${SLURM_SUBMIT_DIR}"
echo "NodeList: ${SLURM_NODELIST}"
echo "Total CPUs: ${SLURM_CPUS_PER_TASK}"
echo "Date: $(date)"

# Travail dans un repertoire scratch dedie
WORK=/scratch_p/$USER/$SLURM_JOBID
mkdir -p "$WORK/OptimalGolombRuler"

# Copier les sources depuis le repertoire de soumission
cp -r "$SLURM_SUBMIT_DIR"/* "$WORK/OptimalGolombRuler/"
cd "$WORK/OptimalGolombRuler"

echo "Building in: $(pwd)"

# =============================================================================
# BUILD ALL VERSIONS
# =============================================================================
echo ""
echo "=========================================="
echo "Building all OpenMP versions"
echo "=========================================="

make clean 2>/dev/null || true

# Build V1 (OpenMP original)
echo "Building V1 (original)..."
make openmp CXX=g++

# Build V2 (bitset shift)
echo "Building V2 (bitset shift)..."
make openmp_v2 CXX=g++

# Build V3 (hybrid)
echo "Building V3 (hybrid)..."
make openmp_v3 CXX=g++

# Build V4 (prefix-based)
echo "Building V4 (prefix-based)..."
make openmp_v4 CXX=g++

# Build V5 (uint64_t ops)
echo "Building V5 (uint64_t ops)..."
make openmp_v5 CXX=g++

echo ""
echo "Contents of build/:"
ls -lah build || true

# Verifier les binaires
for bin in golomb_openmp golomb_openmp_v2 golomb_openmp_v3 golomb_openmp_v4 golomb_openmp_v5; do
    if [[ ! -x ./build/$bin ]]; then
        echo "ERROR: ./build/$bin not found or not executable" >&2
        exit 1
    fi
done

echo "All binaries built successfully!"

# =============================================================================
# BENCHMARK CONFIGURATION
# =============================================================================
THREADS=(8 16 32 64 96 192)
GOLOMB_N=(12 13)

export OMP_PROC_BIND=spread
export OMP_PLACES=cores
export OMP_STACKSIZE=8M

# Output CSV file
CSV_FILE="results_v1v2v3v4v5_comparison.csv"
echo "threads,n,version,time_s,length,states,states_per_sec" > "$CSV_FILE"

# =============================================================================
# RUN BENCHMARKS
# =============================================================================
echo ""
echo "=========================================="
echo "Starting benchmarks..."
echo "=========================================="
echo ""

printf "%-8s %-4s %-8s %-12s %-8s %-15s %-15s\n" "Threads" "n" "Version" "Time(s)" "Length" "States" "States/sec"
echo "--------------------------------------------------------------------------------"

for t in "${THREADS[@]}"; do
    export OMP_NUM_THREADS=$t

    for n in "${GOLOMB_N[@]}"; do
        # V1 - Original
        output=$(./build/golomb_openmp $n 2>&1)
        time_v1=$(echo "$output" | grep "Time" | awk '{print $3}')
        len_v1=$(echo "$output" | grep "Length" | awk '{print $3}')
        states_v1=$(echo "$output" | grep "^States " | awk '{print $3}')
        sps_v1=$(echo "$output" | grep "States/sec" | awk '{print $3}')

        printf "%-8s %-4s %-8s %-12s %-8s %-15s %-15s\n" "$t" "$n" "V1" "$time_v1" "$len_v1" "$states_v1" "$sps_v1"
        echo "$t,$n,V1,$time_v1,$len_v1,$states_v1,$sps_v1" >> "$CSV_FILE"

        # V2 - Bitset shift
        output=$(./build/golomb_openmp_v2 $n 2>&1)
        time_v2=$(echo "$output" | grep "Time" | awk '{print $3}')
        len_v2=$(echo "$output" | grep "Length" | awk '{print $3}')
        states_v2=$(echo "$output" | grep "^States " | awk '{print $3}')
        sps_v2=$(echo "$output" | grep "States/sec" | awk '{print $3}')

        printf "%-8s %-4s %-8s %-12s %-8s %-15s %-15s\n" "$t" "$n" "V2" "$time_v2" "$len_v2" "$states_v2" "$sps_v2"
        echo "$t,$n,V2,$time_v2,$len_v2,$states_v2,$sps_v2" >> "$CSV_FILE"

        # V3 - Hybrid
        output=$(./build/golomb_openmp_v3 $n 2>&1)
        time_v3=$(echo "$output" | grep "Time" | awk '{print $3}')
        len_v3=$(echo "$output" | grep "Length" | awk '{print $3}')
        states_v3=$(echo "$output" | grep "^States " | awk '{print $3}')
        sps_v3=$(echo "$output" | grep "States/sec" | awk '{print $3}')

        printf "%-8s %-4s %-8s %-12s %-8s %-15s %-15s\n" "$t" "$n" "V3" "$time_v3" "$len_v3" "$states_v3" "$sps_v3"
        echo "$t,$n,V3,$time_v3,$len_v3,$states_v3,$sps_v3" >> "$CSV_FILE"

        # V4 - Prefix-based
        output=$(./build/golomb_openmp_v4 $n 2>&1)
        time_v4=$(echo "$output" | grep "Time" | awk '{print $3}')
        len_v4=$(echo "$output" | grep "Length" | awk '{print $3}')
        states_v4=$(echo "$output" | grep "^States " | awk '{print $3}')
        sps_v4=$(echo "$output" | grep "States/sec" | awk '{print $3}')

        printf "%-8s %-4s %-8s %-12s %-8s %-15s %-15s\n" "$t" "$n" "V4" "$time_v4" "$len_v4" "$states_v4" "$sps_v4"
        echo "$t,$n,V4,$time_v4,$len_v4,$states_v4,$sps_v4" >> "$CSV_FILE"

        # V5 - uint64_t ops
        output=$(./build/golomb_openmp_v5 $n 2>&1)
        time_v5=$(echo "$output" | grep "Time" | awk '{print $3}')
        len_v5=$(echo "$output" | grep "Length" | awk '{print $3}')
        states_v5=$(echo "$output" | grep "^States " | awk '{print $3}')
        sps_v5=$(echo "$output" | grep "States/sec" | awk '{print $3}')

        printf "%-8s %-4s %-8s %-12s %-8s %-15s %-15s\n" "$t" "$n" "V5" "$time_v5" "$len_v5" "$states_v5" "$sps_v5"
        echo "$t,$n,V5,$time_v5,$len_v5,$states_v5,$sps_v5" >> "$CSV_FILE"

        echo ""
    done
done

# =============================================================================
# SUMMARY
# =============================================================================
echo ""
echo "=========================================="
echo "SUMMARY - Best times per configuration"
echo "=========================================="
echo ""

printf "%-8s %-4s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n" "Threads" "n" "V1(s)" "V2(s)" "V3(s)" "V4(s)" "V5(s)" "V5/V1" "V5/V4"
echo "------------------------------------------------------------------------------------------------------"

for t in "${THREADS[@]}"; do
    for n in "${GOLOMB_N[@]}"; do
        v1_time=$(grep "^$t,$n,V1," "$CSV_FILE" | cut -d',' -f4)
        v2_time=$(grep "^$t,$n,V2," "$CSV_FILE" | cut -d',' -f4)
        v3_time=$(grep "^$t,$n,V3," "$CSV_FILE" | cut -d',' -f4)
        v4_time=$(grep "^$t,$n,V4," "$CSV_FILE" | cut -d',' -f4)
        v5_time=$(grep "^$t,$n,V5," "$CSV_FILE" | cut -d',' -f4)

        if [[ -n "$v1_time" && -n "$v5_time" ]]; then
            speedup_v1=$(echo "scale=2; $v1_time / $v5_time" | bc)
            speedup_v4=$(echo "scale=2; $v4_time / $v5_time" | bc)
            printf "%-8s %-4s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n" "$t" "$n" "$v1_time" "$v2_time" "$v3_time" "$v4_time" "$v5_time" "${speedup_v1}x" "${speedup_v4}x"
        fi
    done
done

echo ""
echo "=========================================="
echo "Results saved to: $CSV_FILE"
echo "=========================================="

# Copier les resultats vers le repertoire de soumission
cp "$CSV_FILE" "$SLURM_SUBMIT_DIR/"

echo ""
echo "=========================================="
echo "All benchmarks completed!"
echo "=========================================="
echo "End time: $(date)"
