#include "radial_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = kPi * 2.0f;

static float Clamp01(float v)
{
    return (std::max)(0.0f, (std::min)(1.0f, v));
}

static float SmoothStep(float a, float b, float x)
{
    float t = Clamp01((x - a) / (std::max)(b - a, 1e-5f));
    return t * t * (3.0f - 2.0f * t);
}

static float ExpLerp(float cur, float target, float speed, float dt)
{
    float k = 1.0f - std::exp(-speed * dt);
    return cur + (target - cur) * k;
}

static float NormalizeAngle(float a)
{
    while (a < 0.0f) a += kTwoPi;
    while (a >= kTwoPi) a -= kTwoPi;
    return a;
}

static ImU32 ColorWithAlpha(float r, float g, float b, float a)
{
    return IM_COL32(
        static_cast<int>(Clamp01(r) * 255.0f),
        static_cast<int>(Clamp01(g) * 255.0f),
        static_cast<int>(Clamp01(b) * 255.0f),
        static_cast<int>(Clamp01(a) * 255.0f));
}

void RadialRenderer::AddAnnularSector(
    ImDrawList* dl,
    ImVec2 center,
    float rIn,
    float rOut,
    float a0,
    float a1,
    ImU32 col,
    int segments)
{
    if (rOut <= rIn || a1 <= a0)
    {
        return;
    }

    int count = (std::max)(6, segments);
    for (int i = 0; i < count; i++)
    {
        float t0 = static_cast<float>(i) / static_cast<float>(count);
        float t1 = static_cast<float>(i + 1) / static_cast<float>(count);
        float ang0 = a0 + (a1 - a0) * t0;
        float ang1 = a0 + (a1 - a0) * t1;
        float c0 = std::cos(ang0), s0 = std::sin(ang0);
        float c1 = std::cos(ang1), s1 = std::sin(ang1);

        ImVec2 pIn0(center.x + c0 * rIn, center.y + s0 * rIn);
        ImVec2 pOut0(center.x + c0 * rOut, center.y + s0 * rOut);
        ImVec2 pOut1(center.x + c1 * rOut, center.y + s1 * rOut);
        ImVec2 pIn1(center.x + c1 * rIn, center.y + s1 * rIn);
        dl->AddQuadFilled(pIn0, pOut0, pOut1, pIn1, col);
    }
}

void RadialRenderer::AddArcStroke(
    ImDrawList* dl,
    ImVec2 center,
    float r,
    float a0,
    float a1,
    ImU32 col,
    float thickness,
    int segments)
{
    if (a1 <= a0 || r <= 1.0f)
    {
        return;
    }

    dl->PathClear();
    dl->PathArcTo(center, r, a0, a1, (std::max)(8, segments));
    dl->PathStroke(col, ImDrawFlags_None, thickness);
}

void RadialRenderer::OpenAt(float centerX, float centerY, const std::vector<RadialSector>& sectors)
{
    targetOpen_ = true;
    centerX_ = centerX;
    centerY_ = centerY;
    cursorOffsetX_ = 0.0f;
    cursorOffsetY_ = 0.0f;
    hasPointerAngle_ = false;
    hoveredSector_ = -1;
    hoveredSubItem_ = -1;
    activeSubOwner_ = -1;
    wheelAdjusted_ = false;
    showVirtualCursor_ = false;
    subRingAnim_ = 0.0f;
    subItems_.clear();
    sectorHover_.assign(sectors.size(), 0.0f);
    subHover_.clear();
}

void RadialRenderer::Close()
{
    targetOpen_ = false;
}

void RadialRenderer::ApplyRawDelta(int dx, int dy, float scale)
{
    showVirtualCursor_ = true;
    cursorOffsetX_ += static_cast<float>(dx) * scale;
    cursorOffsetY_ += static_cast<float>(dy) * scale;

    float maxClamp = 230.0f;
    float dist = std::sqrt(cursorOffsetX_ * cursorOffsetX_ + cursorOffsetY_ * cursorOffsetY_);
    if (dist > maxClamp)
    {
        cursorOffsetX_ *= maxClamp / dist;
        cursorOffsetY_ *= maxClamp / dist;
    }
}

