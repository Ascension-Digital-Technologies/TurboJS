cmake_minimum_required(VERSION 3.21)

project(TurboJS VERSION 1.0.0 LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(TurboJSBuildPolicy)
include(TurboJSTargets)
include(TurboJSTests)
include(TurboJSGenerators)
include(TurboJSBenchmarks)
include(TurboJSInstall)
