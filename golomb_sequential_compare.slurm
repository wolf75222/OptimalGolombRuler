#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=x64cpu
#SBATCH --time=0-04:00:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --job-name=golomb_seq_v1v2
#SBATCH --output=job_seq.%J.out
#SBATCH --error=job_seq.%J.err

# =============================================================================
# Golomb Ruler Sequential Comparison: V1 vs V2
# =============================================================================
# Compare les 2 versions séquentielles :
#   V1: Original (explicit marks array, O(k) collision detection)
#   V2: BitSet128 shift-based (O(1) collision detection, ~4x faster)
#
# Tests: n = 9, 10, 11, 12, 13
# =============================================================================

set -euo pipefail

romeo_load_x64cpu_env

echo "=========================================="
echo "Golomb Ruler Sequential Comparison: V1 vs V2"
echo "=========================================="
echo "NodeList: ${SLURM_NODELIST}"
echo "Date: $(date)"

# Afficher info CPU
echo ""
echo "CPU Info:"
lscpu | grep -E "^(Model name|CPU MHz)" || true

# Travail dans un repertoire scratch dedie
WORK=/scratch_p/$USER/$SLURM_JOBID
mkdir -p "$WORK/OptimalGolombRuler"

# Copier les sources depuis le repertoire de soumission
cp -r "$SLURM_SUBMIT_DIR"/* "$WORK/OptimalGolombRuler/"
cd "$WORK/OptimalGolombRuler"

echo "Building in: $(pwd)"

# =============================================================================
# BUILD SEQUENTIAL VERSIONS
# =============================================================================
echo ""
echo "=========================================="
echo "Building Sequential versions"
echo "=========================================="

make clean 2>/dev/null || true

ARCH_FLAGS="-march=native -mtune=native"
echo "Using ARCH_FLAGS: $ARCH_FLAGS"

# Build sequential V1
echo "Building sequential V1..."
make sequential CXX=g++ OPTFLAGS="-O3 $ARCH_FLAGS -funroll-loops -fomit-frame-pointer -flto"

# Build sequential V2
echo "Building sequential V2..."
make sequential_v2 CXX=g++ OPTFLAGS="-O3 $ARCH_FLAGS -funroll-loops -fomit-frame-pointer -flto"

echo ""
echo "Contents of build/:"
ls -lah build/golomb_sequential* || true

# Verifier les binaires
for bin in golomb_sequential golomb_sequential_v2; do
    if [[ ! -x ./build/$bin ]]; then
        echo "ERROR: ./build/$bin not found or not executable" >&2
        exit 1
    fi
done

echo "All binaries built successfully!"

# =============================================================================
# BENCHMARK CONFIGURATION
# =============================================================================
GOLOMB_N=(9 10 11 12 13)

# Output CSV file
CSV_FILE="results_sequential_v1v2_comparison.csv"
echo "n,version,time_s,length,states,states_per_sec" > "$CSV_FILE"

# =============================================================================
# HELPER FUNCTION - Robust output parsing
# =============================================================================
parse_output() {
    local output="$1"
    local field="$2"
    echo "$output" | grep -E "^${field}\s*:" | sed -E "s#^${field}\s*:\s*##" | awk '{print $1}'
}

# =============================================================================
# RUN BENCHMARKS
# =============================================================================
echo ""
echo "=========================================="
echo "Starting benchmarks..."
echo "=========================================="
echo ""

printf "%-4s %-8s %-12s %-8s %-15s %-15s\n" "n" "Version" "Time(s)" "Length" "States" "States/sec"
echo "--------------------------------------------------------------------"

for n in "${GOLOMB_N[@]}"; do
    # Run V1
    echo "Running V1 for n=$n..."
    output_v1=$(./build/golomb_sequential "$n" 2>&1)
    time_v1=$(parse_output "$output_v1" "Time")
    len_v1=$(parse_output "$output_v1" "Length")
    states_v1=$(parse_output "$output_v1" "States")
    sps_v1=$(parse_output "$output_v1" "States/sec")

    printf "%-4s %-8s %-12s %-8s %-15s %-15s\n" "$n" "V1" "$time_v1" "$len_v1" "$states_v1" "$sps_v1"
    echo "$n,V1,$time_v1,$len_v1,$states_v1,$sps_v1" >> "$CSV_FILE"

    # Run V2
    echo "Running V2 for n=$n..."
    output_v2=$(./build/golomb_sequential_v2 "$n" 2>&1)
    time_v2=$(parse_output "$output_v2" "Time")
    len_v2=$(parse_output "$output_v2" "Length")
    states_v2=$(parse_output "$output_v2" "States")
    sps_v2=$(parse_output "$output_v2" "States/sec")

    printf "%-4s %-8s %-12s %-8s %-15s %-15s\n" "$n" "V2" "$time_v2" "$len_v2" "$states_v2" "$sps_v2"
    echo "$n,V2,$time_v2,$len_v2,$states_v2,$sps_v2" >> "$CSV_FILE"

    echo ""
done

# =============================================================================
# SUMMARY
# =============================================================================
echo ""
echo "=========================================="
echo "SUMMARY - V1 vs V2 Speedup"
echo "=========================================="
echo ""

printf "%-4s %-12s %-12s %-10s %-12s %-12s\n" "n" "V1(s)" "V2(s)" "Speedup" "V1 St/s" "V2 St/s"
echo "----------------------------------------------------------------------"

for n in "${GOLOMB_N[@]}"; do
    v1_time=$(grep "^$n,V1," "$CSV_FILE" | cut -d',' -f3)
    v2_time=$(grep "^$n,V2," "$CSV_FILE" | cut -d',' -f3)
    v1_sps=$(grep "^$n,V1," "$CSV_FILE" | cut -d',' -f6)
    v2_sps=$(grep "^$n,V2," "$CSV_FILE" | cut -d',' -f6)

    if [[ -n "$v1_time" && -n "$v2_time" && "$v2_time" != "0" ]]; then
        speedup=$(echo "scale=2; $v1_time / $v2_time" | bc 2>/dev/null || echo "N/A")
        printf "%-4s %-12s %-12s %-10s %-12s %-12s\n" "$n" "$v1_time" "$v2_time" "${speedup}x" "$v1_sps" "$v2_sps"
    fi
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
