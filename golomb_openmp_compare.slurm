#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=x64cpu
#SBATCH --time=0-02:00:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=0
#SBATCH --exclusive
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
# Architecture cible: romeo-c (AMD EPYC, 2 sockets × 96 cores)
#   - constraint=x64cpu force les noeuds romeo-c
#   - 192 CPUs disponibles (2:96:1)
#
# Optimizations:
#   - --exclusive: noeud dedié, pas de mixed
#   - --mem=0: utilise toute la RAM du noeud
#   - srun avec cpu-bind: binding correct via Slurm
#   - Test spread vs close pour OMP_PROC_BIND
#   - NUMA-aware: 2 sockets = 2 domaines NUMA
#
# Tests:
#   Threads: 8, 16, 32, 64, 96, 192
#   Golomb n: 12, 13
# =============================================================================

set -euo pipefail

romeo_load_x64cpu_env

echo "=========================================="
echo "Golomb Ruler OpenMP Comparison: V1-V5"
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
lscpu | grep -E "^(Architecture|Model name|CPU\(s\)|Socket|Core|Thread|NUMA|CPU max MHz)" || true

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
THREADS=(8 16 32 64 96 192)
GOLOMB_N=(12 13)
BINDINGS=(close spread)

export OMP_PLACES=cores
export OMP_STACKSIZE=16M

# Output CSV file
CSV_FILE="results_v1v2v3v4v5_comparison.csv"
echo "threads,n,version,binding,time_s,length,states,states_per_sec" > "$CSV_FILE"

# =============================================================================
# HELPER FUNCTION - Robust output parsing
# =============================================================================
parse_output() {
    local output="$1"
    local field="$2"
    echo "$output" | grep -E "^${field}\s*:" | sed -E "s#^${field}\s*:\s*##" | awk '{print $1}'
}

# Function to run benchmark with proper binding
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
echo "Starting benchmarks..."
echo "=========================================="
echo ""

printf "%-8s %-4s %-8s %-8s %-12s %-8s %-15s %-15s\n" "Threads" "n" "Version" "Binding" "Time(s)" "Length" "States" "States/sec"
echo "--------------------------------------------------------------------------------------------"

for bind in "${BINDINGS[@]}"; do
    export OMP_PROC_BIND=$bind

    for t in "${THREADS[@]}"; do
        for n in "${GOLOMB_N[@]}"; do

            # Run all 5 versions
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
# SUMMARY - Best V5 times per configuration
# =============================================================================
echo ""
echo "=========================================="
echo "SUMMARY - V5 vs others (close binding)"
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
# BINDING COMPARISON
# =============================================================================
echo ""
echo "=========================================="
echo "BINDING COMPARISON - V5 close vs spread"
echo "=========================================="
echo ""

printf "%-8s %-4s %-12s %-12s %-10s %-10s\n" "Threads" "n" "V5-close(s)" "V5-spread(s)" "Winner" "Diff%"
echo "----------------------------------------------------------------------"

for t in "${THREADS[@]}"; do
    for n in "${GOLOMB_N[@]}"; do
        v5_close=$(grep "^$t,$n,V5,close," "$CSV_FILE" | cut -d',' -f5)
        v5_spread=$(grep "^$t,$n,V5,spread," "$CSV_FILE" | cut -d',' -f5)

        if [[ -n "$v5_close" && -n "$v5_spread" && "$v5_close" != "0" && "$v5_spread" != "0" ]]; then
            if (( $(echo "$v5_close < $v5_spread" | bc -l) )); then
                winner="close"
                diff=$(echo "scale=1; 100 * ($v5_spread - $v5_close) / $v5_spread" | bc 2>/dev/null || echo "N/A")
            else
                winner="spread"
                diff=$(echo "scale=1; 100 * ($v5_close - $v5_spread) / $v5_close" | bc 2>/dev/null || echo "N/A")
            fi
            printf "%-8s %-4s %-12s %-12s %-10s %-10s\n" "$t" "$n" "$v5_close" "$v5_spread" "$winner" "${diff}%"
        fi
    done
done

# =============================================================================
# NUMA SCALING ANALYSIS
# =============================================================================
echo ""
echo "=========================================="
echo "NUMA SCALING - V5 (close binding)"
echo "=========================================="
echo "Note: romeo-c has 2 NUMA domains (96 cores each)"
echo ""

printf "%-8s %-4s %-12s %-12s %-12s\n" "Threads" "n" "Time(s)" "States/sec" "Efficiency"
echo "--------------------------------------------------------------"

for n in "${GOLOMB_N[@]}"; do
    # Get baseline (8 threads)
    base_time=$(grep "^8,$n,V5,close," "$CSV_FILE" | cut -d',' -f5)

    for t in "${THREADS[@]}"; do
        v5_time=$(grep "^$t,$n,V5,close," "$CSV_FILE" | cut -d',' -f5)
        v5_sps=$(grep "^$t,$n,V5,close," "$CSV_FILE" | cut -d',' -f8)

        if [[ -n "$v5_time" && -n "$base_time" && "$v5_time" != "0" ]]; then
            ideal_speedup=$(echo "scale=2; $t / 8" | bc)
            actual_speedup=$(echo "scale=2; $base_time / $v5_time" | bc 2>/dev/null || echo "1")
            efficiency=$(echo "scale=1; 100 * $actual_speedup / $ideal_speedup" | bc 2>/dev/null || echo "N/A")
            printf "%-8s %-4s %-12s %-12s %-12s\n" "$t" "$n" "$v5_time" "$v5_sps" "${efficiency}%"
        fi
    done
    echo ""
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
