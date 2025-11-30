# ACSLGen: Installation and Usage Guide

## 📦 Installation

ACSLGen is distributed as a pre-built Docker image. To install and use it:

1. **Obtain the image archive** (e.g., `acslg-env.tar.gz`).

2. **Extract the archive**:

   ```bash
   tar -xzf acslg-env.tar.gz
   ```

3. **Load the image into Docker**:

   ```bash
   docker load -i acslg-env.tar
   ```

The environment is now ready for use. 

---

### 🚀 Running ACSLGen

To invoke the ACSLGen tool on a C source file, run the following command inside the container:

```bash
./build/src/ACSLG -extra-arg=-x -extra-arg=c -p build path/to/c-program
```

This performs ACSL annotation inference on the target C program using Clang's frontend.

---

### 📁 Reproducing the experiment

This repository snapshot is tailored for the FM2026 submission. To reproduce the packaged experiment, run the benchmark driver:

```bash
bash benchmark-FM2026/doAll.sh
```

The script generates ACSL annotations, preprocesses them, runs verification, and collects the successful goals used in the FM2026 results.

---

### 🧰 Requirements

* Docker (version 20.10 or later)
* Linux host system (recommended)
