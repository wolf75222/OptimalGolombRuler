#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=arm
#SBATCH --time=0-02:00:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=80
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --job-name=golomb_arm_v1v2v3v4v5
#SBATCH --output=job_arm.%J.out
#SBATCH --error=job_arm.%J.err

# =============================================================================
# Golomb Ruler OpenMP Comparison: V1 vs V2 vs V3 vs V4 vs V5 - ARM VERSION
# =============================================================================
# Compare les 5 versions OpenMP sur processeurs ARM:
#   V1: Original (iterative + loop unrolling)
#   V2: Bitset shift (recursive)
#   V3: Hybrid (iterative + bitset shift)
#   V4: Prefix-based + iterative + bitset shift
#   V5: uint64_t ops + prefix-based
#
# Architecture cible: romeo ARM nodes
#   - constraint=arm force les noeuds ARM
#   - Probablement Ampere Altra ou similaire
#
# Tests:
#   Threads: 8, 16, 32, 40, 80
#   Golomb n: 12, 13
# =============================================================================

set -euo pipefail

romeo_load_armgpu_env

echo "=========================================="
echo "Golomb Ruler OpenMP Comparison: V1-V5 (ARM)"
echo "=========================================="
echo "PWD at start: $(pwd)"
echo "SLURM_SUBMIT_DIR: ${SLURM_SUBMIT_DIR}"
echo "NodeList: ${SLURM_NODELIST}"
echo "Total CPUs: ${SLURM_CPUS_PER_TASK}"
echo "Exclusive: YES"
echo "Date: $(date)"

# Afficher info CPU pour vérifier l'architecture
echo ""
echo "CPU Info:"
lscpu | grep -E "^(Architecture|Model name|CPU\(s\)|Socket|Core|Thread|NUMA)" || true

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
echo "Building all OpenMP versions (ARM)"
echo "=========================================="

make clean 2>/dev/null || true

# ARM native flags
ARCH_FLAGS="-march=native -mtune=native"
echo "Using ARCH_FLAGS: $ARCH_FLAGS"

# Build all versions avec flags natifs
for ver in openmp openmp_v2 openmp_v3 openmp_v4 openmp_v5; do
    echo "Building $ver..."
    make $ver CXX=g++ OPTFLAGS="-O3 $ARCH_FLAGS -funroll-loops -fomit-frame-pointer -flto"
done

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
# Adjust thread counts for ARM (typically fewer cores than x86)
THREADS=(8 16 32 40 80)
GOLOMB_N=(12 13)
BINDINGS=(close spread)

export OMP_PLACES=cores
export OMP_STACKSIZE=16M

# Output CSV file
CSV_FILE="results_arm_v1v2v3v4v5_comparison.csv"
echo "threads,n,version,binding,time_s,length,states,states_per_sec" > "$CSV_FILE"

# =============================================================================
# HELPER FUNCTION
# =============================================================================
parse_output() {
    local output="$1"
    local field="$2"
    echo "$output" | grep -E "^${field}\s*:" | sed -E "s#^${field}\s*:\s*##" | awk '{print $1}'
}

run_bench() {
    local binary="$1"
    local n="$2"
    local threads="$3"

    OMP_NUM_THREADS=$threads srun \
        --cpu-bind=cores \
        --distribution=block:block \
        "$binary" "$n" 2>&1
}

# =============================================================================
# RUN BENCHMARKS
# =============================================================================
echo ""
echo "=========================================="
echo "Starting ARM benchmarks..."
echo "=========================================="
echo ""

printf "%-8s %-4s %-8s %-8s %-12s %-8s %-15s %-15s\n" "Threads" "n" "Version" "Binding" "Time(s)" "Length" "States" "States/sec"
echo "--------------------------------------------------------------------------------------------"

for bind in "${BINDINGS[@]}"; do
    export OMP_PROC_BIND=$bind

    for t in "${THREADS[@]}"; do
        for n in "${GOLOMB_N[@]}"; do

            for ver in V1 V2 V3 V4 V5; do
                case $ver in
                    V1) binary="./build/golomb_openmp" ;;
                    V2) binary="./build/golomb_openmp_v2" ;;
                    V3) binary="./build/golomb_openmp_v3" ;;
                    V4) binary="./build/golomb_openmp_v4" ;;
                    V5) binary="./build/golomb_openmp_v5" ;;
                esac

                output=$(run_bench "$binary" "$n" "$t")
                time_val=$(parse_output "$output" "Time")
                len_val=$(parse_output "$output" "Length")
                states_val=$(parse_output "$output" "States")
                sps_val=$(parse_output "$output" "States/sec")

                printf "%-8s %-4s %-8s %-8s %-12s %-8s %-15s %-15s\n" \
                    "$t" "$n" "$ver" "$bind" "$time_val" "$len_val" "$states_val" "$sps_val"
                echo "$t,$n,$ver,$bind,$time_val,$len_val,$states_val,$sps_val" >> "$CSV_FILE"
            done
            echo ""
        done
    done
done

# =============================================================================
# SUMMARY
# =============================================================================
echo ""
echo "=========================================="
echo "SUMMARY - V5 vs others (ARM, close binding)"
echo "=========================================="
echo ""

printf "%-8s %-4s %-10s %-10s %-10s %-10s %-10s %-10s\n" \
    "Threads" "n" "V1(s)" "V2(s)" "V3(s)" "V4(s)" "V5(s)" "V5/V1"
echo "--------------------------------------------------------------------------"

for t in "${THREADS[@]}"; do
    for n in "${GOLOMB_N[@]}"; do
        v1_time=$(grep "^$t,$n,V1,close," "$CSV_FILE" | cut -d',' -f5)
        v2_time=$(grep "^$t,$n,V2,close," "$CSV_FILE" | cut -d',' -f5)
        v3_time=$(grep "^$t,$n,V3,close," "$CSV_FILE" | cut -d',' -f5)
        v4_time=$(grep "^$t,$n,V4,close," "$CSV_FILE" | cut -d',' -f5)
        v5_time=$(grep "^$t,$n,V5,close," "$CSV_FILE" | cut -d',' -f5)

        if [[ -n "$v1_time" && -n "$v5_time" && "$v5_time" != "0" ]]; then
            speedup_v1=$(echo "scale=2; $v1_time / $v5_time" | bc 2>/dev/null || echo "N/A")
            printf "%-8s %-4s %-10s %-10s %-10s %-10s %-10s %-10s\n" \
                "$t" "$n" "$v1_time" "$v2_time" "$v3_time" "$v4_time" "$v5_time" "${speedup_v1}x"
        fi
    done
done

# =============================================================================
# ARM vs x86 NOTE
# =============================================================================
echo ""
echo "=========================================="
echo "NOTE: Compare with x86 results"
echo "=========================================="
echo "ARM processors may have different characteristics:"
echo "  - Different cache sizes"
echo "  - Different branch prediction"
echo "  - Different SIMD capabilities (NEON vs AVX2)"
echo "Compare results_arm_*.csv with results_v1v2v3v4v5_comparison.csv"
echo ""

echo "=========================================="
echo "Results saved to: $CSV_FILE"
echo "=========================================="

cp "$CSV_FILE" "$SLURM_SUBMIT_DIR/"

echo ""
echo "=========================================="
echo "All ARM benchmarks completed!"
echo "=========================================="
echo "End time: $(date)"
