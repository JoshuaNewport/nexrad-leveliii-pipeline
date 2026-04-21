cmake_minimum_required(VERSION 3.15)
project(LevelIIIProcessor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(ENABLE_TESTING "Build test executables" OFF)

# Find Required Packages
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS s3 core)

# ============================================================================
# Level III Libraries
# ============================================================================

# Utils
add_library(LevelIII_Util STATIC
    src/util/ThreadPool.cpp
    src/util/ZlibUtils.cpp
    src/util/FrameStorageManager.cpp
    src/util/BufferPool.cpp
)
target_include_directories(LevelIII_Util PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(LevelIII_Util PRIVATE ZLIB::ZLIB)

# AWS
add_library(LevelIII_AWS STATIC
    src/aws/AWSInitializer.cpp
    src/aws/BackgroundFrameFetcher.cpp
)
target_include_directories(LevelIII_AWS PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${AWSSDK_INCLUDE_DIRS}
)
target_link_libraries(LevelIII_AWS PUBLIC
    LevelIII_Util
    LevelIIIParser
    aws-cpp-sdk-s3
    aws-cpp-sdk-core
)

# Admin
add_library(LevelIII_Admin STATIC
    src/admin/AdminAPI.cpp
    src/admin/AdminServer.cpp
    src/admin/WebServer.cpp
    src/TerminalUI.cpp
)
target_include_directories(LevelIII_Admin PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(LevelIII_Admin PUBLIC
    LevelIII_AWS
)

# Parser
add_library(LevelIIIParser STATIC
    src/LevelIII_Parser.cpp
    src/RdaWriter.cpp
    src/ProductInfo.cpp
    src/PacketHandlers.cpp
    src/leveliii/ProductDatabase.cpp
)

target_include_directories(LevelIIIParser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(LevelIIIParser PRIVATE
    ZLIB::ZLIB
    BZip2::BZip2
)

# ============================================================================
# Executables
# ============================================================================

add_executable(process_level3 src/process_level3.cpp)
target_link_libraries(process_level3 PRIVATE LevelIIIParser LevelIII_Util)

add_executable(level3_pipeline src/level3_pipeline.cpp)
target_link_libraries(level3_pipeline PRIVATE 
    LevelIIIParser 
    LevelIII_Admin
    LevelIII_AWS
    LevelIII_Util
    aws-cpp-sdk-s3
    aws-cpp-sdk-core
    Threads::Threads
)

add_executable(test_cleanup src/util/test_cleanup.cpp)
target_link_libraries(test_cleanup PRIVATE 
    LevelIIIParser 
    LevelIII_Admin
    LevelIII_AWS
    LevelIII_Util
    aws-cpp-sdk-s3
    aws-cpp-sdk-core
    Threads::Threads
)

add_executable(test_storage_path src/util/test_storage_path.cpp)
target_link_libraries(test_storage_path PRIVATE 
    LevelIII_Util 
    LevelIIIParser
    Threads::Threads
)

if(ENABLE_TESTING)
    enable_testing()
    # add_subdirectory(test/code_tests)
endif()
