cmake_minimum_required(VERSION 3.20)

project(executor)

aux_source_directory(src SOURCE)

include_directories(include)

include_directories(
    googletest/googletest
    googletest/googletest/include)
add_subdirectory(googletest)


add_executable(a_test ${SOURCE})

target_link_libraries(a_test gtest) #关键部分