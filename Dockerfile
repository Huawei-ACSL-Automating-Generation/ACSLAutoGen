FROM ubuntu:24.04

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Define essential environment variables for Conda
ENV CONDA_DIR=/opt/conda
ENV PATH=$CONDA_DIR/bin:$PATH

# -------------------------------------------------------------------
# Stage 1: Install essential system dependencies and build utilities
# -------------------------------------------------------------------
# Added dependencies for Frama-C: opam, graphviz, libcairo2-dev, etc.
RUN apt-get update && apt-get install -y \
    cmake \
    wget \
    curl \
    git \
    bzip2 \
    ca-certificates \
    gnupg \
    python3 \
    python3-pip \
    pkg-config \
    libedit-dev \
    libffi-dev \
    libncurses-dev \
    zlib1g-dev \
    libzstd-dev \
    libtinfo-dev \
    opam \
    graphviz \
    libcairo2-dev \
    libgtk-3-dev \
    libgtksourceview-3.0-dev \
    libgmp-dev \
    && rm -rf /var/lib/apt/lists/*
# Note: libgmp-dev added to system apt because opam usually looks for system headers, 
# not conda headers, unless explicitly configured.

# -------------------------------------------------------------------
# Stage 2: Install Miniconda and Conda-based toolchains
# -------------------------------------------------------------------
RUN wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh -O miniconda.sh && \
    bash miniconda.sh -b -p $CONDA_DIR && \
    rm miniconda.sh && \
    conda update -n base -c defaults conda -y && \
    conda config --add channels conda-forge && \
    conda config --set channel_priority strict && \
    conda install -y \
        gcc=15.1.0 gxx=15.1.0 \
        llvm=19 \
        llvmdev=19 \
        llvm-tools=19 \
        clangdev=19.1.7 \
        clang=19 \
        gmp ppl zstd     

# Configure Conda-provided GCC as the default compiler (Global setting)
ENV CC=$CONDA_DIR/bin/x86_64-conda-linux-gnu-cc
ENV CXX=$CONDA_DIR/bin/x86_64-conda-linux-gnu-c++

# Configure LLVM and Clang CMake package paths
ENV LLVM_DIR=$CONDA_DIR/lib/cmake/llvm
ENV Clang_DIR=$CONDA_DIR/lib/cmake/clang
ENV CMAKE_PREFIX_PATH=$LLVM_DIR:$Clang_DIR:$CMAKE_PREFIX_PATH

# -------------------------------------------------------------------
# Stage 3: Install Z3 Theorem Prover with C++ API support
# -------------------------------------------------------------------
RUN git clone https://github.com/Z3Prover/z3.git /tmp/z3 && \
    cd /tmp/z3 && \
    CXX=$CXX python3 scripts/mk_make.py --prefix=/opt/z3 && \
    cd build && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/z3
    
# Expose Z3 headers and libraries for the build system
ENV Z3_INCLUDE_DIR=/opt/z3/include
ENV Z3_LIBRARY=/opt/z3/lib/libz3.so
ENV LD_LIBRARY_PATH=/opt/z3/lib:$LD_LIBRARY_PATH

# -------------------------------------------------------------------
# Stage 3.5: Install Opam and Frama-C 31.0 (Argon)
# -------------------------------------------------------------------
# 1. Initialize Opam.
# 2. Create a switch (environment) with OCaml compiler.
# 3. Install Frama-C deps.
# Note: We temporarily unset CC/CXX to avoid conflict between Conda GCC and Opam's build system
#       if Opam expects system paths. Or we explicitly trust the system compiler for OCaml.
RUN opam init --disable-sandboxing --shell-setup -y && \
    opam switch create 4.14.1 && \
    eval $(opam env) && \
    opam install -y depext && \
    opam install -y "frama-c=31.0"

# Add Opam environment variables to PATH so frama-c is executable
ENV PATH="/root/.opam/4.14.1/bin:$PATH"

# Ensure that Frama-C can find solvers.
RUN why3 config detect

# -------------------------------------------------------------------
# Stage 4: Build the target project
# -------------------------------------------------------------------
WORKDIR /app
COPY . /app

RUN rm -rf build && \
    mkdir build && \
    cd build && \
    cmake .. \
    -DLLVM_DIR=$LLVM_DIR \
    -DClang_DIR=$Clang_DIR \
    -DCMAKE_PREFIX_PATH=$LLVM_DIR:$Clang_DIR \
    -DGMP_INCLUDE_DIR=$CONDA_DIR/include \
    -DGMP_LIBRARY=$CONDA_DIR/lib/libgmp.so \
    -DGMPXX_INCLUDE_DIR=$CONDA_DIR/include \
    -DGMPXX_LIBRARY=$CONDA_DIR/lib/libgmpxx.so \
    -DPPL_INCLUDE_DIR=$CONDA_DIR/include \
    -DPPL_LIBRARY=$CONDA_DIR/lib/libppl.so \
    -DZ3_LIBRARY=$Z3_LIBRARY \
    -DZ3_INCLUDE_DIR=$Z3_INCLUDE_DIR \
    -DZLIB_LIBRARY=$CONDA_DIR/lib/libz.so \
    -DZLIB_INCLUDE_DIR=$CONDA_DIR/include \
    -Dzstd_LIBRARY=$CONDA_DIR/lib/libzstd.so \
    -Dzstd_INCLUDE_DIR=$CONDA_DIR/include \
    -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# -------------------------------------------------------------------
# Default entrypoint
# -------------------------------------------------------------------
# Ensure opam env is loaded in interactive shell
RUN echo 'eval $(opam env)' >> /root/.bashrc
CMD ["/bin/bash"]
