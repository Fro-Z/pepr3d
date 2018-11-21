#pragma once

#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/exceptions.h>
#include <cinder/Log.h>
#include <cinder/Ray.h>
#include <cinder/gl/gl.h>

#include <cassert>
#include <optional>
#include <vector>

#include "geometry/ColorManager.h"
#include "geometry/ModelExporter.h"
#include "geometry/ModelImporter.h"
#include "geometry/PolyhedronBuilder.h"
#include "geometry/Triangle.h"

namespace pepr3d {

class Geometry {
   public:
    using Direction = pepr3d::DataTriangle::K::Direction_3;
    using Ft = pepr3d::DataTriangle::K::FT;
    using Ray = pepr3d::DataTriangle::K::Ray_3;
    using My_AABB_traits = CGAL::AABB_traits<pepr3d::DataTriangle::K, DataTriangleAABBPrimitive>;
    using Tree = CGAL::AABB_tree<My_AABB_traits>;
    using Ray_intersection = boost::optional<Tree::Intersection_and_primitive_id<Ray>::Type>;
    using BoundingBox = My_AABB_traits::Bounding_box;
    using ColorIndex = GLuint;

   private:
    /// Triangle soup of the model mesh, containing CGAL::Triangle_3 data for AABB tree.
    std::vector<DataTriangle> mTriangles;

    /// Vertex buffer with the same data as mTriangles for OpenGL to render the mesh.
    /// Contains position and color data for each vertex.
    std::vector<glm::vec3> mVertexBuffer;

    /// Color buffer, keeping the invariant that every triangle has only one color - all three vertices have to have the
    /// same color. It is aligned with the vertex buffer and its size should be always equal to the vertex buffer.
    std::vector<ColorIndex> mColorBuffer;

    /// Normal buffer, the triangle has same normal for its every vertex.
    /// It is aligned with the vertex buffer and its size should be always equal to the vertex buffer.
    std::vector<glm::vec3> mNormalBuffer;

    /// Index buffer for OpenGL frontend., specifying the same triangles as in mTriangles.
    std::vector<uint32_t> mIndexBuffer;

    /// AABB tree from the CGAL library, to find intersections with rays generated by user mouse clicks and the mesh.
    std::unique_ptr<Tree> mTree;

    /// AABB of the whole mesh
    std::unique_ptr<BoundingBox> mBoundingBox;

    /// A vector based map mapping size_t into ci::ColorA
    ColorManager mColorManager;

    struct GeometryState {
        std::vector<DataTriangle> triangles;
        ColorManager::ColorMap colorMap;
    };

    struct PolyhedronData {
        /// Vertex positions after joining all identical vertices.
        /// This is after removing all other componens and as such based only on the position property.
        std::vector<glm::vec3> vertices;

        /// Indices to triangles of the polyhedron. Indices are stored in a CCW order, as imported from Assimp.
        std::vector<std::array<size_t, 3>> indices;

        /// CGAL Polyhedral surface class
        Polyhedron P;

        /// A vector with pointers to the polyhedron faces.
        /// The i-th pointer points to the face of the i-th triangle from the indices vector<>.
        std::vector<CGAL::Polyhedron_incremental_builder_3<HalfedgeDS>::Face_handle> faceHandles;

        /// Simple property to check if the Polyhedron is closed or not.
        bool closeCheck = false;
    } mPolyhedronData;

   public:
    /// Empty constructor
    Geometry() {
        mTree = std::make_unique<Tree>();
    }

    Geometry(std::vector<DataTriangle>&& triangles) : mTriangles(std::move(triangles)) {
        generateVertexBuffer();
        generateIndexBuffer();
        generateColorBuffer();
        generateNormalBuffer();
        assert(mIndexBuffer.size() == mVertexBuffer.size());
        mTree = std::make_unique<Tree>(mTriangles.begin(), mTriangles.end());
        assert(mTree->size() == mTriangles.size());
        if(!mTree->empty()) {
            mBoundingBox = std::make_unique<BoundingBox>(mTree->bbox());
        }
    }

    std::vector<glm::vec3>& getVertexBuffer() {
        return mVertexBuffer;
    }

    bool polyClosedCheck() const {
        return mPolyhedronData.closeCheck;
    }

    size_t polyVertCount() const {
        return mPolyhedronData.vertices.size();
    }

    std::vector<uint32_t>& getIndexBuffer() {
        return mIndexBuffer;
    }

    std::vector<ColorIndex>& getColorBuffer() {
        return mColorBuffer;
    }

    std::vector<glm::vec3>& getNormalBuffer() {
        return mNormalBuffer;
    }

    glm::vec3 getBoundingBoxMin() const {
        if(!mBoundingBox) {
            return glm::vec3(0);
        }
        return glm::vec3(mBoundingBox->xmin(), mBoundingBox->ymin(), mBoundingBox->zmin());
    }

    glm::vec3 getBoundingBoxMax() const {
        if(!mBoundingBox) {
            return glm::vec3(0);
        }
        return glm::vec3(mBoundingBox->xmax(), mBoundingBox->ymax(), mBoundingBox->zmax());
    }

    const DataTriangle& getTriangle(const size_t triangleIndex) const {
        assert(triangleIndex < mTriangles.size());
        return mTriangles[triangleIndex];
    }

