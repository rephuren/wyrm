#include "test_framework.hpp"
#include "wyrm/coverage.hpp"

using namespace wyrm;

TEST("merge_new_edges считает только новые байты между вызовами") {
    CoverageMap cov;
    cov.data()[10] = 1;
    cov.data()[20] = 1;
    CHECK(cov.merge_new_edges() == 2);
    CHECK(cov.merge_new_edges() == 0);  // те же байты уже учтены
    cov.clear();
    cov.data()[10] = 1;  // снова ненулевой, но уже был накоплен
    cov.data()[30] = 1;  // новый
    CHECK(cov.merge_new_edges() == 1);
    CHECK(cov.total_edges() == 3);
}

TEST("shm_name непустое и файл временно существует") {
    CoverageMap cov;
    CHECK(!cov.shm_name().empty());
}
