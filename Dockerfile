FROM ubuntu:24.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Define environment variables
ENV CONDA_DIR=/opt/conda
ENV PATH=$CONDA_DIR/bin:$PATH

# -------------------------------------------------------------------
# Stage 1: System Dependencies
# -------------------------------------------------------------------
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    wget \
    curl \
    git \
    bzip2 \
    ca-certificates \
    python3 \
    python3-pip \
    pkg-config \
    libedit-dev \
    libffi-dev \
    libgmp-dev \
    libncurses-dev \
    zlib1g-dev \
    libzstd-dev \
    opam \
    graphviz \
    libcairo2-dev \
    libgtk-3-dev \
    libgtksourceview-3.0-dev \
    libfontconfig1-dev \
    libfreetype6-dev \
    time \
    && rm -rf /var/lib/apt/lists/*

# -------------------------------------------------------------------
# Stage 2: Install Conda Environment (Conda-Forge Only)
# -------------------------------------------------------------------
RUN wget https://repo.anaconda.com/miniconda/Miniconda3-py312_24.11.1-0-Linux-x86_64.sh -O miniconda.sh && \
    bash miniconda.sh -b -p $CONDA_DIR && \
    rm miniconda.sh && \
    conda config --remove channels defaults || true && \
    conda config --add channels conda-forge && \
    conda config --set channel_priority strict && \
    conda install -y \
        gcc=14 \
        gxx=14 \
        llvm=19 \
        llvmdev=19 \
        llvm-tools=19 \
        clangdev=19.1.7 \
        clang=19 \
        make \
        gmp ppl zstd && \
    conda clean -afy

# -------------------------------------------------------------------
# Stage 3: Install Z3 (Source Build)
# -------------------------------------------------------------------
RUN git clone --depth 1 --branch z3-4.15.7 https://github.com/Z3Prover/z3.git /tmp/z3 && \
    cd /tmp/z3 && \
    CC=$CONDA_DIR/bin/x86_64-conda-linux-gnu-cc \
    CXX=$CONDA_DIR/bin/x86_64-conda-linux-gnu-c++ \
    python3 scripts/mk_make.py --prefix=/opt/z3 && \
    cd build && \
    make -j$(nproc) && \
    make install && \
    rm -rf /tmp/z3

ENV Z3_INCLUDE_DIR=/opt/z3/include
ENV Z3_LIBRARY=/opt/z3/lib/libz3.so

# -------------------------------------------------------------------
# Stage 4: Install Opam and Frama-C
# -------------------------------------------------------------------
RUN export PATH="/usr/bin:/bin:/usr/sbin:/sbin" && \
    opam init --disable-sandboxing --shell-setup -y --compiler=4.14.2 && \
    eval $(opam env) && \
    opam install -y "frama-c=31.0" && \
    opam clean -a -c -s --logs

# Add Opam to PATH
ENV PATH="/root/.opam/4.14.2/bin:$PATH"

# Detect solvers
RUN eval $(opam env) && why3 config detect

# -------------------------------------------------------------------
# Stage 5: Configure Environment for Final Project
# -------------------------------------------------------------------
ENV CC=$CONDA_DIR/bin/x86_64-conda-linux-gnu-cc
ENV CXX=$CONDA_DIR/bin/x86_64-conda-linux-gnu-c++
ENV LLVM_DIR=$CONDA_DIR/lib/cmake/llvm
ENV Clang_DIR=$CONDA_DIR/lib/cmake/clang

# -------------------------------------------------------------------
# Stage 6: Build User Project
# -------------------------------------------------------------------
WORKDIR /app
COPY . /app

RUN rm -rf /app/build && \
    cmake -S /app -B /app/build \
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
    -DZLIB_LIBRARY=/usr/lib/x86_64-linux-gnu/libz.so \
    -DZLIB_INCLUDE_DIR=/usr/include \
    -Dzstd_LIBRARY=/usr/lib/x86_64-linux-gnu/libzstd.so \
    -Dzstd_INCLUDE_DIR=/usr/include \
    -DCMAKE_BUILD_TYPE=Release && \
    cmake --build /app/build -j$(nproc)

RUN echo 'eval $(opam env)' >> /root/.bashrc

# -------------------------------------------------------------------
# Stage 7: Prepare openHiTLS 0.2.1 (clone + build) for benchmark-FM2026
# -------------------------------------------------------------------
RUN git clone https://gitcode.com/openHiTLS/openhitls.git /app/benchmark-FM2026/openhitls && \
    git -C /app/benchmark-FM2026/openhitls checkout -f tags/openhitls-0.2.1 && \
    git -C /app/benchmark-FM2026/openhitls submodule update --init --recursive && \
    mkdir -p /app/benchmark-FM2026/openhitls/build && \
    cd /app/benchmark-FM2026/openhitls/build && \
    CC=/usr/bin/cc CXX=/usr/bin/c++ python3 ../configure.py \
      --enable hitls_bsl hitls_crypto hitls_tls hitls_pki hitls_auth \
      --lib_type static \
      --bits=64 \
      --system=linux && \
    CC=/usr/bin/cc CXX=/usr/bin/c++ cmake \
      -DCMAKE_C_COMPILER=/usr/bin/cc \
      -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. && \
    cmake --build . -j$(nproc)

CMD ["/bin/bash"]
