cmake_minimum_required(VERSION 3.22 FATAL_ERROR)

if(NOT Legion_SOURCE_DIR)
  find_package(Legion REQUIRED)
endif()

add_legion_executable(instance_names
  SOURCES instance_names.cc
  DEPENDS Legion::legion)

add_legion_test(instance_names
  NAME instance_names
  COMMAND instance_names
  LABELS profiling)
