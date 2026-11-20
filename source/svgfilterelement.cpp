#include "svgfilterelement.h"
#include "svgparserutils.h"
#include "svgrenderstate.h"

#include <cmath>
#include <functional>

namespace lunasvg {

namespace {

// Filters nested deeper than this many levels are treated as cycles.
constexpr int kMaxFilterDepth = 16;

bool isBuiltInInput(const std::string& name)
{
    return name == "SourceGraphic" || name == "SourceAlpha";
}

// Collects the named inputs a primitive depends on. Empty "in" values use
// the previous primitive output (or SourceGraphic for the first one), which
// can never form a cycle, so they are not returned here.
std::vector<std::string> namedInputs(const SVGElement* primitive)
{
    std::vector<std::string> names;
    auto addName = [&names](const std::string& name) {
        if(!name.empty() && !isBuiltInInput(name))
            names.push_back(name);
    };

    if(primitive->id() == ElementID::FeMerge) {
        for(const auto& child : primitive->children()) {
            auto node = toSVGElement(child);
            if(node && node->id() == ElementID::FeMergeNode)
                addName(node->getAttribute(PropertyID::In));
        }
    } else {
        addName(primitive->getAttribute(PropertyID::In));
    }

    return names;
}

// Parses "stdDeviation" style: one number or two space/comma separated numbers.
bool parseStdDeviation(std::string_view input, float& x, float& y)
{
    stripLeadingAndTrailingSpaces(input);
    if(!parseNumber(input, x) || !std::isfinite(x))
        return false;
    if(input.empty()) {
        y = x;
        return true;
    }

    if(!skipOptionalSpacesOrComma(input))
        return false;
    if(!parseNumber(input, y) || !std::isfinite(y) || !input.empty())
        return false;
    return true;
}

} // namespace

SVGFilterPrimitiveElement::SVGFilterPrimitiveElement(Document* document, ElementID id)
    : SVGElement(document, id)
    , m_in(PropertyID::In)
    , m_result(PropertyID::Result)
{
    addProperty(m_in);
    addProperty(m_result);
}

const Canvas* SVGFilterPrimitiveElement::resolveInput(const std::string& name,
    const std::map<std::string, std::shared_ptr<Canvas>>& results,
    const Canvas& sourceGraphic, const Canvas& sourceAlpha,
    const Canvas* fallback)
{
    if(name.empty())
        return fallback;
    if(name == "SourceGraphic")
        return &sourceGraphic;
    if(name == "SourceAlpha")
        return &sourceAlpha;
    auto it = results.find(name);
    if(it == results.end())
        return nullptr; // Missing node: transparent black.
    return it->second.get();
}

std::shared_ptr<Canvas> SVGFilterPrimitiveElement::apply(SVGRenderState&,
    const std::map<std::string, std::shared_ptr<Canvas>>&,
    const Canvas&, const Canvas&, const Rect&, const Canvas*) const
{
    return nullptr;
}

SVGFEGaussianBlurElement::SVGFEGaussianBlurElement(Document* document)
    : SVGFilterPrimitiveElement(document, ElementID::FeGaussianBlur)
{
}

std::shared_ptr<Canvas> SVGFEGaussianBlurElement::apply(SVGRenderState& state,
    const std::map<std::string, std::shared_ptr<Canvas>>& results,
    const Canvas& sourceGraphic, const Canvas& sourceAlpha,
    const Rect& filterRegion, const Canvas* previousResult) const
{
    float sigmaX = 0.f, sigmaY = 0.f;
    if(!parseStdDeviation(getAttribute(PropertyID::StdDeviation), sigmaX, sigmaY)
        || sigmaX < 0.f || sigmaY < 0.f) {
        return nullptr;
    }

    // Output always covers the whole filter region so the blur can spread in
    // every direction instead of being clipped to the input's own bounds.
    auto output = Canvas::create(filterRegion);
    auto input = resolveInput(in(), results, sourceGraphic, sourceAlpha, previousResult);
    if(input == nullptr)
        return output; // Missing input: transparent black.
    output->drawCanvas(*input, 0.f, 0.f);

    // feGaussianBlur operates in the user space current when the filter was
    // invoked; scale the standard deviations by the CTM axes.
    auto scaleX = state.currentTransform().xScale();
    auto scaleY = state.currentTransform().yScale();
    output->gaussianBlur(sigmaX * scaleX, sigmaY * scaleY);
    return output;
}

SVGFeOffsetElement::SVGFeOffsetElement(Document* document)
    : SVGFilterPrimitiveElement(document, ElementID::FeOffset)
{
}

std::shared_ptr<Canvas> SVGFeOffsetElement::apply(SVGRenderState& state,
    const std::map<std::string, std::shared_ptr<Canvas>>& results,
    const Canvas& sourceGraphic, const Canvas& sourceAlpha,
    const Rect& filterRegion, const Canvas* previousResult) const
{
    auto output = Canvas::create(filterRegion);
    auto input = resolveInput(in(), results, sourceGraphic, sourceAlpha, previousResult);
    if(input == nullptr)
        return output;

    auto& dxAttr = getAttribute(PropertyID::Dx);
    auto& dyAttr = getAttribute(PropertyID::Dy);
    float dx = 0.f, dy = 0.f;
    std::string_view dxInput(dxAttr);
    std::string_view dyInput(dyAttr);
    if((!dxAttr.empty() && (!parseNumber(dxInput, dx) || !std::isfinite(dx) || !dxInput.empty()))
        || (!dyAttr.empty() && (!parseNumber(dyInput, dy) || !std::isfinite(dy) || !dyInput.empty()))) {
        return nullptr;
    }

    // Map the offset vector through the CTM like every other filter length.
    auto offset = state.currentTransform().mapPoint(dx, dy);
    output->drawCanvas(*input, offset.x, offset.y);
    return output;
}

SVGFeMergeElement::SVGFeMergeElement(Document* document)
    : SVGFilterPrimitiveElement(document, ElementID::FeMerge)
{
}

std::shared_ptr<Canvas> SVGFeMergeElement::apply(SVGRenderState&,
    const std::map<std::string, std::shared_ptr<Canvas>>& results,
    const Canvas& sourceGraphic, const Canvas& sourceAlpha,
    const Rect& filterRegion, const Canvas* previousResult) const
{
    auto output = Canvas::create(filterRegion);
    // An feMergeNode without "in" takes the previous feMergeNode's input;
    // the first one falls back to the previous primitive result.
    const Canvas* mergeFallback = previousResult;
    for(const auto& child : children()) {
        auto node = toSVGElement(child);
        if(node == nullptr || node->id() != ElementID::FeMergeNode)
            continue;
        auto input = resolveInput(node->getAttribute(PropertyID::In), results, sourceGraphic, sourceAlpha, mergeFallback);
        if(input != nullptr) {
            output->drawCanvas(*input, 0.f, 0.f);
            mergeFallback = input;
        }
    }

    return output;
}

SVGFeMergeNodeElement::SVGFeMergeNodeElement(Document* document)
    : SVGElement(document, ElementID::FeMergeNode)
    , m_in(PropertyID::In)
{
    addProperty(m_in);
}

SVGFilterElement::SVGFilterElement(Document* document)
    : SVGGraphicsElement(document, ElementID::Filter)
    , m_x(PropertyID::X, LengthDirection::Horizontal, LengthNegativeMode::Allow, -10.f, LengthUnits::Percent)
    , m_y(PropertyID::Y, LengthDirection::Vertical, LengthNegativeMode::Allow, -10.f, LengthUnits::Percent)
    , m_width(PropertyID::Width, LengthDirection::Horizontal, LengthNegativeMode::Forbid, 120.f, LengthUnits::Percent)
    , m_height(PropertyID::Height, LengthDirection::Vertical, LengthNegativeMode::Forbid, 120.f, LengthUnits::Percent)
    , m_filterUnits(PropertyID::FilterUnits, Units::ObjectBoundingBox)
    , m_primitiveUnits(PropertyID::PrimitiveUnits, Units::UserSpaceOnUse)
{
    addProperty(m_x);
    addProperty(m_y);
    addProperty(m_width);
    addProperty(m_height);
    addProperty(m_filterUnits);
    addProperty(m_primitiveUnits);
}

Rect SVGFilterElement::filterBoundingBox(const SVGElement* element, const SVGRenderState& state) const
{
    auto bbox = element->paintBoundingBox();
    if(!bbox.isValid() || bbox.isEmpty())
        return Rect::Empty;

    LengthContext lengthContext(this, m_filterUnits.value());
    Rect region = {
        lengthContext.valueForLength(m_x),
        lengthContext.valueForLength(m_y),
        lengthContext.valueForLength(m_width),
        lengthContext.valueForLength(m_height)
    };

    if(!std::isfinite(region.x) || !std::isfinite(region.y)
        || !std::isfinite(region.w) || !std::isfinite(region.h)
        || region.w <= 0.f || region.h <= 0.f) {
        return Rect::Empty;
    }

    if(m_filterUnits.value() == Units::ObjectBoundingBox) {
        region.x = region.x * bbox.w + bbox.x;
        region.y = region.y * bbox.h + bbox.y;
        region.w *= bbox.w;
        region.h *= bbox.h;
    }

    auto deviceRegion = state.currentTransform().mapRect(region);
    return deviceRegion.intersected(state.canvas()->extents());
}

bool SVGFilterElement::isChainValid() const
{
    // Gather the supported primitives in document order and map each result
    // name to the primitive that produces it.
    std::vector<const SVGElement*> primitives;
    std::map<std::string, const SVGElement*> producer;
    for(const auto& child : children()) {
        auto primitive = toSVGElement(child);
        if(primitive == nullptr)
            continue;
        auto id = primitive->id();
        if(id != ElementID::FeGaussianBlur && id != ElementID::FeOffset && id != ElementID::FeMerge)
            continue;

        if(id == ElementID::FeGaussianBlur) {
            float sigmaX = 0.f, sigmaY = 0.f;
            if(!parseStdDeviation(primitive->getAttribute(PropertyID::StdDeviation), sigmaX, sigmaY)
                || sigmaX < 0.f || sigmaY < 0.f) {
                return false; // Negative/non-finite stdDeviation disables the filter.
            }
        } else if(id == ElementID::FeOffset) {
            float value = 0.f;
            for(auto property : {PropertyID::Dx, PropertyID::Dy}) {
                auto& attr = primitive->getAttribute(property);
                if(attr.empty())
                    continue;
                std::string_view input(attr);
                if(!parseNumber(input, value) || !std::isfinite(value) || !input.empty())
                    return false; // Malformed/non-finite offset disables the filter.
            }
        }

        primitives.push_back(primitive);
        auto& resultName = primitive->getAttribute(PropertyID::Result);
        if(!resultName.empty())
            producer.emplace(resultName, primitive);
    }

    if(primitives.empty())
        return false; // Nothing to apply.

    // DFS over the result dependency graph; a gray node means a cycle.
    enum class VisitState { Unvisited, InProgress, Done };
    std::map<const SVGElement*, VisitState> visitStates;
    for(auto primitive : primitives)
        visitStates[primitive] = VisitState::Unvisited;

    std::function<bool(const SVGElement*, int)> visit = [&](const SVGElement* node, int depth) -> bool {
        if(depth > kMaxFilterDepth || visitStates[node] == VisitState::InProgress)
            return false; // Circular reference.
        if(visitStates[node] == VisitState::Done)
            return true;
        visitStates[node] = VisitState::InProgress;
        for(const auto& input : namedInputs(node)) {
            auto it = producer.find(input);
            // A missing named input is legal: it resolves to transparent
            // black at render time, so it cannot participate in a cycle.
            if(it != producer.end() && !visit(it->second, depth + 1))
                return false;
        }
        visitStates[node] = VisitState::Done;
        return true;
    };

    for(auto primitive : primitives) {
        if(!visit(primitive, 0))
            return false;
    }

    return true;
}

std::shared_ptr<Canvas> SVGFilterElement::applyFilter(SVGRenderState& state, const Canvas& source) const
{
    if(source.width() <= 0 || source.height() <= 0)
        return nullptr;

    auto filterRegion = source.extents();
    auto sourceAlpha = Canvas::create(filterRegion);
    sourceAlpha->blendCanvas(source, BlendMode::Src, 1.f);
    sourceAlpha->convertToAlpha();

    std::map<std::string, std::shared_ptr<Canvas>> results;
    std::shared_ptr<Canvas> lastResult;
    const Canvas* previousResult = &source; // SVG 1.1 default "in" for the first primitive.
    for(const auto& child : children()) {
        auto primitive = toSVGElement(child);
        if(primitive == nullptr)
            continue;
        auto id = primitive->id();
        if(id != ElementID::FeGaussianBlur && id != ElementID::FeOffset && id != ElementID::FeMerge)
            continue;

        auto output = static_cast<const SVGFilterPrimitiveElement*>(primitive)->apply(
            state, results, source, *sourceAlpha, filterRegion, previousResult);
        if(output == nullptr)
            return nullptr; // Primitive error: the whole filter is disabled.
        lastResult = output;
        previousResult = output.get();

        auto& resultName = primitive->getAttribute(PropertyID::Result);
        if(!resultName.empty())
            results.emplace(resultName, output);
    }

    if(lastResult == nullptr)
        return nullptr; // Empty filter: nothing to apply.
    return lastResult;
}

} // namespace lunasvg
