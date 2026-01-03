#pragma once

#include <mpi.h>
#include <vector>
#include <cmath>

class HypercubeMPI {
private:
    int rank_;
    int size_;
    int dimensions_;

public:
    HypercubeMPI() {
        MPI_Comm_rank(MPI_COMM_WORLD, &rank_);
        MPI_Comm_size(MPI_COMM_WORLD, &size_);
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

    inline int neighbor(int dimension) const {
        return rank_ ^ (1 << dimension);
    }

    int allReduceMin(int localMin) {
        int globalMin = localMin;

        for (int d = 0; d < dimensions_; ++d) {
            int partner = neighbor(d);
            int recvMin;

            MPI_Sendrecv(&localMin, 1, MPI_INT, partner, 0,
                        &recvMin, 1, MPI_INT, partner, 0,
                        MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (recvMin < globalMin) {
                globalMin = recvMin;
            }
        }

        return globalMin;
    }

    void broadcastMin(int& value, int root = 0) {
        if (size_ == 1) return;
        MPI_Bcast(&value, 1, MPI_INT, root, MPI_COMM_WORLD);
    }

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

    void gatherResults(const std::vector<int>& localMarks, int localLength,
                      std::vector<int>& allMarks, std::vector<int>& allLengths) {
        if (rank_ == 0) {
            allLengths.resize(size_);
        }

        MPI_Gather(&localLength, 1, MPI_INT,
                   allLengths.data(), 1, MPI_INT,
                   0, MPI_COMM_WORLD);
    }
};
