# Debug模式
# cmake_minimum_required(VERSION 3.15)

# project(cpp-matrix-benchmark)

# set(CMAKE_CXX_STANDARD 11)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(CMAKE_BUILD_TYPE Debug)

# # 设置编译和链接选项
# # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g -O1 -fno-omit-frame-pointer")
# # set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")

# aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src SRC_LIST)

# add_executable(
#     app
#     tests/test8.cpp
#     ${SRC_LIST}
# )

# target_include_directories(
#     app
#     PRIVATE
#     ${CMAKE_CURRENT_SOURCE_DIR}/include
# )

# set_target_properties(
#     app
#     PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/output
# )


# Test模式
cmake_minimum_required(VERSION 3.15)
project(thread_pool)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(GTest REQUIRED) # 使用GoogleTest
aux_source_directory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    SRC_LIST
)
add_executable(
    thread_pool_test
    tests/unit/cancel_test.cpp
    ${SRC_LIST}
)
target_include_directories(
    thread_pool_test
    PRIVATE
    include
)
target_link_libraries(
    thread_pool_test
    GTest::gtest_main
    pthread
)
enable_testing()
include(GoogleTest)
gtest_discover_tests(
    thread_pool_test
)