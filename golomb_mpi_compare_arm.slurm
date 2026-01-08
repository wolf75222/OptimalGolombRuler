#!/usr/bin/env bash
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --constraint=armgpu
#SBATCH --time=0-04:00:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=24
#SBATCH --mem=0
#SBATCH --exclusive
#SBATCH --job-name=golomb_mpi_arm_v1v2v3
#SBATCH --output=job_mpi_arm.%J.out
#SBATCH --error=job_mpi_arm.%J.err

# =============================================================================
# Golomb Ruler MPI Comparison: V1 vs V2 vs V3 - ARM VERSION
# =============================================================================
# Compare les 3 versions MPI sur ARM:
#   V1: Original (hypercube + loop unrolling)
#   V2: Hypercube + BitSet128 shift optimization
#   V3: No hypercube (MPI_Allreduce) + BitSet128 shift
#
# Architecture cible: romeo ARM nodes
#   - constraint=armgpu force les noeuds ARM
#   - 2 nodes × 192 CPUs
#
# Tests:
#   MPI procs: 1, 2, 4
#   Threads per proc: 8, 16, 32, 64, 96, 192
#   Golomb n: 12, 13, 14
# =============================================================================

set -euo pipefail

romeo_load_armgpu_env
spack load openmpi

echo "=========================================="
echo "Golomb Ruler MPI Comparison: V1-V3 (ARM)"
echo "=========================================="
echo "PWD at start: $(pwd)"
echo "SLURM_SUBMIT_DIR: ${SLURM_SUBMIT_DIR}"
echo "NodeList: ${SLURM_NODELIST}"
echo "Nodes: ${SLURM_NNODES}"
echo "Tasks per node: ${SLURM_NTASKS_PER_NODE}"
echo "CPUs per task: ${SLURM_CPUS_PER_TASK}"
echo "Exclusive: YES"
echo "Date: $(date)"

# Afficher info CPU
echo ""
echo "CPU Info:"
lscpu | grep -E "^(Architecture|Model name|CPU\(s\)|Socket|Core|Thread|NUMA|CPU max MHz)" || true

# Travail dans un repertoire scratch dedie
WORK=/scratch_p/$USER/$SLURM_JOBID
mkdir -p "$WORK/OptimalGolombRuler"

