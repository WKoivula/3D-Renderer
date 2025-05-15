#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <PerlinNoise.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <bitset>

using glm::vec3;
using glm::ivec3;
using namespace std;

// ----------------------------------------------------------------------------
// STRUCTS

struct VoxelData {
    vec3 color;
    vec3 pos;
};

struct Intersection {
    vec3 normal;
    vec3 voxelPos;
    vec3 color;
};

struct Node {
    bool isLeaf = false;
    Node* children[8] = { nullptr };
    vec3 color;
    int depth;
};

struct FlatNode {
    uint8_t childMask;
    uint32_t firstChildIndex;
    uint32_t color;
    bool isLeaf = false;
};

/*
* Class containing the SVO data structure for the voxel world.
*/
class SparseVoxelOctree {
private:
    int svoSize;
    int maxDepth;
    Node* root;

    void insertNode(Node*& node, vec3 point, ivec3 pos, vec3 color, int depth) {
        if (!node) {
            node = new Node;
            node->depth = depth;
        }

        // Stop subdivision at max depth
        if (depth == maxDepth) {
            node->isLeaf = true;
            node->color = color;
            //cout << "Leaf created at depth " << depth << " for point: "
            //    << point.x << ", " << point.y << ", " << point.z << "\n";
            return;
        }

        for (int i = 0; i < 8; i++) {
            if (!node->children[i]) {
                node->children[i] = new Node;
                node->children[i]->depth = depth + 1;
                //cout << node->children[i] << "\n";
            }
        }

        float size = svoSize / (float)exp2(depth);
        ivec3 childPos;
        childPos.x = point.x >= (pos.x * size) + (size / 2.f);
        childPos.y = point.y >= (pos.y * size) + (size / 2.f);
        childPos.z = point.z >= (pos.z * size) + (size / 2.f);

        int childIndex = (childPos.x << 0) | (childPos.y << 1) | (childPos.z << 2);

        pos.x = pos.x << 1 | childPos.x;
        pos.y = pos.y << 1 | childPos.y;
        pos.z = pos.z << 1 | childPos.z;

        insertNode(node->children[childIndex], point, pos, color, depth + 1);
    }

    uint32_t vecToIntColor(vec3 color) {
        uint8_t r = color.r;
        uint8_t g = color.g;
        uint8_t b = color.b;
        return r | g << 8 | b << 16;
    }

    void flattenSVO(Node* node, vector<FlatNode>& flatNodes) {
        if (!node) return;

        FlatNode flatNode;
        flatNode.childMask = 0;
        flatNode.firstChildIndex = UINT32_MAX;
        flatNode.color = vecToIntColor(node->color);
        flatNode.isLeaf = node->isLeaf;

        size_t currentIndex = flatNodes.size();
        flatNodes.push_back(flatNode);

        // Create child mask and get all child nodes
        vector<Node*> childList;
        for (int i = 0; i < 8; i++) {
            if (node->children[i]) {
                flatNode.childMask |= (1 << i);
                childList.push_back(node->children[i]);
            }
        }

        if (!childList.empty()) {
            flatNode.firstChildIndex = flatNodes.size();
            for (int i = 0; i < 8; i++) {
                if (node->children[i]) {
                    flattenSVO(node->children[i], flatNodes);
                }
            }
        }

        flatNodes[currentIndex] = flatNode;
    }
public:
    SparseVoxelOctree(int svoSize, int maxDepth)
        : svoSize(svoSize), maxDepth(maxDepth), root(nullptr) {
    }

    int getSize() {
        return svoSize;
    }

    int getMaxDepth() {
        return maxDepth;
    }

    Node* getNodeAtPos(vec3 pos) {
        int depth = 0;
        vec3 offset = vec3(0.0f, 0.0f, 0.0f);
        Node* node = root;
        Node* lastValidNode = node;

        while (true) {
            lastValidNode = node;  // update the deepest node we've reached

            float nodeSize = svoSize / (float)(1 << depth);
            vec3 center = offset + vec3(nodeSize, nodeSize, nodeSize) * 0.5f;
            //printVec("Center", center);

            ivec3 childPos;
            childPos.x = pos.x >= center.x;
            childPos.y = pos.y >= center.y;
            childPos.z = pos.z >= center.z;
            int childIndex = (childPos.x << 0) | (childPos.y << 1) | (childPos.z << 2);

            // Update offset for next child search
            offset += vec3(childPos.x * nodeSize * 0.5f,
                childPos.y * nodeSize * 0.5f,
                childPos.z * nodeSize * 0.5f);

            //for (int i = 0; i < 8; i++) {
            //    cout << i << " exists as " << node->children[i] << "\n";
            //}

            // If the child exists, keep going  
            if (node->children[childIndex]) {
                //cout << "Found\n";
                node = node->children[childIndex];
                depth++;
            }
            else {
                break;
            }
        }

        //cout << "Deepest node found at depth: " << lastValidNode->depth
        //    << ", isLeaf: " << (lastValidNode->isLeaf ? "True" : "False") << "\n";
        return lastValidNode;
    }

