#include "../src/layer_policy.h"
#include <cstdlib>
#include <iostream>

void check(bool value) { if (!value) std::abort(); }

int main() {
    using namespace gxr::layer;
    ImageQueue q;
    check(!q.readable());
    q.acquired(7); q.acquired(2);
    check(!q.readable());
    q.waited(); check(q.readable() && q.readable()->index == 7);
    q.waited(); check(q.readable() && q.readable()->index == 7);
    q.released(); check(q.readable() && q.readable()->index == 2);
    q.released(); check(!q.readable());
    q.acquired(4); q.waited(); q.acquired(5); q.waited();
    check(q.readable()->index == 4); q.released(); check(q.readable()->index == 5);
    q.clear(); check(!q.readable());

    check(rectValid(1536, 0, 1536, 6144, 3072, 6144));
    check(!rectValid(-1, 0, 2, 2, 10, 10));
    check(!rectValid(0, 0, 0, 2, 10, 10));
    check(!rectValid(2147483647, 0, 2147483647, 2, 8192, 8192));
    check(!rectValid(1536, 0, 1537, 6144, 3072, 6144));

    check(dimensionsValid(3072, 6144, 1, 1, 1, 1));
    check(dimensionsValid(8192, 8192, 2, 1, 1, 1));
    check(!dimensionsValid(8193, 6144, 1, 1, 1, 1));
    check(!dimensionsValid(3072, 6144, 1, 4, 1, 1));
    check(!dimensionsValid(3072, 6144, 3, 1, 1, 1));

    check(sameExtent(enlargedExtent(1536, 1536), {2304, 2304}));
    check(sameExtent(enlargedExtent(6000, 4000), {8192, 6000}));
    check(sameExtent(enlargedExtent(0, 0), {0, 0}));
    check(enlargedExtent(10, 10).width >= 10 && enlargedExtent(10, 10).height >= 10);

    // Runtime max per view: preserve aspect, never downscale.
    check(sameExtent(targetExtent(1536, 1536, 3152, 3682), {3152, 3152}));
    check(sameExtent(targetExtent(1536, 6144, 3152, 3682), {1536, 6144}));
    check(sameExtent(targetExtent(1000, 1000, 8192, 8192), {8192, 8192}));
    check(sameExtent(targetExtent(0, 100, 3152, 3682), {0, 0}));
    check(sameExtent(targetExtent(100, 100, 0, 0), {100, 100}));

    const auto full = sourceRectUv(0, 0, 1536, 1536, 1536, 6144);
    check(full.x == 0.0f && full.y > 0.74f && full.y < 0.76f);
    check(full.width == 1.0f && full.height > 0.24f && full.height < 0.26f);
    const auto lower = sourceRectUv(0, 1536, 1536, 1536, 1536, 6144);
    check(lower.x == 0.0f && lower.y > 0.49f && lower.y < 0.51f);
    const auto empty = sourceRectUv(0, 0, 0, 0, 1536, 6144);
    check(empty.width == 0.0f && empty.height == 0.0f);

    std::cout << "PASS target extents, image ownership, rect bounds and UV mapping\n";
    return 0;
}
