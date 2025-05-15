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
#include "../SparseVoxelOctree.cpp"

using glm::vec3;
using glm::ivec3;
using namespace std;

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
    //printFlatSVO(svoArray);

    vector<uint32_t> flatIntArray = svo.toFlatIntArray();

    //for (int i = 0; i < svoArray.size(); i++) {
    //    cout << svoArray[i] << ", ";
    //}

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "DH2323 Project", NULL, NULL);
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
    string src = readShaderSource("C:/Users/willi/source/repos/DH2323Project/DH2323Project/3D-Renderer/vertex.vert");
    vertexShader = compileShader(GL_VERTEX_SHADER, src);

    // Compiling two fragment shaders with different colors
    GLuint fragmentShader;
    string fragSrc = readShaderSource("C:/Users/willi/source/repos/DH2323Project/DH2323Project/3D-Renderer/fragment.frag");
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