void RadialRenderer::SetAbsoluteCursor(float xInOverlay, float yInOverlay, bool showVirtualCursor)
{
    showVirtualCursor_ = showVirtualCursor;
    cursorOffsetX_ = xInOverlay - centerX_;
    cursorOffsetY_ = yInOverlay - centerY_;

    float maxClamp = 230.0f;
    float dist = std::sqrt(cursorOffsetX_ * cursorOffsetX_ + cursorOffsetY_ * cursorOffsetY_);
    if (dist > maxClamp)
    {
        cursorOffsetX_ *= maxClamp / dist;
        cursorOffsetY_ *= maxClamp / dist;
    }
}

void RadialRenderer::SyncSubItems(const std::vector<RadialSector>& sectors)
{
    if (activeSubOwner_ < 0 || activeSubOwner_ >= static_cast<int>(sectors.size()))
    {
        subItems_.clear();
        subHover_.clear();
        return;
    }

    const auto& sec = sectors[activeSubOwner_];
    if (!sec.buildSubItems)
    {
        subItems_.clear();
        subHover_.clear();
        return;
    }

    auto fresh = sec.buildSubItems();
    if (fresh.size() != subHover_.size())
    {
        subHover_.assign(fresh.size(), 0.0f);
    }
    subItems_ = std::move(fresh);
}

void RadialRenderer::HandleWheel(int delta, std::vector<RadialSector>& sectors)
{
    wheelAdjusted_ = true;

    if (hoveredSubItem_ >= 0 && hoveredSubItem_ < static_cast<int>(subItems_.size()))
    {
        if (subItems_[hoveredSubItem_].onWheel)
        {
            subItems_[hoveredSubItem_].onWheel(delta);
            SyncSubItems(sectors);
            return;
        }
    }

    if (hoveredSector_ >= 0 && hoveredSector_ < static_cast<int>(sectors.size()))
    {
        if (sectors[hoveredSector_].onWheel)
        {
            sectors[hoveredSector_].onWheel(delta);
            if (hoveredSector_ == activeSubOwner_)
            {
                SyncSubItems(sectors);
            }
            return;
        }
    }

    if (!sectors.empty() && sectors[0].onWheel)
    {
        sectors[0].onWheel(delta);
    }
}

void RadialRenderer::ExecuteHovered(std::vector<RadialSector>& sectors)
{
    if (wheelAdjusted_)
    {
        return;
    }

    if (hoveredSubItem_ >= 0 && hoveredSubItem_ < static_cast<int>(subItems_.size()))
    {
        if (subItems_[hoveredSubItem_].onActivate)
        {
            subItems_[hoveredSubItem_].onActivate();
        }
        return;
    }

    if (hoveredSector_ >= 0 && hoveredSector_ < static_cast<int>(sectors.size()))
    {
        if (sectors[hoveredSector_].onActivate)
        {
            sectors[hoveredSector_].onActivate();
        }
    }
}

