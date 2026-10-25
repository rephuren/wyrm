CXX ?= clang++
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -Iinclude
# -O0: на этом Apple clang (21.0.0, arm64) -O1 не всегда расставляет redzone вокруг стековых
# массивов (похоже на взаимодействие с инлайнингом/SROA) — переполнение буфера на стеке
# тихо проходит мимо ASan. Проверено отдельно минимальным репро: тот же memcpy-оверфлоу ловится
# на -O0 и пропускается на -O1 на этой машине. Для нашей архитектуры «процесс на запуск» это
# не стоит пропускной способности: узкое место всё равно fork+exec, а не скорость самой цели.
# -fno-sanitize-recover=all: без этого UBSan по умолчанию восстанавливается после ошибки
# (печатает диагностику и продолжает выполнение) — div-by-zero не аварийно завершает процесс.
SAN_FLAGS := -O0 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fsanitize-coverage=trace-pc-guard

BUILD := build
CORE_SRCS := src/corpus.cpp src/mutator.cpp src/coverage.cpp src/executor.cpp src/minimizer.cpp src/fuzzer.cpp
CORE_OBJS := $(CORE_SRCS:src/%.cpp=$(BUILD)/%.o)

.PHONY: all clean test targets

all: $(BUILD)/wyrm targets

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/wyrm: $(CORE_OBJS) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Среда выполнения покрытия для целей: без санитайзеров/покрытия поверх себя, иначе рекурсия.
$(BUILD)/coverage_rt.o: src/coverage_rt.cpp | $(BUILD)
	$(CXX) -std=c++17 -O2 -g -Iinclude -c $< -o $@

targets: $(BUILD)/target_parse_header $(BUILD)/target_vuln_buffer $(BUILD)/target_json_lite

$(BUILD)/target_%: targets/%.cpp $(BUILD)/coverage_rt.o
	$(CXX) -std=c++17 $(SAN_FLAGS) $< $(BUILD)/coverage_rt.o -o $@

TEST_SRCS := tests/main_test.cpp tests/test_mutator.cpp tests/test_corpus.cpp tests/test_minimizer.cpp tests/test_coverage.cpp tests/test_fuzzer_e2e.cpp

$(BUILD)/wyrm_tests: $(TEST_SRCS) $(CORE_OBJS) | targets
	$(CXX) $(CXXFLAGS) -Itests $(TEST_SRCS) $(CORE_OBJS) -o $@

test: $(BUILD)/wyrm_tests
	cd $(BUILD) && ./wyrm_tests

clean:
	rm -rf $(BUILD)
