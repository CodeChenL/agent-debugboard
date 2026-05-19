# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(agent_debugboard)

target_sources(app PRIVATE src/main.c src/debugboard_model.c)