bool RadialRenderer::Update(float dt, const std::vector<RadialSector>& sectors)
{
    float clampedDt = (std::min)(0.05f, (std::max)(0.001f, dt));
    float targetOpen = targetOpen_ ? 1.0f : 0.0f;
    float openSpeed = targetOpen_ ? 18.0f : 22.0f;
    openAnim_ = ExpLerp(openAnim_, targetOpen, openSpeed, clampedDt);

    if (sectorHover_.size() != sectors.size())
    {
        sectorHover_.assign(sectors.size(), 0.0f);
    }

    if (targetOpen_ && !sectors.empty())
    {
        float dist = std::sqrt(cursorOffsetX_ * cursorOffsetX_ + cursorOffsetY_ * cursorOffsetY_);
        if (dist > 22.0f)
        {
            float rawAngle = std::atan2(cursorOffsetY_, cursorOffsetX_);
            if (!hasPointerAngle_)
            {
                smoothedAngle_ = rawAngle;
                hasPointerAngle_ = true;
            }
            else
            {
                float diff = rawAngle - smoothedAngle_;
                while (diff > kPi) diff -= kTwoPi;
                while (diff < -kPi) diff += kTwoPi;
                smoothedAngle_ += diff * (1.0f - std::exp(-28.0f * clampedDt));
            }

            int n = static_cast<int>(sectors.size());
            float step = kTwoPi / static_cast<float>(n);
            float startOffset = -kPi * 0.5f - step * 0.5f;
            float rel = NormalizeAngle(rawAngle - startOffset);
            int idx = static_cast<int>(rel / step) % n;

            hoveredSector_ = idx;
            if (sectors[idx].buildSubItems && dist > 92.0f)
            {
                if (activeSubOwner_ != idx)
                {
                    activeSubOwner_ = idx;
                    SyncSubItems(sectors);
                }
            }
            else if (dist < 70.0f)
            {
                hoveredSubItem_ = -1;
            }
        }
        else
        {
            hoveredSector_ = -1;
            hoveredSubItem_ = -1;
        }
    }

    for (size_t i = 0; i < sectorHover_.size(); i++)
    {
        float t = (targetOpen_ && static_cast<int>(i) == hoveredSector_) ? 1.0f : 0.0f;
        sectorHover_[i] = ExpLerp(sectorHover_[i], t, 20.0f, clampedDt);
    }

    bool wantSub = targetOpen_ && activeSubOwner_ >= 0 && !subItems_.empty();
    subRingAnim_ = ExpLerp(subRingAnim_, wantSub ? 1.0f : 0.0f, 16.0f, clampedDt);
    if (!wantSub && subRingAnim_ < 0.02f)
    {
        activeSubOwner_ = -1;
        subItems_.clear();
        subHover_.clear();
    }

    return openAnim_ > 0.004f || targetOpen_;
}

