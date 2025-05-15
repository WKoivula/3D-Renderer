#version 430 core

layout(std430, binding = 0) buffer DataBuffer {
    uvec2 nodes[];  // Dynamically sized array of uints
};

out vec4 FragColor;

uint getChildMask(uvec2 node) {
    return node.y & 0xFFu;
}

uint getChildBase(uvec2 node) {
    return node.x & 0x00FFFFFFu;
}

bool hasChild(uint mask, int index) {
    return (mask & (1u << index)) != 0u;
}

bool isLeaf(uvec2 node) {
    return ((node.x >> 31u) & 0x1u) != 0u;
}

vec3 getColor(uvec2 node) {
    uint rawColor = (node.y >> 8u) & 0xFFFFFFu;
    return vec3(
        float((rawColor >> 16u) & 0xFFu),
        float((rawColor >> 8u) & 0xFFu),
        float(rawColor & 0xFFu)
    ) / 255.0;
}

struct DecodedNode {
    vec3 color;
    uint childMask;
    bool isLeaf;
    uint firstChildIndex;
    uint depth;
};

struct Intersection {
    bool intersected;
    vec3 normal;
    vec3 voxelPos;
    vec3 color;
};

DecodedNode decodeNode(uvec2 node) {
    DecodedNode n;
    n.color = getColor(node);
    n.childMask = getChildMask(node);
    n.isLeaf = isLeaf(node);
    n.firstChildIndex = getChildBase(node);
    return n;
}

float floorToDec(float value, float decimal) {
    return floor(value * (1 / decimal)) / (1 / decimal);
}

float ceilToDec(float value, float decimal) {
    return ceil(value * (1 / decimal)) / (1 / decimal);
}

float ceilOrFloor(float dVal, float camVal, float increment) {
    if (dVal > 0) {
        return ceilToDec(camVal, increment);
    }
    else {
        return floorToDec(camVal, increment);
    }
}

float safeDiv(float a, float b) {
    const float tiny = 1e-6;
    float safeB = (abs(b) < tiny) ? (b >= 0.0 ? tiny : -tiny) : b;
    return a / safeB;
}

DecodedNode getNodeAtPos(vec3 pos, uvec2 root) {
    int depth = 0;
    vec3 offset = vec3(0.0, 0.0, 0.0);
    uvec2 node = root;

    while (true) {
        float nodeSize = 1 / float(1 << depth);
        vec3 center = offset + vec3(nodeSize, nodeSize, nodeSize) * 0.5;

        ivec3 childPos;
        childPos.x = (pos.x >= center.x) ? 1 : 0;
        childPos.y = (pos.y >= center.y) ? 1 : 0;
        childPos.z = (pos.z >= center.z) ? 1 : 0;
        int childIndex = (childPos.x << 0) | (childPos.y << 1) | (childPos.z << 2);

        offset += vec3(childPos.x * nodeSize * 0.5,
                childPos.y * nodeSize * 0.5,
                childPos.z * nodeSize * 0.5);

        DecodedNode dNode = decodeNode(node);
        bool childExists = hasChild(dNode.childMask, childIndex);
        if (childExists) {
            uint flatIndex = dNode.firstChildIndex + childIndex;
            node = nodes[flatIndex];
            depth++;
        }
        else {
            break;
        }
    }
    DecodedNode dNode = decodeNode(node);
    dNode.depth = depth;
    return dNode;
}

Intersection closestIntersection (vec3 pos, vec3 d, uvec2 root) {
    int maxSteps = 20;
    vec3 normal = vec3(0.0, 0.0, 0.0);

    for (int i = 0; i < maxSteps; i++) {
        DecodedNode node = getNodeAtPos(pos, root);

        const float epsilon = 0.00001;
        float increment = 1 / float(2^(node.depth));

        ivec3 voxelCoord = ivec3(floor(clamp(pos, 0.0, 1 - epsilon)));
        vec3 voxelCenter = (vec3(voxelCoord) + 0.5) * increment;

        if (node.isLeaf) {
            Intersection intersection;
            intersection.voxelPos = voxelCenter;
            intersection.normal = normal;
            intersection.color = node.color;
        }
        if (!(pos.x >= 0.0f && pos.x <= 1 &&
            pos.y >= 0.0f && pos.y <= 1 &&
            pos.z >= 0.0f && pos.z <= 1)) {
            Intersection i;
            i.intersected = false;
            return i;
        }

        vec3 gridPos;

        gridPos.x = ceilOrFloor(d.x, pos.x, increment);
        gridPos.y = ceilOrFloor(d.y, pos.y, increment);
        gridPos.z = ceilOrFloor(d.z, pos.z, increment);

        float xDist = abs(safeDiv(gridPos.x - pos.x, d.x));
        float yDist = abs(safeDiv(gridPos.y - pos.y, d.y));
        float zDist = abs(safeDiv(gridPos.z - pos.z, d.z));

        float closestAxisDist = min(min(xDist, yDist), zDist);

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

        float stepEpsilon = increment * 0.00001;

        const float faceBias = 0.0001;
        vec3 worldStep = d * (closestAxisDist + stepEpsilon);
        pos = pos + worldStep - normal * faceBias;
    }
    Intersection i;
    i.intersected = false;
    return i;
}

void main()
{
    // Remove the 'f' suffix for floating-point literals
    FragColor = vec4(1.0, 0.5, 0.2, 1.0);
}
