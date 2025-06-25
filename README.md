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

### 📁 Reproducing the noLoop.in Example

To reproduce the example included in the project (located in `noLoop.in`), simply execute:

```bash
./all.sh
```

This script runs ACSLGen over the test input and demonstrates the end-to-end annotation generation workflow. [Remark: do not use compile.sh, which is only for local test.]

---

### 🧰 Requirements

* Docker (version 20.10 or later)
* Linux host system (recommended)