void RadialRenderer::DrawVectorIcon(ImDrawList* dl, IconKind kind, ImVec2 c, float s, ImU32 col, bool muted)
{
    float thick = (std::max)(1.6f, s * 0.14f);
    switch (kind)
    {
    case IconKind::Volume:
    {
        ImVec2 pts[4] = {
            ImVec2(c.x - s * 0.50f, c.y - s * 0.20f),
            ImVec2(c.x - s * 0.22f, c.y - s * 0.20f),
            ImVec2(c.x + s * 0.12f, c.y - s * 0.48f),
            ImVec2(c.x + s * 0.12f, c.y + s * 0.48f),
        };
        ImVec2 pts2[4] = {
            ImVec2(c.x - s * 0.50f, c.y - s * 0.20f),
            ImVec2(c.x + s * 0.12f, c.y + s * 0.48f),
            ImVec2(c.x - s * 0.22f, c.y + s * 0.20f),
            ImVec2(c.x - s * 0.50f, c.y + s * 0.20f),
        };
        dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3], col);
        dl->AddQuadFilled(pts2[0], pts2[1], pts2[2], pts2[3], col);
        if (!muted)
        {
            dl->PathClear();
            dl->PathArcTo(ImVec2(c.x + s * 0.12f, c.y), s * 0.34f, -0.75f, 0.75f, 10);
            dl->PathStroke(col, ImDrawFlags_None, thick);
            dl->PathClear();
            dl->PathArcTo(ImVec2(c.x + s * 0.12f, c.y), s * 0.56f, -0.78f, 0.78f, 12);
            dl->PathStroke(col, ImDrawFlags_None, thick);
        }
        else
        {
            dl->AddLine(ImVec2(c.x + s * 0.24f, c.y - s * 0.22f), ImVec2(c.x + s * 0.56f, c.y + s * 0.22f), col, thick);
            dl->AddLine(ImVec2(c.x + s * 0.56f, c.y - s * 0.22f), ImVec2(c.x + s * 0.24f, c.y + s * 0.22f), col, thick);
        }
        break;
    }
    case IconKind::Mixer:
    {
        float xs[3] = { -s * 0.36f, 0.0f, s * 0.36f };
        float ks[3] = { -0.22f, 0.24f, -0.05f };
        for (int i = 0; i < 3; i++)
        {
            float x = c.x + xs[i];
            dl->AddLine(ImVec2(x, c.y - s * 0.48f), ImVec2(x, c.y + s * 0.48f), col, thick);
            dl->AddCircleFilled(ImVec2(x, c.y + s * ks[i]), s * 0.14f, col);
        }
        break;
    }
    case IconKind::Mic:
    {
        dl->AddRectFilled(
            ImVec2(c.x - s * 0.17f, c.y - s * 0.50f),
            ImVec2(c.x + s * 0.17f, c.y + s * 0.10f),
            col,
            s * 0.17f);
        dl->PathClear();
        dl->PathArcTo(ImVec2(c.x, c.y - s * 0.02f), s * 0.34f, 0.15f, kPi - 0.15f, 14);
        dl->PathStroke(col, ImDrawFlags_None, thick);
        dl->AddLine(ImVec2(c.x, c.y + s * 0.32f), ImVec2(c.x, c.y + s * 0.54f), col, thick);
        dl->AddLine(ImVec2(c.x - s * 0.22f, c.y + s * 0.54f), ImVec2(c.x + s * 0.22f, c.y + s * 0.54f), col, thick);
        if (muted)
        {
            dl->AddLine(ImVec2(c.x - s * 0.42f, c.y + s * 0.42f), ImVec2(c.x + s * 0.42f, c.y - s * 0.42f), IM_COL32(255, 92, 92, 235), thick * 1.25f);
        }
        break;
    }
    case IconKind::Media:
    case IconKind::PlayPause:
    {
        dl->AddTriangleFilled(
            ImVec2(c.x - s * 0.32f, c.y - s * 0.42f),
            ImVec2(c.x - s * 0.32f, c.y + s * 0.42f),
            ImVec2(c.x + s * 0.42f, c.y),
            col);
        break;
    }
    case IconKind::Next:
    {
        dl->AddTriangleFilled(
            ImVec2(c.x - s * 0.34f, c.y - s * 0.34f),
            ImVec2(c.x - s * 0.34f, c.y + s * 0.34f),
            ImVec2(c.x + s * 0.20f, c.y),
            col);
        dl->AddRectFilled(ImVec2(c.x + s * 0.24f, c.y - s * 0.34f), ImVec2(c.x + s * 0.40f, c.y + s * 0.34f), col);
        break;
    }
    case IconKind::Prev:
    {
        dl->AddTriangleFilled(
            ImVec2(c.x + s * 0.34f, c.y - s * 0.34f),
            ImVec2(c.x + s * 0.34f, c.y + s * 0.34f),
            ImVec2(c.x - s * 0.20f, c.y),
            col);
        dl->AddRectFilled(ImVec2(c.x - s * 0.40f, c.y - s * 0.34f), ImVec2(c.x - s * 0.24f, c.y + s * 0.34f), col);
        break;
    }
    default:
    {
        dl->AddRect(
            ImVec2(c.x - s * 0.44f, c.y - s * 0.34f),
            ImVec2(c.x + s * 0.44f, c.y + s * 0.22f),
            col,
            3.0f,
            0,
            thick);
        dl->AddLine(ImVec2(c.x - s * 0.20f, c.y + s * 0.40f), ImVec2(c.x + s * 0.20f, c.y + s * 0.40f), col, thick);
        dl->AddLine(ImVec2(c.x, c.y + s * 0.22f), ImVec2(c.x, c.y + s * 0.40f), col, thick);
        break;
    }
    }
}

