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

    void flattenSVO(Node* node, vector<FlatNode>& flatNodes) {
        if (!node) return;

        FlatNode flatNode;
        flatNode.childMask = 0;
        flatNode.firstChildIndex = UINT32_MAX;

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

        while (node) {
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

    vector<uint32_t> toFlatIntArray() {
        vector<FlatNode> flatArray = toFlatArray();
        vector<uint32_t> flatIntArray;
        for (int i = 0; i < flatArray.size(); i++) {
            uint32_t node = (flatArray[i].childMask << 24) | (flatArray[i].firstChildIndex & 0x00FFFFFF);
            flatIntArray.push_back(node);
        }
        return flatIntArray;
    }
};

void createSphere(SparseVoxelOctree& svo, vec3 sphereCenter, float sphereDiameter, float loops) {
    for (int i = 0; i < loops; i++) {
        float x = ((i - loops / 2.0) / (loops / 2.0)) * sphereDiameter;
        for (int j = 0; j < loops; j++) {
            float y = ((j - loops / 2.0) / (loops / 2.0)) * sphereDiameter;
            for (int k = 0; k < loops; k++) {
                float z = ((k - loops / 2.0) / (loops / 2.0)) * sphereDiameter;
                vec3 sPoint(sphereCenter.x + x, sphereCenter.y + y, sphereCenter.z + z);
                if (glm::distance(sPoint, sphereCenter) < sphereDiameter) {
                    float r = ((double)rand() / (RAND_MAX));
                    float g = ((double)rand() / (RAND_MAX));
                    float b = ((double)rand() / (RAND_MAX));
                    svo.insert(sPoint, vec3(i / loops, j / loops, k / loops));
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// FUNCTIONS

void printFlatSVO(const vector<FlatNode>& flatNodes, int index = 0, int depth = 0) {
    if (index >= flatNodes.size()) return;

    const FlatNode& node = flatNodes[index];

    // Indentation
    for (int i = 0; i < depth; ++i) cout << "  ";

    // Print this node
    cout << "Node " << index << " | Mask: " << std::bitset<8>(node.childMask)
        << " | FirstChild: ";
    if (node.firstChildIndex == 0xFFFFFF)
        cout << "Leaf";
    else
        cout << node.firstChildIndex;
    cout << "\n";

    if (node.firstChildIndex == 0xFFFFFF) return; // Leaf node

    int childCount = 0;
    for (int i = 0; i < 8; ++i) {
        if (node.childMask & (1 << i)) {
            printFlatSVO(flatNodes, node.firstChildIndex + childCount, depth + 1);
            childCount++;
        }
    }
}

void createPerlinTerrain(SparseVoxelOctree& svo, float heightScaling) {
    std::cout << "---------------------------------\n";
    std::cout << "* frequency [0.1 .. 8.0 .. 64.0] \n";
    std::cout << "* octaves   [1 .. 8 .. 16]       \n";
    std::cout << "* seed      [0 .. 2^32-1]        \n";
    std::cout << "---------------------------------\n";

    double frequency;
    std::cout << "double frequency = ";
    std::cin >> frequency;
    frequency = glm::clamp(frequency, 0.1, 64.0);

    std::int32_t octaves;
    std::cout << "int32 octaves    = ";
    std::cin >> octaves;
    octaves = glm::clamp(octaves, 1, 16);

    std::uint32_t seed;
    std::cout << "uint32 seed      = ";
    std::cin >> seed;

    const siv::PerlinNoise perlin{ seed };
    const float voxelSize = svo.getSize() / (float)exp2(svo.getMaxDepth());
    const int width = 1 / voxelSize;
    cout << "Width: " << 1 / voxelSize << "\n";
    const double fx = (frequency / width);

    vec3 groundColor(0.46f, 0.64f, 0.38f);
    float step = voxelSize;


    for (int y = 0; y < width; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const double noise = perlin.octave2D_01((x * fx), (y * fx), octaves);

            for (int i = 0; i < 4; i++) {
                vec3 pos((float)x * voxelSize, (noise / heightScaling) - (voxelSize * i), (float)y * voxelSize);
                svo.insert(pos, groundColor);
            }
            //cout << "x: " << pos.x << ", y: " << pos.y << ", z:" << pos.z << "\n";
        }
    }
}

SparseVoxelOctree createSVO() {
    SparseVoxelOctree svo(1, 8);

    createPerlinTerrain(svo, 16.0f);

    vec3 red(1.0f, 0.0f, 0.0f);
    vec3 point(0.1f, 0.78f, 0.1f);
    svo.insert(point, red);

    vec3 point1(0.1f, 0.1f, 0.55f);
    svo.insert(point1, red);

    vec3 point2(0.7f, 0.1f, 0.35f);
    svo.insert(point2, red);

    return svo;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose (window, true);
    }
}

float vertices[] = {
    0.5f,  0.5f, 0.0f,
    0.5f, -0.5f, 0.0f,
    -0.5f, -0.5f, 0.0f,
    -0.5f,  0.5f, 0.0f,
};
unsigned int indices[] = {  // note that we start from 0!
    0, 1, 3,   // first triangle
    1, 2, 3    // second triangle
};

string readShaderSource(const std::string& filepath) {
    ifstream file(filepath);
    stringstream buffer;

    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << filepath << std::endl;
        return "";
    }

    buffer << file.rdbuf();
    return buffer.str();
}

GLuint compileShader(GLenum shaderType, const string& path) {
    GLuint shader = glCreateShader(shaderType);
    const char* src = path.c_str();
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint success;
    char infoLog[512];

    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    return shader;
}

GLuint createShaderProgram(GLuint vertexShader, GLuint fragmentShader) {
    GLint success;
    char infoLog[512];
    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    
    return shaderProgram;
}

int main()
{
    SparseVoxelOctree svo = createSVO();
    cout << "SVO created\n";
    vector<FlatNode> svoArray = svo.toFlatArray();
    printFlatSVO(svoArray);

    vector<uint32_t> flatIntArray = svo.toFlatIntArray();

    //for (int i = 0; i < svoArray.size(); i++) {
    //    cout << svoArray[i] << ", ";
    //}

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "LearnOpenGL", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glViewport(0, 0, 800, 600);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // Compiling vertex shader
    GLuint vertexShader;
    string src = readShaderSource("C:/Users/willi/source/repos/FirstOpenGL/FirstOpenGL/3D-Renderer/vertex.vert");
    vertexShader = compileShader(GL_VERTEX_SHADER, src);

    // Compiling two fragment shaders with different colors
    GLuint fragmentShader;
    string fragSrc = readShaderSource("C:/Users/willi/source/repos/FirstOpenGL/FirstOpenGL/3D-Renderer/fragment.frag");
    fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragSrc);

    // Link fragment and vertex shader into shader program
    GLuint shaderProgram;
    shaderProgram = createShaderProgram(vertexShader, fragmentShader);

    glUseProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLuint VBOs[2], VAOs[2];
    glGenVertexArrays(2, VAOs); // we can also generate multiple VAOs or buffers at the same time
    glGenBuffers(2, VBOs);

    GLuint EBO;
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAOs[0]);
    glBindBuffer(GL_ARRAY_BUFFER, VBOs[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    while (!glfwWindowShouldClose(window))
    {
        processInput(window);

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        glm::mat4 trans = glm::mat4(1.0f);
        trans = glm::rotate(trans, (float)glfwGetTime(), glm::vec3(0.0, 0.0, 1.0));

        unsigned int transformLoc = glGetUniformLocation(shaderProgram, "transform");
        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(trans));

        glBindVertexArray(VAOs[0]);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glDeleteVertexArrays(2, VAOs);
    glDeleteBuffers(2, VAOs);
    glDeleteProgram(shaderProgram);

    glfwTerminate();

    return 0;
}