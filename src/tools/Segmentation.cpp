#include "tools/Segmentation.h"
#include <random>
#include <vector>
#include "commands/CmdPaintSingleColor.h"
#include "ui/MainApplication.h"

namespace pepr3d {

void Segmentation::drawToSidePane(SidePane& sidePane) {
    if(!mGeometryCorrect) {
        sidePane.drawText("Polyhedron not built, since the geometry was damaged. Tool disabled.");
        return;
    }
    const bool isSdfComputed = mApplication.getCurrentGeometry()->isSdfComputed();
    if(!isSdfComputed) {
        sidePane.drawText("Warning: This computation may take a long time to perform.");
        if(sidePane.drawButton("Compute SDF")) {
            mApplication.getCurrentGeometry()->computeSdfValues();
        }
    } else {
        if(sidePane.drawButton("Segment!")) {
            computeSegmentation();
        }
        sidePane.drawIntDragger("Robustness [2,15]", mNumberOfClusters, 0.25f, 2, 15, "%.0f", 40.0f);
        sidePane.drawFloatDragger("Edge tolerance [0,1]", mSmoothingLambda, .01f, 0.01f, 1.f, "%.02f", 70.f);
    }

    sidePane.drawSeparator();

    ColorManager& colorManager = mApplication.getCurrentGeometry()->getColorManager();
    if(mPickState) {
        sidePane.drawText("Segmented into " + std::to_string(mNumberOfSegments) +
                          " segments. Assign a color from the palette to each segment.");

        sidePane.drawColorPalette();

        for(const auto& toPaint : mSegmentToTriangleIds) {
            std::string displayText = "Segment " + std::to_string(toPaint.first);

            glm::vec4 colorOfSegment(0, 0, 0, 1);
            if(mNewColors[toPaint.first] != std::numeric_limits<size_t>::max()) {
                colorOfSegment = colorManager.getColor(mNewColors[toPaint.first]);
            } else {
                colorOfSegment = mSegmentationColors[toPaint.first];
            }
            glm::vec3 hsvButtonColor = ci::rgbToHsv(static_cast<ci::ColorA>(colorOfSegment));
            hsvButtonColor.y = 0.75;  // reduce the saturation
            ci::ColorA borderColor = ci::hsvToRgb(hsvButtonColor);
            float thickness = 3.0f;
            if(mHoveredTriangleId) {
                auto find = mTriangleToSegmentMap.find(*mHoveredTriangleId);
                assert(find != mTriangleToSegmentMap.end());
                assert(find->first == *mHoveredTriangleId);
                if(find->second == toPaint.first) {
                    borderColor = ci::ColorA(1, 0, 0, 1);
                    displayText = "Currently hovered";
                    thickness = 5.0f;
                }
            }
            if(sidePane.drawColoredButton(displayText.c_str(), borderColor, thickness)) {
                mNewColors[toPaint.first] = colorManager.getActiveColorIndex();
                const glm::vec4 newColor = colorManager.getColor(colorManager.getActiveColorIndex());
                setSegmentColor(toPaint.first, newColor);
            }
        }

        if(sidePane.drawButton("Accept")) {
            // Find the maximum index of the color assignment
            size_t maxColorIndex = std::numeric_limits<size_t>::min();
            for(const auto& segment : mSegmentToTriangleIds) {
                const size_t activeColorAssigned = mNewColors[segment.first];
                if(activeColorAssigned > maxColorIndex) {
                    maxColorIndex = activeColorAssigned;
                }
            }

            // If the user assigned colors are valid (i.e. there aren't colors out of the palette size), apply.
            if(maxColorIndex < colorManager.size()) {
                for(const auto& segment : mSegmentToTriangleIds) {
                    const size_t activeColorAssigned = mNewColors[segment.first];
                    auto proxy = segment.second;
                    CommandManager<Geometry>* const commandManager = mApplication.getCommandManager();
                    commandManager->execute(
                        std::make_unique<CmdPaintSingleColor>(std::move(proxy), activeColorAssigned), false);
                }
                reset();
                CI_LOG_I("Segmentation applied.");
            } else {  // Else report the error to the user and continue.
                /// \todo Popup for the user
                CI_LOG_W("Please assign all segments to a color from the palette first.");
            }
        }
        if(sidePane.drawButton("Cancel")) {
            cancel();
        }
    }
}

void Segmentation::cancel() {
    if(mPickState) {
        reset();
        CI_LOG_I("Segmentation canceled.");
    }
}

void Segmentation::onToolDeselect(ModelView& modelView) {
    if(!mGeometryCorrect) {
        return;
    }
    cancel();
}

void Segmentation::onModelViewMouseMove(ModelView& modelView, ci::app::MouseEvent event) {
    ci::Ray mLastRay = modelView.getRayFromWindowCoordinates(event.getPos());
    const auto geometry = mApplication.getCurrentGeometry();
    if(geometry == nullptr) {
        mHoveredTriangleId = {};
        return;
    }
    mHoveredTriangleId = safeIntersectMesh(mApplication, mLastRay);
}

void Segmentation::drawToModelView(ModelView& modelView) {
    if(mHoveredTriangleId && mPickState && mGeometryCorrect) {
        modelView.drawTriangleHighlight(*mHoveredTriangleId);
    }
}

void Segmentation::onModelViewMouseDown(ModelView& modelView, ci::app::MouseEvent event) {
    if(!event.isLeftDown()) {
        return;
    }
    if(!mHoveredTriangleId) {
        return;
    }
    if(!mPickState) {
        return;
    }
    if(!mGeometryCorrect) {
        return;
    }

    auto find = mTriangleToSegmentMap.find(*mHoveredTriangleId);
    assert(find != mTriangleToSegmentMap.end());
    assert(find->first == *mHoveredTriangleId);

    const size_t segId = find->second;
    assert(segId < mNumberOfSegments);
    assert(segId < mNewColors.size());

    ColorManager& colorManager = mApplication.getCurrentGeometry()->getColorManager();

    mNewColors[segId] = colorManager.getActiveColorIndex();
    const glm::vec4 newColor = colorManager.getColor(colorManager.getActiveColorIndex());
    setSegmentColor(segId, newColor);
}

void Segmentation::reset() {
    mApplication.getModelView().setColorOverride(false);
    mApplication.getModelView().getOverrideColorBuffer().clear();

    mNumberOfSegments = 0;
    mPickState = false;

    mNewColors.clear();
    mSegmentationColors.clear();
    mHoveredTriangleId = {};

    mSegmentToTriangleIds.clear();
    mTriangleToSegmentMap.clear();
}

void Segmentation::computeSegmentation() {
    cancel();

    mSmoothingLambda = std::min<float>(mSmoothingLambda, 1.0f);
    mSmoothingLambda = std::max<float>(mSmoothingLambda, 0.01f);

    mNumberOfClusters =
        std::min<int>(mNumberOfClusters, static_cast<int>(mApplication.getCurrentGeometry()->getTriangleCount()) - 2);
    mNumberOfClusters = std::max<int>(2, mNumberOfClusters);

    assert(0.0f < mSmoothingLambda && mSmoothingLambda <= 1.0f);
    assert(2 <= mNumberOfClusters && mNumberOfClusters <= mApplication.getCurrentGeometry()->getTriangleCount() &&
           mNumberOfClusters <= 15);

    mNumberOfSegments = mApplication.getCurrentGeometry()->segmentation(mNumberOfClusters, mSmoothingLambda,
                                                                        mSegmentToTriangleIds, mTriangleToSegmentMap);

    if(mNumberOfSegments > 0) {
        mPickState = true;

        // If we this time segment into more segments than previously
        if(mNewColors.size() < mNumberOfSegments) {
            mNewColors.resize(mNumberOfSegments);
            for(size_t i = 0; i < mNumberOfSegments; ++i) {
                mNewColors[i] = std::numeric_limits<size_t>::max();
            }
        }

        assert(mNumberOfSegments != std::numeric_limits<size_t>::max());
        assert(mNumberOfSegments <= PEPR3D_MAX_PALETTE_COLORS);

        // Generate new colors
        pepr3d::ColorManager::generateColors(mNumberOfSegments, mSegmentationColors);
        assert(mSegmentationColors.size() == mNumberOfSegments);

        // Create an override color buffer based on the segmentation
        std::vector<glm::vec4> newOverrideBuffer;
        newOverrideBuffer.resize(mApplication.getCurrentGeometry()->getTriangleCount() * 3);
        for(const auto& toPaint : mSegmentToTriangleIds) {
            for(const auto& tri : toPaint.second) {
                newOverrideBuffer[3 * tri] = mSegmentationColors[toPaint.first];
                newOverrideBuffer[3 * tri + 1] = mSegmentationColors[toPaint.first];
                newOverrideBuffer[3 * tri + 2] = mSegmentationColors[toPaint.first];
            }
        }
        mApplication.getModelView().getOverrideColorBuffer() = newOverrideBuffer;
        mApplication.getModelView().setColorOverride(true);
    }
}

void Segmentation::setSegmentColor(const size_t segmentId, const glm::vec4 newColor) {
    std::vector<glm::vec4>& overrideBuffer = mApplication.getModelView().getOverrideColorBuffer();
    auto segmentTris = mSegmentToTriangleIds.find(segmentId);

    if(segmentTris == mSegmentToTriangleIds.end()) {
        assert(false);
        return;
    }
    assert((*segmentTris).first == segmentId);

    assert(!overrideBuffer.empty());
    for(const auto& tri : (*segmentTris).second) {
        overrideBuffer[3 * tri] = newColor;
        overrideBuffer[3 * tri + 1] = newColor;
        overrideBuffer[3 * tri + 2] = newColor;
    }
}

void Segmentation::onNewGeometryLoaded(ModelView& modelView) {
    mHoveredTriangleId = {};
    mGeometryCorrect = mApplication.getCurrentGeometry()->polyhedronValid();
    if(!mGeometryCorrect) {
        return;
    }
    CI_LOG_I("Model changed, segmentation reset");
    reset();
}
}  // namespace pepr3d