cmake_minimum_required(VERSION 3.20)
project(hlux C)

include(Grimace)

grimace_add_executable(hlux hlux.c)
target_link_libraries(hlux PRIVATE grimace::commctrl)
