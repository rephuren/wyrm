cmake_minimum_required(VERSION 3.16)
project(wyrm CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_library(wyrm_core STATIC
  src/corpus.cpp
  src/mutator.cpp
  src/coverage.cpp
  src/executor.cpp
  src/minimizer.cpp
  src/fuzzer.cpp
  src/parallel.cpp
)
target_include_directories(wyrm_core PUBLIC include)
target_compile_options(wyrm_core PRIVATE -Wall -Wextra)

add_executable(wyrm src/main.cpp)
target_link_libraries(wyrm PRIVATE wyrm_core)

# Цели для фаззинга: собираются с ASan + покрытием, фаззер запускает их как отдельные процессы.
# coverage_rt.cpp компилируется БЕЗ санитайзеров/покрытия поверх себя — иначе собственная
# инструментация среды выполнения покрытия бесконечно рекурсирует сама в себя.
add_library(wyrm_coverage_rt OBJECT src/coverage_rt.cpp)
target_include_directories(wyrm_coverage_rt PUBLIC include)

# main() и persistent-цикл для целей: тоже без санитайзеров и покрытия поверх себя.
add_library(wyrm_persistent_rt OBJECT src/persistent_rt.cpp)
target_include_directories(wyrm_persistent_rt PUBLIC include)

function(wyrm_target name src)
  add_executable(${name} ${src} $<TARGET_OBJECTS:wyrm_coverage_rt> $<TARGET_OBJECTS:wyrm_persistent_rt>)
  target_compile_options(${name} PRIVATE -O1 -g -fsanitize=address,undefined -fsanitize-coverage=trace-pc-guard)
  target_link_options(${name} PRIVATE -fsanitize=address,undefined -fsanitize-coverage=trace-pc-guard)
endfunction()

wyrm_target(target_parse_header targets/parse_header.cpp)
wyrm_target(target_vuln_buffer targets/vuln_buffer.cpp)
wyrm_target(target_json_lite targets/json_lite.cpp)
wyrm_target(target_stateful targets/stateful.cpp)

enable_testing()
add_subdirectory(tests)
