#pragma once

#include <fstream>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

class BenchmarkLog {
private:
    std::string filename_;
    bool fileExists_;

    static std::string getTimestamp() {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    static std::string getDateStamp() {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        return oss.str();
    }

public:
    BenchmarkLog(const std::string& baseDir, const std::string& type) {
        fs::create_directories(baseDir);
        filename_ = baseDir + "/" + type + "_benchmark.csv";
        fileExists_ = fs::exists(filename_);
    }

    void logOpenMP(int n, int threads, int length, double time,
                   double speedup, double efficiency, long long states,
                   const std::string& changes = "") {
        std::ofstream file(filename_, std::ios::app);

        if (!fileExists_) {
            file << "timestamp,date,n,threads,length,time_s,speedup,efficiency_pct,states,changes\n";
            fileExists_ = true;
        }

        file << getTimestamp() << ","
             << getDateStamp() << ","
             << n << ","
             << threads << ","
             << length << ","
             << std::fixed << std::setprecision(5) << time << ","
             << std::setprecision(2) << speedup << ","
             << std::setprecision(1) << efficiency << ","
             << states << ","
             << "\"" << changes << "\"\n";
    }

    void logMPI(int n, int mpiProcs, int ompThreads, int length, double time,
                double speedup, double efficiency, long long states,
                const std::string& changes = "") {
        std::ofstream file(filename_, std::ios::app);

        if (!fileExists_) {
            file << "timestamp,date,n,mpi_procs,omp_threads,length,time_s,speedup,efficiency_pct,states,changes\n";
            fileExists_ = true;
        }

        file << getTimestamp() << ","
             << getDateStamp() << ","
             << n << ","
             << mpiProcs << ","
             << ompThreads << ","
             << length << ","
             << std::fixed << std::setprecision(5) << time << ","
             << std::setprecision(2) << speedup << ","
             << std::setprecision(1) << efficiency << ","
             << states << ","
             << "\"" << changes << "\"\n";
    }
};
