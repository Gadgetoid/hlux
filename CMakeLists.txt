cmake_minimum_required(VERSION 3.20)
project(hlux C)

include(VeloCE)

velo_add_executable(hlux hlux.c)
target_link_libraries(hlux PRIVATE velo::commctrl)
