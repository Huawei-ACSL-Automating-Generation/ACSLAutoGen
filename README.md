# ACSLGen: Installation and Usage Guide

## 📦 Installation

ACSLGen is installed and built via the repository `Dockerfile`.

1. **Clone the repository**:

   ```bash
   git clone <your-repo-url> ACSLAutoGen
   cd ACSLAutoGen
   ```

2. **Build the Docker image from `Dockerfile`**:

   ```bash
   docker build -t acslgen:latest .
   ```

3. **Start an interactive container**:

   ```bash
   docker run --rm -it --workdir /app/benchmark-FM2026 acslgen:latest bash
   ```

After the container starts, you are directly in `/app/benchmark-FM2026`, and ACSLGen is built at `/app/build/src/ACSLG`.

---

### 📁 Reproducing the experiment

This repository snapshot is tailored for the FM2026 submission. To reproduce the full packaged experiment (original benchmark + openHiTLS tests), run:

```bash
bash run_all_tests.sh
```

The script runs both parts and writes unified English results under `benchmark-FM2026/runlogs/full_test_<timestamp>/summary.md`.

---

### 🚀 Running ACSLGen

To invoke the ACSLGen tool on a C source file, run the following command inside the container:

```bash
../build/src/ACSLG -extra-arg=-x -extra-arg=c -p ../build path/to/c-program
```

This performs ACSL annotation inference on the target C program using Clang's frontend.

---

### 📚 Documentation in `docs/`

The `docs/` directory contains project documentation, including:

- `docs/projectArchitecture.md`: a full architecture and data-flow overview, from Clang tooling input and symbolic execution to plugin-based ACSL generation and final rewrite output.
- `docs/loopInvariantPlugins.md`: a detailed guide to `loopInvariantPlugins.cpp`, including PI/PS plugin categories, default groups, each plugin's dependencies/outputs, and how loop post-state information is merged.

---

### 🧰 Requirements

* Docker (version 20.10 or later)
* Linux host system (recommended)
