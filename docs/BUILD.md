# Building NEXRAD Radar Level II Processor

### Prerequisites
- **Compiler**: C++17 compatible compiler (GCC 8+, Clang 7+)
- **Build System**: CMake 3.15 or higher
- **OS**: Linux (tested on Ubuntu/Debian)

### Dependencies
You must install the following libraries before building:

1. **CURL**: For AWS SDK and HTTP communication.
2. **BZip2**: For decompressing NEXRAD Level II message chunks.
3. **ZLIB**: For compressing output bitmasks.
4. **AWS SDK for C++**: Required components: `s3`, `core`.
5. **nlohmann_json**: Header-only JSON library.

#### Ubuntu/Debian Installation Example:

    sudo apt-get update
    sudo apt-get install build-essential cmake libcurl4-openssl-dev libbz2-dev zlib1g-dev nlohmann-json3-dev

---

## Building AWS SDK for C++

The AWS SDK for C++ must be built from source with the required components (`s3`, `core`).

### 1. Clone the AWS SDK for C++

    git clone https://github.com/aws/aws-sdk-cpp.git
    cd aws-sdk-cpp

### 2. Configure the build
Only build the components you need to keep build time reasonable:

    mkdir build && cd build
    cmake .. \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_ONLY="s3;core" \
      -DBUILD_SHARED_LIBS=ON \
      -DENABLE_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr/local

### 3. Build and install

    make -j$(nproc)
    sudo make install

This installs:
- Headers to: `/usr/local/include/aws`
- Libraries to: `/usr/local/lib`

### 4. Verify installation

    ls /usr/local/include/aws

You should see directories such as:

    core  s3

### Notes
- Requires `libcurl`, `openssl`, `zlib`, and `libbz2` (already covered in dependencies).
- If CMake cannot find the AWS SDK, you may need:

    export CMAKE_PREFIX_PATH=/usr/local

- **NOTICE** Build time may be slow depending on your cpu

---

### Build Instructions

1. **Clone the repository**:

    git clone <https://github.com/JoshuaNewport/nexrad-levelii-pipeline>
    cd nexrad-levelii-pipeline

2. **Configure and Build**:

    mkdir build && cd build
    cmake ..
    make -j$(nproc)

### Build Options
- **Testing**: To build unit tests, use the `-DENABLE_TESTING=ON` flag:

    cmake -DENABLE_TESTING=ON ..
    make
    ctest
