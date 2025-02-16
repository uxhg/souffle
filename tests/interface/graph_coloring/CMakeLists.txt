# Souffle - A Datalog Compiler
# Copyright (c) 2021 The Souffle Developers. All rights reserved
# Licensed under the Universal Permissive License v 1.0 as shown at:
# - https://opensource.org/licenses/UPL
# - <souffle root>/licenses/SOUFFLE-UPL.txt

add_library(gcfunctors SHARED functors.cpp)
target_include_directories(gcfunctors PRIVATE "${CMAKE_SOURCE_DIR}/src/include")

target_compile_features(gcfunctors
                        PUBLIC cxx_std_17)
set_target_properties(gcfunctors PROPERTIES CXX_EXTENSIONS OFF)
set_target_properties(gcfunctors PROPERTIES OUTPUT_NAME "functors")

target_compile_options(gcfunctors
                       PUBLIC "-Wall;-Wextra;-Werror;-fwrapv")

if (SOUFFLE_DOMAIN_64BIT)
    target_compile_definitions(functors
                               PUBLIC RAM_DOMAIN_SIZE=64)
endif()
