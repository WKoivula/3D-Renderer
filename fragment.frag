#version 330 core

out vec4 FragColor;

void main()
{
    // Remove the 'f' suffix for floating-point literals
    FragColor = vec4(1.0, 0.5, 0.2, 1.0);
}
