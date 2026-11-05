#pragma once
#include "BrxDocument.h"
#include <array>
#include <string>
#include <vector>

struct PathZoneRouteResult
{
    bool success = false, startSnapped = false, endSnapped = false;
    std::array<float, 3> resolvedStart{}, resolvedEnd{};
    std::vector<std::array<float, 3>> points;
    std::string message;
};

struct PathZoneClipResult
{
    bool valid = false;
    bool clipped = false;
    float fraction = 0.0f;
    std::array<float, 3> point{};
    std::string message;
};

void RebuildPathZoneAdjacency(BrxPathZoneRecord &zone);
void RebuildPathZoneEdges(BrxPathZoneRecord &zone);
std::size_t FixPathZoneTriangleWinding(BrxPathZoneRecord &zone);
[[nodiscard]] bool BuildPathZoneFromPolygon(BrxPathZoneRecord &zone, const std::vector<std::array<float, 3>> &polygon,
                                            std::string &error);
[[nodiscard]] bool RebuildPathZoneBsp(BrxPathZoneRecord &zone);
[[nodiscard]] bool DeletePathZoneVertex(BrxPathZoneRecord &zone, std::size_t vertexIndex);
[[nodiscard]] bool SplitPathZoneBoundaryEdge(BrxPathZoneRecord &zone, std::size_t edgeIndex);
[[nodiscard]] bool ProjectPathZoneVertexToCollision(BrxPathZoneRecord &zone, std::size_t vertexIndex,
                                                    const BrxDocument &document);
[[nodiscard]] std::vector<std::string> AuditPathZone(const BrxPathZoneRecord &zone);
[[nodiscard]] PathZoneRouteResult FindPathZoneRoute(const BrxPathZoneRecord &zone, const std::array<float, 3> &start,
                                                    const std::array<float, 3> &end);
[[nodiscard]] PathZoneClipResult ClipPathZoneMovement(const BrxPathZoneRecord &zone, const std::array<float, 3> &start,
                                                      const std::array<float, 3> &end);
