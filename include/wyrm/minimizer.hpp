#pragma once
#include <functional>

#include "wyrm/types.hpp"

namespace wyrm {

// Убирает из входа всё, что не нужно для воспроизведения краша: делфт-дебаг (удаление блоков
// пополам) плюс побайтовое обнуление на оставшемся. still_crashes запускает цель и возвращает,
// упал ли процесс так же, как на исходном входе.
Bytes minimize(const Bytes& crashing_input, const std::function<bool(const Bytes&)>& still_crashes,
                int max_rounds = 20);

}  // namespace wyrm
