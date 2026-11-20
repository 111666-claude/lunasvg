#ifndef LUNASVG_SVGFILTERELEMENT_H
#define LUNASVG_SVGFILTERELEMENT_H

#include "svgelement.h"

namespace lunasvg {

class SVGRenderState;

// A single primitive inside an SVGFilterElement.
class SVGFilterPrimitiveElement : public SVGElement {
public:
    SVGFilterPrimitiveElement(Document* document, ElementID id);

    const std::string& in() const { return m_in.value(); }
    const std::string& result() const { return m_result.value(); }

    // Runs the primitive against the already-computed named results and
    // returns a freshly allocated canvas owned by the filter context.
    // Returns nullptr when the primitive cannot produce any output.
    // `previousResult` is the previous primitive's output (or SourceGraphic
    // for the first primitive), used when "in" is omitted.
    virtual std::shared_ptr<Canvas> apply(SVGRenderState& state,
        const std::map<std::string, std::shared_ptr<Canvas>>& results,
        const Canvas& sourceGraphic, const Canvas& sourceAlpha,
        const Rect& filterRegion, const Canvas* previousResult) const;

protected:
    // Resolves an "in" reference to a concrete canvas. Built-ins resolve to
    // the source buffers; named references resolve against `results`; an
    // unknown/missing reference resolves to nullptr (transparent black).
    // An empty name selects `fallback`, which is SourceGraphic for the first
    // primitive and the previous primitive's output afterwards (SVG 1.1).
    static const Canvas* resolveInput(const std::string& name,
        const std::map<std::string, std::shared_ptr<Canvas>>& results,
        const Canvas& sourceGraphic, const Canvas& sourceAlpha,
        const Canvas* fallback);

private:
    SVGString m_in;
    SVGString m_result;
};

class SVGFEGaussianBlurElement final : public SVGFilterPrimitiveElement {
public:
    SVGFEGaussianBlurElement(Document* document);

    std::shared_ptr<Canvas> apply(SVGRenderState& state,
        const std::map<std::string, std::shared_ptr<Canvas>>& results,
        const Canvas& sourceGraphic, const Canvas& sourceAlpha,
        const Rect& filterRegion, const Canvas* previousResult) const final;
};

class SVGFeOffsetElement final : public SVGFilterPrimitiveElement {
public:
    SVGFeOffsetElement(Document* document);

    std::shared_ptr<Canvas> apply(SVGRenderState& state,
        const std::map<std::string, std::shared_ptr<Canvas>>& results,
        const Canvas& sourceGraphic, const Canvas& sourceAlpha,
        const Rect& filterRegion, const Canvas* previousResult) const final;
};

class SVGFeMergeElement final : public SVGFilterPrimitiveElement {
public:
    SVGFeMergeElement(Document* document);

    std::shared_ptr<Canvas> apply(SVGRenderState& state,
        const std::map<std::string, std::shared_ptr<Canvas>>& results,
        const Canvas& sourceGraphic, const Canvas& sourceAlpha,
        const Rect& filterRegion, const Canvas* previousResult) const final;
};

class SVGFeMergeNodeElement final : public SVGElement {
public:
    SVGFeMergeNodeElement(Document* document);

    const std::string& in() const { return m_in.value(); }

private:
    SVGString m_in;
};

class SVGFilterElement final : public SVGGraphicsElement {
public:
    SVGFilterElement(Document* document);

    const SVGEnumeration<Units>& filterUnits() const { return m_filterUnits; }
    const SVGEnumeration<Units>& primitiveUnits() const { return m_primitiveUnits; }

    // Device-space region covered by the filter output, intersected with the
    // target canvas. Returns Rect::Empty when the region is degenerate.
    Rect filterBoundingBox(const SVGElement* element, const SVGRenderState& state) const;

    // Validates the whole chain once per render: finite/non-negative numeric
    // attributes, resolvable references and no cycles.
    bool isChainValid() const;

    // Renders the filter chain. `source` holds the rendered reference element
    // (already clipped/masked). The returned canvas shares the source origin.
    std::shared_ptr<Canvas> applyFilter(SVGRenderState& state, const Canvas& source) const;

private:
    SVGLength m_x;
    SVGLength m_y;
    SVGLength m_width;
    SVGLength m_height;
    SVGEnumeration<Units> m_filterUnits;
    SVGEnumeration<Units> m_primitiveUnits;
};

} // namespace lunasvg

#endif // LUNASVG_SVGFILTERELEMENT_H
