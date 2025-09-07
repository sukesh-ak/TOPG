# Build stage
FROM ubuntu:24.04 AS builder

# Avoid interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    curl \
    zip \
    unzip \
    tar \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files (excluding vcpkg)
COPY . .

# Clone and bootstrap vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git && \
    cd vcpkg && \
    ./bootstrap-vcpkg.sh

# Build the application
RUN cmake . -B build -DCMAKE_TOOLCHAIN_FILE="vcpkg/scripts/buildsystems/vcpkg.cmake"
RUN cmake --build build/ --config Release

# Runtime stage
FROM ubuntu:24.04

# Install only runtime dependencies (nvidia-smi)
RUN apt-get update && apt-get install -y \
    libc6 \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -m -s /bin/bash topg

# Set working directory
WORKDIR /app

# Copy only the built executable
COPY --from=builder /app/build/topg .

# Change ownership to non-root user
RUN chown topg:topg /app/topg

# Switch to non-root user
USER topg

# Expose the default port
EXPOSE 8080

# Run the server
CMD ["./topg"]