#pragma once

#include "../core/config.h"
#include "../core/module.h"
#include "../../third_party/imgui/imgui.h"
#include <vector>

class RadialRenderer
{
public:
    void OpenAt(float centerX, float centerY, const std::vector<RadialSector>& sectors);
    void Close();
    void ApplyRawDelta(int dx, int dy, float scale);
    void SetAbsoluteCursor(float xInOverlay, float yInOverlay, bool showVirtualCursor);
    void HandleWheel(int delta, std::vector<RadialSector>& sectors);
    void ExecuteHovered(std::vector<RadialSector>& sectors);

    bool Update(float dt, const std::vector<RadialSector>& sectors);
    void Render(
        const AppConfig& cfg,
        const std::vector<RadialSector>& sectors,
        const std::string& centerTime,
        float masterVolume,
        bool masterMuted);

    bool IsAnimating() const { return openAnim_ > 0.004f || targetOpen_; }
    bool IsOpen() const { return targetOpen_; }
    bool DidWheelAdjust() const { return wheelAdjusted_; }

private:
    void SyncSubItems(const std::vector<RadialSector>& sectors);
    void DrawVectorIcon(ImDrawList* dl, IconKind kind, ImVec2 c, float size, ImU32 col, bool muted);
    static void AddAnnularSector(
        ImDrawList* dl,
        ImVec2 center,
        float rIn,
        float rOut,
        float a0,
        float a1,
        ImU32 col,
        int segments = 40);
    static void AddArcStroke(
        ImDrawList* dl,
        ImVec2 center,
        float r,
        float a0,
        float a1,
        ImU32 col,
        float thickness,
        int segments = 40);

    bool targetOpen_ = false;
    float openAnim_ = 0.0f;
    float centerX_ = 0.0f;
    float centerY_ = 0.0f;
    float cursorOffsetX_ = 0.0f;
    float cursorOffsetY_ = 0.0f;
    float smoothedAngle_ = 0.0f;
    bool hasPointerAngle_ = false;
    bool showVirtualCursor_ = false;
    int hoveredSector_ = -1;
    int activeSubOwner_ = -1;
    int hoveredSubItem_ = -1;
    bool wheelAdjusted_ = false;
    float subRingAnim_ = 0.0f;
    float displayMasterVol_ = 0.5f;
    std::vector<float> sectorHover_;
    std::vector<float> subHover_;
    std::vector<RadialSubItem> subItems_;
};
