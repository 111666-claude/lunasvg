#include "svgrenderstate.h"
#include "svgfilterelement.h"

namespace lunasvg {

namespace {
// Guards against pathological/cyclic filter references.
constexpr int kMaxFilterDepth = 16;
} // namespace

SVGBlendInfo::SVGBlendInfo(const SVGElement* element)
    : m_clipper(element->clipper())
    , m_masker(element->masker())
    , m_filter(element->filter())
    , m_opacity(element->opacity())
{
}

bool SVGBlendInfo::requiresCompositing(SVGRenderMode mode) const
{
    return (m_clipper && m_clipper->requiresMasking())
        || (mode == SVGRenderMode::Painting && (m_masker || m_filter || m_opacity < 1.f));
}

bool SVGRenderState::hasCycleReference(const SVGElement* element) const
{
    auto current = this;
    do {
        if(element == current->element())
            return true;
        current = current->parent();
    } while(current);
    return false;
}

void SVGRenderState::beginGroup(const SVGBlendInfo& blendInfo)
{
    auto requiresCompositing = blendInfo.requiresCompositing(m_mode);
    if(requiresCompositing) {
        Rect boundingBox;
        if(blendInfo.filter() && m_mode == SVGRenderMode::Painting)
            boundingBox = blendInfo.filter()->filterBoundingBox(m_element, *this);
        else
            boundingBox = m_currentTransform.mapRect(m_element->paintBoundingBox());
        if(boundingBox.isEmpty())
            boundingBox = m_currentTransform.mapRect(m_element->paintBoundingBox());
        boundingBox.intersect(m_canvas->extents());
        m_canvas = Canvas::create(boundingBox);
    } else {
        m_canvas->save();
    }

    if(!requiresCompositing && blendInfo.clipper()) {
        blendInfo.clipper()->applyClipPath(*this);
    }
}

void SVGRenderState::endGroup(const SVGBlendInfo& blendInfo)
{
    if(m_canvas == m_parent->canvas()) {
        m_canvas->restore();
        return;
    }

    // Per SVG 1.1, filter effects apply first (to the raw rendered source),
    // then clipping and masking, then group opacity when compositing.
    const auto* filter = (m_mode == SVGRenderMode::Painting) ? blendInfo.filter() : nullptr;
    std::shared_ptr<Canvas> filteredCanvas;
    if(filter && filterDepth() < kMaxFilterDepth
        && !hasCycleReference(filter) && filter->isChainValid()
        && !filter->filterBoundingBox(m_element, *this).isEmpty()) {
        enterFilter();
        filteredCanvas = filter->applyFilter(*this, *m_canvas);
        leaveFilter();
    }

    auto opacity = m_mode == SVGRenderMode::Clipping ? 1.f : blendInfo.opacity();
    if(blendInfo.clipper()) {
        auto savedCanvas = m_canvas;
        if(filteredCanvas)
            m_canvas = filteredCanvas;
        blendInfo.clipper()->applyClipMask(*this);
        m_canvas = savedCanvas;
    }
    if(m_mode == SVGRenderMode::Painting && blendInfo.masker()) {
        auto savedCanvas = m_canvas;
        if(filteredCanvas)
            m_canvas = filteredCanvas;
        blendInfo.masker()->applyMask(*this);
        m_canvas = savedCanvas;
    }

    auto& compositedCanvas = filteredCanvas ? filteredCanvas : m_canvas;
    m_parent->m_canvas->blendCanvas(*compositedCanvas, BlendMode::Src_Over, opacity);
}

} // namespace lunasvg
