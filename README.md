# Concurrent Hash Set Implementations

This project was developed as part of a group coursework during the autumn term of my third year at university. The goal was to implement **concurrent hash sets** using different concurrency control techniques, exploring trade-offs in performance and complexity.

## Implementations

1. **Sequential**: A single-threaded implementation with no concurrency.
2. **Coarse-Grained**: Utilizes a global lock for thread safety.
3. **Striped**: Uses finer-grained locks, assigning locks to specific buckets.
4. **Refinable**: Dynamically resizes the hash set while maintaining thread safety.

## Repository Structure

- **`src/`**: Source code for all implementations.

## Technologies Used

- **C++**: Core programming language.

## Getting Started

### Prerequisites

- GCC Compiler for C++.

### Installation

1. Clone the repository:
   ```bash
   git clone https://github.com/MattyWilliams22/concurrent_hash_set.git
   cd concurrent_hash_set
   ```
2. Build the project:
   ```bash
   cd skeleton
   ./scripts/check_clean_build.sh
   ```
