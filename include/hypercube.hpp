#pragma once

#include <mpi.h>
#include <cmath>

// =============================================================================
// HYPERCUBE MPI TOPOLOGY - Classic Parallel Communication Algorithms
// =============================================================================
// Implements standard hypercube communication patterns:
// - All-reduce minimum in O(log P) steps
// - Broadcast from root in O(log P) steps
// - Non-blocking exchanges for overlapping computation/communication
// =============================================================================

class HypercubeMPI {
private:
    int rank_;
    int size_;
    int dimensions_;

public:
    HypercubeMPI() {
        MPI_Comm_rank(MPI_COMM_WORLD, &rank_);
        MPI_Comm_size(MPI_COMM_WORLD, &size_);

        // Handle size=1 case (no hypercube needed)
        if (size_ == 1) {
            dimensions_ = 0;
            return;
        }

        dimensions_ = static_cast<int>(std::log2(size_));

        if ((1 << dimensions_) != size_) {
            if (rank_ == 0) {
                fprintf(stderr, "ERREUR: Le nombre de processus MPI doit etre une puissance de 2\n");
            }
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    inline int rank() const { return rank_; }
    inline int size() const { return size_; }
    inline int dimensions() const { return dimensions_; }

    // Get neighbor in dimension d: rank XOR 2^d
    inline int neighbor(int dimension) const {
        return rank_ ^ (1 << dimension);
    }

    // =========================================================================
    // ALL-REDUCE MINIMUM - Classic hypercube algorithm
    // =========================================================================
    // Each process exchanges with neighbor in each dimension, keeping minimum.
    // After log2(P) rounds, all processes have the global minimum.
    // Complexity: O(log P) communication rounds
    // =========================================================================
    int allReduceMin(int localMin) {
        if (size_ == 1) return localMin;

        int result = localMin;

        for (int d = 0; d < dimensions_; ++d) {
            int partner = neighbor(d);
            int recvVal;

            MPI_Sendrecv(&result, 1, MPI_INT, partner, d,
                        &recvVal, 1, MPI_INT, partner, d,
                        MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (recvVal < result) {
                result = recvVal;
            }
        }

        return result;
    }

    // =========================================================================
    // BROADCAST - Classic hypercube algorithm from root=0
    // =========================================================================
    // Dimension-order broadcast: root sends to neighbors in decreasing
    // dimension order. Each receiver becomes sender for lower dimensions.
    // Complexity: O(log P) communication rounds
    // =========================================================================
    void broadcast(int& value, int root = 0) {
        if (size_ == 1) return;

        // For root=0, use standard hypercube broadcast pattern
        // Process p receives from p XOR 2^d where d is highest set bit
        // Then sends to neighbors in dimensions < d

        for (int d = dimensions_ - 1; d >= 0; --d) {
            int mask = (1 << (d + 1)) - 1;  // 2^(d+1) - 1
            int partner = neighbor(d);

            if ((rank_ & mask) == (root & mask)) {
                // This process already has the value, send to partner
                if ((rank_ ^ (1 << d)) != rank_) {  // Partner exists
                    MPI_Send(&value, 1, MPI_INT, partner, d, MPI_COMM_WORLD);
                }
            } else if ((rank_ & mask) == ((root ^ (1 << d)) & mask)) {
                // This process receives from partner
                MPI_Recv(&value, 1, MPI_INT, partner, d, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }
    }

    // =========================================================================
    // NON-BLOCKING EXCHANGE for overlapping computation/communication
    // =========================================================================
    struct AsyncMinOp {
        MPI_Request requests[2];
        int sendBuf;
        int recvBuf;
        int partner;
        bool active;
    };

    void asyncExchangeMin(int dimension, int localMin, AsyncMinOp& op) {
        op.partner = neighbor(dimension);
        op.sendBuf = localMin;
        op.active = true;

        MPI_Isend(&op.sendBuf, 1, MPI_INT, op.partner, dimension,
                  MPI_COMM_WORLD, &op.requests[0]);
        MPI_Irecv(&op.recvBuf, 1, MPI_INT, op.partner, dimension,
                  MPI_COMM_WORLD, &op.requests[1]);
    }

    int completeAsyncMin(AsyncMinOp& op, int currentMin) {
        if (!op.active) return currentMin;

        MPI_Waitall(2, op.requests, MPI_STATUSES_IGNORE);
        op.active = false;

        return (op.recvBuf < currentMin) ? op.recvBuf : currentMin;
    }

    // =========================================================================
    // Test if async operation is complete (non-blocking check)
    // =========================================================================
    bool testAsyncComplete(AsyncMinOp& op) {
        if (!op.active) return true;

        int flag;
        MPI_Testall(2, op.requests, &flag, MPI_STATUSES_IGNORE);
        return flag != 0;
    }
};