void RadialRenderer::Render(
    const AppConfig& cfg,
    const std::vector<RadialSector>& sectors,
    const std::string& centerTime,
    float masterVolume,
    bool masterMuted)
{
    if (openAnim_ <= 0.004f || sectors.empty())
    {
        return;
    }

    displayMasterVol_ = ExpLerp(displayMasterVol_, masterVolume, 18.0f, 0.016f);

    float alpha = SmoothStep(0.0f, 0.70f, openAnim_);
    float ease = 1.0f - std::pow(1.0f - Clamp01(openAnim_), 3.0f);
    float scale = 0.88f + 0.12f * ease;

    float rIn = cfg.innerRadius * scale;
    float rOut = cfg.outerRadius * scale;
    ImVec2 center(centerX_, centerY_);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;

    float ar = cfg.accentColor[0];
    float ag = cfg.accentColor[1];
    float ab = cfg.accentColor[2];

    dl->AddCircle(center, rOut + 5.0f, ColorWithAlpha(0.0f, 0.0f, 0.0f, 0.18f * alpha), 72, 8.0f);
    dl->AddCircleFilled(center, rOut + 1.0f, ColorWithAlpha(0.06f, 0.07f, 0.08f, cfg.bgAlpha * 0.65f * alpha), 72);

    int n = static_cast<int>(sectors.size());
    float step = kTwoPi / static_cast<float>(n);
    float gap = 0.022f;
    float startOffset = -kPi * 0.5f - step * 0.5f;

    for (int i = 0; i < n; i++)
    {
        float h = (i < static_cast<int>(sectorHover_.size())) ? sectorHover_[i] : 0.0f;
        float a0 = startOffset + step * static_cast<float>(i) + gap * 0.5f;
        float a1 = startOffset + step * static_cast<float>(i + 1) - gap * 0.5f;
        float midA = (a0 + a1) * 0.5f;

        float expand = 3.5f * h;
        float srIn = rIn + 2.0f;
        float srOut = rOut + expand;

        ImU32 baseFill = ColorWithAlpha(
            0.095f + 0.085f * h,
            0.105f + 0.090f * h,
            0.125f + 0.095f * h,
            cfg.bgAlpha * alpha);
        AddAnnularSector(dl, center, srIn, srOut, a0, a1, baseFill, 36);

        if (h > 0.02f)
        {
            ImU32 accentRim = ColorWithAlpha(ar, ag, ab, 0.90f * h * alpha);
            AddArcStroke(dl, center, srOut - 0.5f, a0, a1, accentRim, 2.0f, 36);
        }
        else
        {
            ImU32 subtleRim = ColorWithAlpha(0.24f, 0.27f, 0.33f, 0.55f * alpha);
            AddArcStroke(dl, center, srOut, a0, a1, subtleRim, 1.0f, 36);
        }

        if (sectors[i].value >= 0.0f && !sectors[i].muted)
        {
            float span = (a1 - a0) * Clamp01(sectors[i].value);
            if (span > 0.01f)
            {
                AddArcStroke(dl, center, srIn + 2.0f, a0, a0 + span, ColorWithAlpha(ar, ag, ab, (0.35f + 0.50f * h) * alpha), 1.8f, 24);
            }
        }

        float iconR = (srIn + srOut) * 0.50f;
        ImVec2 iconPos(center.x + std::cos(midA) * iconR, center.y + std::sin(midA) * iconR - 9.0f);
        ImU32 iconCol = (h > 0.25f)
            ? ColorWithAlpha(0.96f, 0.97f, 0.99f, alpha)
            : ColorWithAlpha(0.72f, 0.75f, 0.80f, 0.92f * alpha);
        DrawVectorIcon(dl, sectors[i].icon, iconPos, 13.0f, iconCol, sectors[i].muted);

        ImVec2 txtSz = ImGui::CalcTextSize(sectors[i].label.c_str());
        ImVec2 txtPos(center.x + std::cos(midA) * iconR - txtSz.x * 0.5f, iconPos.y + 11.0f);
        dl->AddText(ImVec2(txtPos.x, txtPos.y + 1.0f), ColorWithAlpha(0.0f, 0.0f, 0.0f, 0.50f * alpha), sectors[i].label.c_str());
        dl->AddText(txtPos, iconCol, sectors[i].label.c_str());
    }

    if (activeSubOwner_ >= 0 && activeSubOwner_ < n && subRingAnim_ > 0.02f && !subItems_.empty())
    {
        float ownerMid = startOffset + step * (static_cast<float>(activeSubOwner_) + 0.5f);
        float subIn = rOut + 8.0f;
        float subOut = subIn + 56.0f * subRingAnim_;
        float totalSpan = (std::min)(kPi * 0.92f, static_cast<float>(subItems_.size()) * 0.44f);
        float sA0 = ownerMid - totalSpan * 0.5f;
        float sStep = totalSpan / static_cast<float>(subItems_.size());

        float dist = std::sqrt(cursorOffsetX_ * cursorOffsetX_ + cursorOffsetY_ * cursorOffsetY_);
        int newSubHover = -1;
        if (dist > cfg.outerRadius + 2.0f)
        {
            float rawAngle = std::atan2(cursorOffsetY_, cursorOffsetX_);
            for (int j = 0; j < static_cast<int>(subItems_.size()); j++)
            {
                float ja0 = sA0 + sStep * static_cast<float>(j);
                float ja1 = ja0 + sStep;
                float jmid = (ja0 + ja1) * 0.5f;
                float diff = rawAngle - jmid;
                while (diff > kPi) diff -= kTwoPi;
                while (diff < -kPi) diff += kTwoPi;
                if (std::abs(diff) <= sStep * 0.5f)
                {
                    newSubHover = j;
                    break;
                }
            }
        }
        hoveredSubItem_ = newSubHover;

        for (int j = 0; j < static_cast<int>(subItems_.size()); j++)
        {
            float sh = (j == hoveredSubItem_) ? 1.0f : 0.0f;
            if (j < static_cast<int>(subHover_.size()))
            {
                subHover_[j] = ExpLerp(subHover_[j], sh, 20.0f, 0.016f);
                sh = subHover_[j];
            }

            float ja0 = sA0 + sStep * static_cast<float>(j) + 0.016f;
            float ja1 = sA0 + sStep * static_cast<float>(j + 1) - 0.016f;
            float jmid = (ja0 + ja1) * 0.5f;

            ImU32 subFill = ColorWithAlpha(
                0.10f + 0.08f * sh,
                0.11f + 0.085f * sh,
                0.13f + 0.09f * sh,
                cfg.bgAlpha * alpha * subRingAnim_);
            AddAnnularSector(dl, center, subIn, subOut + 2.0f * sh, ja0, ja1, subFill, 20);
            AddArcStroke(
                dl,
                center,
                subOut + 2.0f * sh,
                ja0,
                ja1,
                sh > 0.2f ? ColorWithAlpha(ar, ag, ab, 0.90f * alpha * subRingAnim_) : ColorWithAlpha(0.24f, 0.27f, 0.33f, 0.60f * alpha * subRingAnim_),
                sh > 0.2f ? 1.8f : 1.0f,
                20);

            if (subItems_[j].value >= 0.0f && !subItems_[j].muted)
            {
                float span = (ja1 - ja0) * Clamp01(subItems_[j].value);
                AddArcStroke(dl, center, subIn + 2.0f, ja0, ja0 + span, ColorWithAlpha(ar, ag, ab, (0.45f + 0.45f * sh) * alpha * subRingAnim_), 1.8f, 16);
            }

            float jr = (subIn + subOut) * 0.5f;
            ImVec2 jp(center.x + std::cos(jmid) * jr, center.y + std::sin(jmid) * jr);
            std::string title = subItems_[j].label;
            if (title.size() > 9) title = title.substr(0, 8) + ".";
            ImVec2 lsz = ImGui::CalcTextSize(title.c_str());
            dl->AddText(
                ImVec2(jp.x - lsz.x * 0.5f, jp.y - (subItems_[j].subLabel.empty() ? lsz.y * 0.5f : 13.0f)),
                ColorWithAlpha(0.94f, 0.95f, 0.97f, alpha * subRingAnim_),
                title.c_str());

            if (!subItems_[j].subLabel.empty())
            {
                ImVec2 ssz = ImGui::CalcTextSize(subItems_[j].subLabel.c_str());
                dl->AddText(
                    ImVec2(jp.x - ssz.x * 0.5f, jp.y + 1.0f),
                    ColorWithAlpha(ar, ag, ab, 0.92f * alpha * subRingAnim_),
                    subItems_[j].subLabel.c_str());
            }
        }
    }

    float hubR = rIn - 2.0f;
    dl->AddCircleFilled(center, hubR, ColorWithAlpha(0.08f, 0.09f, 0.11f, 0.96f * alpha), 48);
    dl->AddCircle(center, hubR, ColorWithAlpha(0.28f, 0.31f, 0.38f, 0.85f * alpha), 48, 1.2f);

    if (hasPointerAngle_ && hoveredSector_ >= 0)
    {
        float pa = smoothedAngle_;
        float px = std::cos(pa);
        float py = std::sin(pa);
        float tx = -py;
        float ty = px;
        ImVec2 tip(center.x + px * (hubR + 7.0f), center.y + py * (hubR + 7.0f));
        ImVec2 baseL(center.x + px * (hubR - 1.0f) + tx * 5.0f, center.y + py * (hubR - 1.0f) + ty * 5.0f);
        ImVec2 baseR(center.x + px * (hubR - 1.0f) - tx * 5.0f, center.y + py * (hubR - 1.0f) - ty * 5.0f);
        dl->AddTriangleFilled(tip, baseL, baseR, ColorWithAlpha(ar, ag, ab, 0.95f * alpha));
    }

    std::string topLine = centerTime;
    std::string bottomLine;
    if (hoveredSubItem_ >= 0 && hoveredSubItem_ < static_cast<int>(subItems_.size()))
    {
        topLine = subItems_[hoveredSubItem_].subLabel.empty() ? subItems_[hoveredSubItem_].label : subItems_[hoveredSubItem_].subLabel;
        bottomLine = subItems_[hoveredSubItem_].label;
    }
    else if (hoveredSector_ >= 0 && hoveredSector_ < n)
    {
        topLine = sectors[hoveredSector_].valueText.empty() ? sectors[hoveredSector_].label : sectors[hoveredSector_].valueText;
        bottomLine = sectors[hoveredSector_].label;
    }
    else
    {
        char vbuf[16]{};
        if (masterMuted) std::snprintf(vbuf, sizeof(vbuf), "MUTED");
        else std::snprintf(vbuf, sizeof(vbuf), "VOL %d%%", static_cast<int>(masterVolume * 100.0f + 0.5f));
        bottomLine = vbuf;
    }

    if (topLine.size() > 9) topLine = topLine.substr(0, 9);
    if (bottomLine.size() > 10) bottomLine = bottomLine.substr(0, 10);

    ImVec2 tsz = ImGui::CalcTextSize(topLine.c_str());
    dl->AddText(ImVec2(center.x - tsz.x * 0.5f, center.y - tsz.y - 1.0f), ColorWithAlpha(0.95f, 0.96f, 0.98f, alpha), topLine.c_str());

    if (!bottomLine.empty())
    {
        ImVec2 bsz = ImGui::CalcTextSize(bottomLine.c_str());
        dl->AddText(ImVec2(center.x - bsz.x * 0.5f, center.y + 2.0f), ColorWithAlpha(ar, ag, ab, 0.90f * alpha), bottomLine.c_str());
    }

    if (targetOpen_ && showVirtualCursor_)
    {
        ImVec2 vCursor(center.x + cursorOffsetX_, center.y + cursorOffsetY_);
        dl->AddCircleFilled(vCursor, 4.5f, ColorWithAlpha(0.06f, 0.07f, 0.09f, 0.90f * alpha), 16);
        dl->AddCircleFilled(vCursor, 3.0f, ColorWithAlpha(ar, ag, ab, 0.95f * alpha), 16);
    }
}