    size_t getTriangleColor(const size_t triangleIndex) const {
        return getTriangle(triangleIndex).getColor();
    }

    /// Return the number of triangles in the whole mesh
    size_t getTriangleCount() const {
        return mTriangles.size();
    }

    const ColorManager& getColorManager() const {
        return mColorManager;
    }

    ColorManager& getColorManager() {
        return mColorManager;
    }

    /// Loads new geometry into the private data, rebuilds the buffers and other data structures automatically.
    void loadNewGeometry(const std::string& fileName);

    /// Exports the modified geometry to the file specified by a path, file name and file type.
    void exportGeometry(const std::string filePath, const std::string fileName, const std::string fileType);

    /// Set new triangle color. Fast, as it directly modifies the color buffer, without requiring a reload.
    void setTriangleColor(const size_t triangleIndex, const size_t newColor);

    /// Intersects the mesh with the given ray and returns the index of the triangle intersected, if it exists.
    /// Example use: generate ray based on a mouse click, call this method, then call setTriangleColor.
    std::optional<size_t> intersectMesh(const ci::Ray& ray) const;

    /// Save current state into a struct so that it can be restored later (CommandManager target requirement)
    GeometryState saveState() const;

    /// Load previous state from a struct (CommandManager target requirement)
    void loadState(const GeometryState&);

    /// Spreads as BFS, starting from startTriangle to wherever it can reach.
    /// Stopping is handled by the StoppingCondition functor/lambda.
    /// A vector of reached triangle indices is returned;
    template <typename StoppingCondition>
    std::vector<size_t> bucket(const std::size_t startTriangle, const StoppingCondition& stopFunctor);

   private:
    /// Generates the vertex buffer linearly - adding each vertex of each triangle as a new one.
    /// We need to do this because each triangle has to be able to be colored differently, therefore no vertex sharing
    /// is possible.
    void generateVertexBuffer();

    /// Generating a linear index buffer, since we do not reuse any vertices.
    void generateIndexBuffer();

    /// Generating triplets of colors, since we only allow a single-colored triangle.
    void generateColorBuffer();

    /// Generate a buffer of normals. Generates only "triangle normals" - all three vertices have the same normal.
    void generateNormalBuffer();

    /// Build the CGAL Polyhedron construct in mPolyhedronData. Takes a bit of time to rebuild.
    void buildPolyhedron();

    /// Used by BFS in bucket painting. Aggregates the neighbours of the triangle at triIndex by looking into the CGAL
    /// Polyhedron construct.
    std::array<int, 3> gatherNeighbours(
        const size_t triIndex,
        const std::vector<CGAL::Polyhedron_incremental_builder_3<HalfedgeDS>::Face_handle>& faceHandles) const;

    /// Used by BFS in bucket painting. Manages the queue used to search through the graph.
    template <typename StoppingCondition>
    void addNeighboursToQueue(
        const size_t currentVertex,
        const std::vector<CGAL::Polyhedron_incremental_builder_3<HalfedgeDS>::Face_handle>& faceHandles,
        std::unordered_set<size_t>& alreadyVisited, std::deque<size_t>& toVisit,
        const StoppingCondition& stopFunctor) const;
};

template <typename StoppingCondition>
std::vector<size_t> Geometry::bucket(const std::size_t startTriangle, const StoppingCondition& stopFunctor) {
    if(mPolyhedronData.P.is_empty()) {
        return {};
    }

    std::vector<size_t> trianglesToColor;

    std::deque<size_t> toVisit;
    const size_t startingFace = startTriangle;
    toVisit.push_back(startingFace);

    std::unordered_set<size_t> alreadyVisited;
    alreadyVisited.insert(startingFace);

    assert(mPolyhedronData.indices.size() == mTriangles.size());

    while(!toVisit.empty()) {
        // Remove yourself from queue and mark visited
        const size_t currentVertex = toVisit.front();
        toVisit.pop_front();
        assert(alreadyVisited.find(currentVertex) != alreadyVisited.end());
        assert(currentVertex < mTriangles.size());
        assert(toVisit.size() < mTriangles.size());

        // Manage neighbours and grow the queue
        addNeighboursToQueue(currentVertex, mPolyhedronData.faceHandles, alreadyVisited, toVisit, stopFunctor);

        // Set the color
        // setTriangleColor(currentVertex, mColorManager.getActiveColorIndex());
        trianglesToColor.push_back(currentVertex);
    }
    return trianglesToColor;
}

template <typename StoppingCondition>
void Geometry::addNeighboursToQueue(
    const size_t currentVertex,
    const std::vector<CGAL::Polyhedron_incremental_builder_3<HalfedgeDS>::Face_handle>& faceHandles,
    std::unordered_set<size_t>& alreadyVisited, std::deque<size_t>& toVisit,
    const StoppingCondition& stopFunctor) const {
    const std::array<int, 3> neighbours = gatherNeighbours(currentVertex, faceHandles);
    for(int i = 0; i < 3; ++i) {
        if(neighbours[i] == -1) {
            continue;
        } else {
            if(alreadyVisited.find(neighbours[i]) == alreadyVisited.end()) {
                // New vertex -> visit it.
                if(stopFunctor(neighbours[i], currentVertex)) {
                    toVisit.push_back(neighbours[i]);
                    alreadyVisited.insert(neighbours[i]);
                }
            }
        }
    }
}

}  // namespace pepr3d