# Copier les sources
cp -r "$SLURM_SUBMIT_DIR"/* "$WORK/OptimalGolombRuler/"
cd "$WORK/OptimalGolombRuler"

echo "Building in: $(pwd)"

# =============================================================================
# BUILD ALL MPI VERSIONS
# =============================================================================
echo ""
echo "=========================================="
echo "Building all MPI versions (ARM)"
echo "=========================================="

make clean 2>/dev/null || true

ARCH_FLAGS="-march=native -mtune=native"
echo "Using ARCH_FLAGS: $ARCH_FLAGS"

# Build all MPI versions
for ver in mpi mpi_v2 mpi_v3; do
    echo "Building $ver..."
    make $ver CXX=g++ MPICXX=mpicxx OPTFLAGS="-O3 $ARCH_FLAGS -funroll-loops -fomit-frame-pointer -flto"
done

echo ""
echo "Contents of build/:"
ls -lah build || true

# Verify binaries
for bin in golomb_mpi golomb_mpi_v2 golomb_mpi_v3; do
    if [[ ! -x ./build/$bin ]]; then
        echo "ERROR: ./build/$bin not found or not executable" >&2
        exit 1
    fi
done

echo "All MPI binaries built successfully!"

# =============================================================================
# BENCHMARK CONFIGURATION
# =============================================================================
# Configurations optimales uniquement (MPI × Threads = total workers)
# On teste les configs les plus pertinentes pour 2 noeuds ARM (192 CPUs/noeud)
#   - 1 MPI × 96t  = 96 workers (baseline single node)
#   - 2 MPI × 96t  = 192 workers (1 proc/node, optimal)
#   - 4 MPI × 48t  = 192 workers (2 proc/node)
#   - 8 MPI × 24t  = 192 workers (4 proc/node)
MPI_CONFIGS=(
    "1 96"
    "2 96"
    "4 48"
    "8 24"
)
GOLOMB_N=(12 13 14)

export OMP_PLACES=cores
export OMP_PROC_BIND=close
export OMP_STACKSIZE=16M

# Output CSV file
CSV_FILE="results_mpi_arm_v1v2v3_comparison.csv"
echo "mpi_procs,threads,total_workers,n,version,time_s,length,states,states_per_sec" > "$CSV_FILE"

# =============================================================================
# HELPER FUNCTIONS
# =============================================================================
parse_output() {
    local output="$1"
    local field="$2"
    echo "$output" | grep -E "^${field}\s*:" | sed -E "s#^${field}\s*:\s*##" | awk '{print $1}'
}

run_mpi_bench() {
    local binary="$1"
    local n="$2"
    local procs="$3"
    local threads="$4"

    OMP_NUM_THREADS=$threads srun \
        --ntasks=$procs \
        --ntasks-per-node=1 \
        --cpus-per-task=$SLURM_CPUS_PER_TASK \
        --cpu-bind=cores \
        --distribution=block:block \
        "$binary" "$n" 2>&1
}

# =============================================================================
# RUN BENCHMARKS
# =============================================================================
echo ""
echo "=========================================="
echo "Starting ARM MPI benchmarks..."
echo "=========================================="
echo ""

printf "%-8s %-8s %-8s %-4s %-8s %-12s %-8s %-15s %-15s\n" \
    "MPI" "Threads" "Total" "n" "Version" "Time(s)" "Length" "States" "States/sec"
echo "----------------------------------------------------------------------------------------------------"

for config in "${MPI_CONFIGS[@]}"; do
    read -r procs threads <<< "$config"
    total_workers=$((procs * threads))

    for n in "${GOLOMB_N[@]}"; do

        for ver in V1 V2 V3; do
            case $ver in
                V1) binary="./build/golomb_mpi" ;;
                V2) binary="./build/golomb_mpi_v2" ;;
                V3) binary="./build/golomb_mpi_v3" ;;
            esac

            output=$(run_mpi_bench "$binary" "$n" "$procs" "$threads")
            time_val=$(parse_output "$output" "Time")
            len_val=$(parse_output "$output" "Length")
            states_val=$(parse_output "$output" "States")
            sps_val=$(parse_output "$output" "States/sec")

            printf "%-8s %-8s %-8s %-4s %-8s %-12s %-8s %-15s %-15s\n" \
                "$procs" "$threads" "$total_workers" "$n" "$ver" "$time_val" "$len_val" "$states_val" "$sps_val"
            echo "$procs,$threads,$total_workers,$n,$ver,$time_val,$len_val,$states_val,$sps_val" >> "$CSV_FILE"
        done
        echo ""
    done
done

# =============================================================================
# SUMMARY - Best times per configuration
# =============================================================================
echo ""
echo "=========================================="
echo "SUMMARY - V2/V3 vs V1 speedup (ARM)"
echo "=========================================="
echo ""

printf "%-8s %-8s %-4s %-12s %-12s %-12s %-10s %-10s\n" \
    "MPI" "Threads" "n" "V1(s)" "V2(s)" "V3(s)" "V2/V1" "V3/V1"
echo "--------------------------------------------------------------------------------"

for config in "${MPI_CONFIGS[@]}"; do
    read -r procs threads <<< "$config"
    for n in "${GOLOMB_N[@]}"; do
        v1_time=$(grep "^$procs,$threads,.*,$n,V1," "$CSV_FILE" | cut -d',' -f6)
        v2_time=$(grep "^$procs,$threads,.*,$n,V2," "$CSV_FILE" | cut -d',' -f6)
        v3_time=$(grep "^$procs,$threads,.*,$n,V3," "$CSV_FILE" | cut -d',' -f6)

        if [[ -n "$v1_time" && -n "$v2_time" && -n "$v3_time" && "$v2_time" != "0" && "$v3_time" != "0" ]]; then
            speedup_v2=$(echo "scale=2; $v1_time / $v2_time" | bc 2>/dev/null || echo "N/A")
            speedup_v3=$(echo "scale=2; $v1_time / $v3_time" | bc 2>/dev/null || echo "N/A")
            printf "%-8s %-8s %-4s %-12s %-12s %-12s %-10s %-10s\n" \
                "$procs" "$threads" "$n" "$v1_time" "$v2_time" "$v3_time" "${speedup_v2}x" "${speedup_v3}x"
        fi
    done
done

# =============================================================================
# MPI SCALING ANALYSIS
# =============================================================================
echo ""
echo "=========================================="
echo "MPI SCALING - V3 (ARM)"
echo "=========================================="
echo ""

printf "%-8s %-8s %-8s %-4s %-12s %-12s %-12s\n" \
    "MPI" "Threads" "Total" "n" "Time(s)" "States/sec" "Efficiency"
echo "------------------------------------------------------------------------"

for n in "${GOLOMB_N[@]}"; do
    # Get baseline (1 MPI, 96 threads)
    base_time=$(grep "^1,96,.*,$n,V3," "$CSV_FILE" | cut -d',' -f6)

    for config in "${MPI_CONFIGS[@]}"; do
        read -r procs threads <<< "$config"
        total_workers=$((procs * threads))
        v3_time=$(grep "^$procs,$threads,.*,$n,V3," "$CSV_FILE" | cut -d',' -f6)
        v3_sps=$(grep "^$procs,$threads,.*,$n,V3," "$CSV_FILE" | cut -d',' -f9)

        if [[ -n "$v3_time" && -n "$base_time" && "$v3_time" != "0" ]]; then
            ideal_speedup=$(echo "scale=2; $total_workers / 96" | bc)
            actual_speedup=$(echo "scale=2; $base_time / $v3_time" | bc 2>/dev/null || echo "1")
            efficiency=$(echo "scale=1; 100 * $actual_speedup / $ideal_speedup" | bc 2>/dev/null || echo "N/A")
            printf "%-8s %-8s %-8s %-4s %-12s %-12s %-12s\n" \
                "$procs" "$threads" "$total_workers" "$n" "$v3_time" "$v3_sps" "${efficiency}%"
        fi
    done
    echo ""
done

# =============================================================================
# HYPERCUBE VS NO-HYPERCUBE COMPARISON
# =============================================================================
echo ""
echo "=========================================="
echo "HYPERCUBE (V2) vs NO-HYPERCUBE (V3) - ARM"
echo "=========================================="
echo ""

printf "%-8s %-8s %-4s %-12s %-12s %-10s\n" \
    "MPI" "Threads" "n" "V2-hyper(s)" "V3-simple(s)" "Winner"
echo "--------------------------------------------------------------"

for config in "${MPI_CONFIGS[@]}"; do
    read -r procs threads <<< "$config"
    for n in "${GOLOMB_N[@]}"; do
        v2_time=$(grep "^$procs,$threads,.*,$n,V2," "$CSV_FILE" | cut -d',' -f6)
        v3_time=$(grep "^$procs,$threads,.*,$n,V3," "$CSV_FILE" | cut -d',' -f6)

        if [[ -n "$v2_time" && -n "$v3_time" && "$v2_time" != "0" && "$v3_time" != "0" ]]; then
            if (( $(echo "$v2_time < $v3_time" | bc -l) )); then
                winner="V2-hyper"
            else
                winner="V3-simple"
            fi
            printf "%-8s %-8s %-4s %-12s %-12s %-10s\n" \
                "$procs" "$threads" "$n" "$v2_time" "$v3_time" "$winner"
        fi
    done
done

echo ""
echo "=========================================="
echo "Results saved to: $CSV_FILE"
echo "=========================================="

# Copier les resultats
cp "$CSV_FILE" "$SLURM_SUBMIT_DIR/"

echo ""
echo "=========================================="
echo "All ARM MPI benchmarks completed!"
echo "=========================================="
echo "End time: $(date)"
