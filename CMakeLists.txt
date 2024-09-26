cmake_minimum_required(VERSION 3.3.2)
project(netlink)
set (CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE CPP_FILES    "src/*.cpp" )
file(GLOB_RECURSE H_FILES      "include/*.h" )

message( "${CPP_FILES}")
message("${H_FILES}")



add_library(${PROJECT_NAME} STATIC ${CPP_FILES}  ${H_FILES} )
target_include_directories( ${PROJECT_NAME}
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

find_package(GTest REQUIRED)
SET(TEST_EXE netlink_tests )
add_executable( ${TEST_EXE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp
)
# set_target_properties(${TEST_EXE} PROPERTIES LINKER_LANGUAGE CXX)


target_link_libraries(${TEST_EXE} PUBLIC
    GTest::gtest_main
    netlink
)