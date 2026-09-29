//--------------------------------------------------------------------------------------
// UVAtlas - segmentoverlap.h
//
// Local performance patch: spatial-hash accelerated pairwise 2D segment intersection.
// Replaces the original O(E^2) brute-force loops in IsSelfOverlapping() and
// CIsochartMesh::IsParameterizationOverlapping() with an equivalent near-linear test.
//--------------------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "isochartutil.h"

namespace Isochart
{
    namespace detail
    {
        struct SegmentBox
        {
            uint32_t vid0;
            uint32_t vid1;
            DirectX::XMFLOAT2 p0;
            DirectX::XMFLOAT2 p1;
            float minX;
            float minY;
            float maxX;
            float maxY;
        };

        inline bool SegmentBoxesCanIntersect(const SegmentBox &a, const SegmentBox &b)
        {
            if (a.maxX < b.minX || b.maxX < a.minX || a.maxY < b.minY || b.maxY < a.minY)
            {
                return false;
            }
            if (a.vid0 == b.vid0 || a.vid0 == b.vid1 || a.vid1 == b.vid0 || a.vid1 == b.vid1)
            {
                return false;
            }
            return IsochartIsSegmentsIntersect(a.p0, a.p1, b.p0, b.p1);
        }

        // Tests all unordered pairs (i, j), i < j, of 2D segments.
        //   fetchEdge(i, &vid0, &vid1, &p0, &p1) fills segment i's vertex ids and endpoints.
        //   reportPair(i, j) decides whether an intersecting, non-adjacent pair counts.
        // Returns true on the first pair that intersects and passes reportPair.
        template <typename FetchEdge, typename ReportPair>
        bool AnySegmentsIntersectFast(
            size_t segmentCount,
            FetchEdge fetchEdge,
            ReportPair reportPair)
        {
            if (segmentCount < 2)
            {
                return false;
            }

            std::vector<SegmentBox> boxes(segmentCount);
            float globalMinX = FLT_MAX;
            float globalMinY = FLT_MAX;
            float globalMaxX = -FLT_MAX;
            float globalMaxY = -FLT_MAX;
            double lengthSum = 0.0;
            float maxAbsCoord = 0.0f;
            bool allFinite = true;

            for (size_t i = 0; i < segmentCount; i++)
            {
                SegmentBox &box = boxes[i];
                fetchEdge(i, box.vid0, box.vid1, box.p0, box.p1);

                const float coords[4] = { box.p0.x, box.p0.y, box.p1.x, box.p1.y };
                for (float c : coords)
                {
                    if (!std::isfinite(c))
                    {
                        allFinite = false;
                    }
                    maxAbsCoord = std::max(maxAbsCoord, std::fabs(c));
                }

                box.minX = std::min(box.p0.x, box.p1.x);
                box.maxX = std::max(box.p0.x, box.p1.x);
                box.minY = std::min(box.p0.y, box.p1.y);
                box.maxY = std::max(box.p0.y, box.p1.y);
                globalMinX = std::min(globalMinX, box.minX);
                globalMinY = std::min(globalMinY, box.minY);
                globalMaxX = std::max(globalMaxX, box.maxX);
                globalMaxY = std::max(globalMaxY, box.maxY);
                const double dx = double(box.p1.x) - double(box.p0.x);
                const double dy = double(box.p1.y) - double(box.p0.y);
                lengthSum += std::sqrt(dx * dx + dy * dy);
            }

            const auto bruteForce = [&]() -> bool
            {
                for (size_t i = 0; i + 1 < segmentCount; i++)
                {
                    for (size_t j = i + 1; j < segmentCount; j++)
                    {
                        if (SegmentBoxesCanIntersect(boxes[i], boxes[j]) && reportPair(i, j))
                        {
                            return true;
                        }
                    }
                }
                return false;
            };

            if (!allFinite)
            {
                // Preserve original behavior for pathological input.
                return bruteForce();
            }

            // Expand boxes slightly so the epsilon tolerance used by
            // IsochartIsSegmentsIntersect cannot report a hit for boxes that the
            // grid separated.
            const float expand = std::max(ISOCHART_ZERO_EPS * (2.0f + maxAbsCoord), 1e-30f);
            const float extentX = std::max(globalMaxX - globalMinX, expand);
            const float extentY = std::max(globalMaxY - globalMinY, expand);

            float cell = static_cast<float>(lengthSum / static_cast<double>(segmentCount));
            cell = std::max(cell, expand);
            cell = std::max(cell, std::max(extentX, extentY) / 65536.0f);
            if (!(cell > 0.0f) || !std::isfinite(cell))
            {
                cell = std::max(std::max(extentX, extentY), 1.0f);
            }

            const auto cellIndex = [&](float value, float origin) -> int32_t
            {
                const float scaled = (value - origin) / cell;
                const int64_t index = static_cast<int64_t>(std::floor(scaled));
                return static_cast<int32_t>(std::min<int64_t>(std::max<int64_t>(index, 0), 65535));
            };

            std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
            grid.reserve(segmentCount * 2);
            const size_t maxInsertions = 64 * segmentCount + 4096;
            size_t insertions = 0;
            bool overflowed = false;
            for (size_t i = 0; i < segmentCount; i++)
            {
                const SegmentBox &box = boxes[i];
                const int32_t x0 = cellIndex(box.minX - expand, globalMinX);
                const int32_t x1 = cellIndex(box.maxX + expand, globalMinX);
                const int32_t y0 = cellIndex(box.minY - expand, globalMinY);
                const int32_t y1 = cellIndex(box.maxY + expand, globalMinY);
                for (int32_t y = y0; y <= y1; y++)
                {
                    for (int32_t x = x0; x <= x1; x++)
                    {
                        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) |
                            static_cast<uint64_t>(static_cast<uint32_t>(x));
                        grid[key].push_back(static_cast<uint32_t>(i));
                        if (++insertions > maxInsertions)
                        {
                            overflowed = true;
                        }
                    }
                }
            }

            if (overflowed)
            {
                // Degenerate distribution; keep correctness with the original
                // quadratic scan rather than growing the grid unboundedly.
                return bruteForce();
            }

            std::vector<uint32_t> stamp(segmentCount, 0xffffffffu);
            for (size_t i = 0; i < segmentCount; i++)
            {
                const SegmentBox &box = boxes[i];
                const int32_t x0 = cellIndex(box.minX - expand, globalMinX);
                const int32_t x1 = cellIndex(box.maxX + expand, globalMinX);
                const int32_t y0 = cellIndex(box.minY - expand, globalMinY);
                const int32_t y1 = cellIndex(box.maxY + expand, globalMinY);
                for (int32_t y = y0; y <= y1; y++)
                {
                    for (int32_t x = x0; x <= x1; x++)
                    {
                        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) |
                            static_cast<uint64_t>(static_cast<uint32_t>(x));
                        const auto it = grid.find(key);
                        if (it == grid.end())
                        {
                            continue;
                        }
                        for (uint32_t j : it->second)
                        {
                            if (j <= i || stamp[j] == static_cast<uint32_t>(i))
                            {
                                continue;
                            }
                            stamp[j] = static_cast<uint32_t>(i);
                            if (SegmentBoxesCanIntersect(box, boxes[j]) && reportPair(i, j))
                            {
                                return true;
                            }
                        }
                    }
                }
            }

            return false;
        }
    }
}
