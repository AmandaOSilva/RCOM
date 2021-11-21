cmake_minimum_required(VERSION 3.16)
project(trab1 C)

set(CMAKE_C_STANDARD 11)

add_executable(trab1 emissor.c receptor.c
        linkLayer.c
        state_machine.c)
