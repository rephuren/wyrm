add_executable(wyrm_tests main_test.cpp test_mutator.cpp test_corpus.cpp test_minimizer.cpp test_coverage.cpp test_fuzzer_e2e.cpp test_persistent.cpp test_parallel.cpp test_dedup.cpp)
target_link_libraries(wyrm_tests PRIVATE wyrm_core)
target_compile_options(wyrm_tests PRIVATE -Wall -Wextra)
add_test(NAME wyrm_tests COMMAND wyrm_tests)

# Зависимости порядка: e2e-тест фаззера запускает собранные цели как подпроцессы.
add_dependencies(wyrm_tests target_parse_header target_vuln_buffer target_json_lite target_stateful)