    void printVec(string name, vec3 pos) {
        cout << name << ": " << pos.x << ", " << pos.y << ", " << pos.z << "\n";
    }

    float safeDiv(float a, float b) {
        const float tiny = 1e-6f;
        return a / (abs(b) < tiny ? copysign(tiny, b) : b);
    }

    bool ClosestIntersection(vec3 pos, vec3 d, Intersection& intersection) {
        int maxSteps = 100;
        vec3 normal = vec3(0);

        // Small offset to avoid exact axis-alignment
        const float rayEpsilon = 1e-5f;
        vec3 offset = vec3(0.0f);
        if (d.x == 0.0f) offset.x = rayEpsilon;
        if (d.y == 0.0f) offset.y = rayEpsilon;
        if (d.z == 0.0f) offset.z = rayEpsilon;

        // Apply the offset to avoid precision issues with perfectly aligned rays
        pos += offset;

        for (int i = 0; i < maxSteps; i++) {
            Node* node = getNodeAtPos(pos);

            const float epsilon = 1e-5f;
            float increment = svoSize / (float)exp2(node->depth);

            // Calculate integer voxel coordinate and center
            ivec3 voxelCoord = ivec3(glm::floor(glm::clamp(pos, 0.0f, svoSize - epsilon) / increment));
            vec3 voxelCenter = (vec3(voxelCoord) + 0.5f) * increment;

            if (node->isLeaf) {
                intersection.voxelPos = voxelCenter;
                intersection.normal = normal;
                intersection.color = node->color;
                return true;
            }
            if (!(pos.x >= 0.0f && pos.x <= svoSize &&
                pos.y >= 0.0f && pos.y <= svoSize &&
                pos.z >= 0.0f && pos.z <= svoSize)) {
                return false;
            }

            vec3 gridPos;

            // Either rounds the position up or down to nearest grid if direction is positive or negative.
            ceilOrFloor(d.x, &gridPos.x, pos.x, increment);
            ceilOrFloor(d.y, &gridPos.y, pos.y, increment);
            ceilOrFloor(d.z, &gridPos.z, pos.z, increment);

            // Gets amount of "steps" of d in each direction to get to point
            float xDist = abs(safeDiv(gridPos.x - pos.x, d.x));
            float yDist = abs(safeDiv(gridPos.y - pos.y, d.y));
            float zDist = abs(safeDiv(gridPos.z - pos.z, d.z));

            float closestAxisDist = min({ xDist, yDist, zDist });

            // Use epsilon-safe comparisons for normal direction
            if (abs(closestAxisDist - xDist) < epsilon) {
                normal = d.x < 0 ? vec3(1, 0, 0) : vec3(-1, 0, 0);
            }
            else if (abs(closestAxisDist - yDist) < epsilon) {
                normal = d.y < 0 ? vec3(0, 1, 0) : vec3(0, -1, 0);
            }
            else {
                normal = d.z < 0 ? vec3(0, 0, 1) : vec3(0, 0, -1);
            }

            float stepEpsilon = increment * 1e-5f;

            const float faceBias = 1e-4f;
            vec3 step = d * (closestAxisDist + stepEpsilon);
            pos = pos + step - normal * faceBias;
        }
        return false;
    }

    void ceilOrFloor(float dVal, float* gridVal, float camVal, float increment) {
        if (dVal > 0) {
            *gridVal = ceilToDec(camVal, increment);
        }
        else {
            *gridVal = floorToDec(camVal, increment);
        }
    }

    float floorToDec(float value, float decimal) {
        return floor(value * (1 / decimal)) / (1 / decimal);
    }

    float roundToDec(float value, float decimal) {
        return round(value * (1 / decimal)) / (1 / decimal);
    }

    float ceilToDec(float value, float decimal) {
        return ceil(value * (1 / decimal)) / (1 / decimal);
    }

    void insert(vec3 point, vec3 color) {
        //cout << root;
        Node* node = getNodeAtPos(point);
        int depth = 0;
        if (!node || (node && !node->isLeaf)) {
            insertNode(root, point, ivec3(0), color, 0);
        }
    }

    vector<FlatNode> toFlatArray() {
        vector<FlatNode> flatNodes;
        flattenSVO(root, flatNodes);
        return flatNodes;
    }

    vector<uint64_t> toFlatIntArray() {
        vector<FlatNode> flatArray = toFlatArray();
        vector<uint64_t> flatIntArray;
        for (int i = 0; i < flatArray.size(); i++) {
            uint64_t node = (uint64_t(flatArray[i].color) << 56) |
                (uint64_t(flatArray[i].childMask) << 48) |
                (uint64_t(flatArray[i].isLeaf ? 1 : 0) << 47) |
                (uint64_t(flatArray[i].firstChildIndex) & 0x00FFFFFF);
            flatIntArray.push_back(node);
        }
        return flatIntArray;
    }
};