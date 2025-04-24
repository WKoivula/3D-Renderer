#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using glm::vec3;
using glm::ivec3;
using namespace std;

struct VoxelData {
    vec3 color;
};

struct Node {
    bool isLeaf;
    Node *children[8];
    VoxelData data;
};

struct FlatNode {
    uint8_t childMask;
    uint32_t firstChildIndex;
};

/*
* Class containing the SVO data structure for the voxel world.
*/
class SparseVoxelOctree {
    private:
        int svoSize;
        int maxDepth;
        Node* root;

        void insertNode(Node*& node, vec3 point, ivec3 pos, int depth) {
            if (!node) {
                node = new Node;
            }

            if (depth == maxDepth) {
                node->isLeaf = true;
                return;
            }

            float size = svoSize / exp2(depth);
            ivec3 childPos;
            childPos.x = point.x >= (pos.x * size) + (size / 2.f);
            childPos.y = point.y >= (pos.y * size) + (size / 2.f);
            childPos.z = point.z >= (pos.z * size) + (size / 2.f);

            int childIndex = (childPos.x << 0) | (childPos.y << 0) | (childPos.z << 0);

            pos.x = pos.x << 1 | childPos.x;
            pos.y = pos.y << 1 | childPos.y;
            pos.z = pos.z << 1 | childPos.z;

            insertNode(node->children[childIndex], pos, pos, depth + 1);
        }
    public:
        SparseVoxelOctree(int svoSize, int maxDepth)
            : svoSize(svoSize), maxDepth(maxDepth), root(nullptr) {}

        void insert(vec3 point) {
            insertNode(root, point, ivec3(0), 0);
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

SparseVoxelOctree createSVO() {
    SparseVoxelOctree svo(64, 4);
    vec3 point(10.5f, 20.0f, 5.0f);
    svo.insert(point);

    vec3 point1(2.f, 2.f, 2.f);
    svo.insert(point1);

    return svo;
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