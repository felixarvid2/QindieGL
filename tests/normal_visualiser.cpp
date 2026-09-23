
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>

#include "normals.h"

//#pragma comment(lib, "glfw3.lib")
//#pragma comment(lib, "opengl32.lib")

//#define MAX_VERTEXES 10000
//#define MAX_INDEXES 30000
//
//struct vertexData_s {
//    float xyz[3];
//    float normal[3];
//};
//
//struct drawbuff_s {
//    int numVertexes;
//    int numIndexes;
//    struct vertexData_s vertexes[MAX_VERTEXES];
//    unsigned short indexes[MAX_INDEXES];
//};

#define MAX_MESHES 10

// --- Global Camera State ---
float camDist = 5.0f;
float camPosX = 0.0f;
float camPosY = 0.0f;
float camPosZ = 0.0f;
float camRotX = 0.0f;
float camRotY = 0.0f;
double lastMouseX, lastMouseY;
bool isDragging_rotate = false;
bool isDragging_translate = false;
float normalScale = 1.0f;

// --- Mesh Offset State ---
float meshOffsetX = 0.0f;
float meshOffsetY = 0.0f;
float meshOffsetZ = 0.0f;

// --- Input Callbacks ---
static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    camPosZ -= (float)yoffset * 10.0f;
    //if (camPosZ < 0.1f) camPosZ = 0.1f;
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        isDragging_rotate = (action == GLFW_PRESS);
        glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        isDragging_translate = (action == GLFW_PRESS);
        glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
    }
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (isDragging_rotate) {
        camRotY += (float)(xpos - lastMouseX) * 0.5f;
        camRotX += (float)(ypos - lastMouseY) * 0.5f;
        lastMouseX = xpos;
        lastMouseY = ypos;
    }
    if (isDragging_translate) {
        camPosX += (float)(xpos - lastMouseX) * 1.0f;
        camPosY -= (float)(ypos - lastMouseY) * 1.0f;
        lastMouseX = xpos;
        lastMouseY = ypos;
    }
}

// Variable to store the last pressed number (0-9). -1 indicates no key pressed yet.
static int g_numberKey = 0;
static bool g_numberKeychanged = false;

// The key callback function
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // Only capture the initial press, ignoring holding the key down (GLFW_REPEAT) 
    // or letting it go (GLFW_RELEASE)
    if (action == GLFW_PRESS) {
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
            // Subtracting GLFW_KEY_0 converts the key code directly to an integer 0-9
            g_numberKey = key - GLFW_KEY_0;
            g_numberKeychanged = true;
        }
        else if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9) {
            g_numberKey = key - GLFW_KEY_KP_0;
            g_numberKeychanged = true;
        }
    }
}

// --- Mesh Processing ---
static void CenterMeshAndCalculateScale(drawbuff_s* mesh) {
    if (!mesh || mesh->numVertexes == 0)
        return;

    // 1. Calculate AABB
    float minB[3] = { mesh->vertexes[0].xyz[0], mesh->vertexes[0].xyz[1], mesh->vertexes[0].xyz[2] };
    float maxB[3] = { minB[0], minB[1], minB[2] };

    for (int i = 1; i < mesh->numVertexes; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (mesh->vertexes[i].xyz[j] < minB[j]) minB[j] = mesh->vertexes[i].xyz[j];
            if (mesh->vertexes[i].xyz[j] > maxB[j]) maxB[j] = mesh->vertexes[i].xyz[j];
        }
    }

    // 2. Store Center as Translation Offset (DO NOT modify vertices)
    meshOffsetX = (minB[0] + maxB[0]) / 2.0f;
    meshOffsetY = (minB[1] + maxB[1]) / 2.0f;
    meshOffsetZ = (minB[2] + maxB[2]) / 2.0f;

    // 3. Calculate max extent relative to the new center
    float maxExtent = 0.0f;
    for (int i = 0; i < mesh->numVertexes; ++i) {
        float dx = std::abs(mesh->vertexes[i].xyz[0] - meshOffsetX);
        float dy = std::abs(mesh->vertexes[i].xyz[1] - meshOffsetY);
        float dz = std::abs(mesh->vertexes[i].xyz[2] - meshOffsetZ);

        if (dx > maxExtent) maxExtent = dx;
        if (dy > maxExtent) maxExtent = dy;
        if (dz > maxExtent) maxExtent = dz;
    }

    // 4. Set camera distance and normal line length based on mesh size
    camDist = maxExtent * 2.5f;
    normalScale = maxExtent * 0.1f;
    if (normalScale == 0.0f) normalScale = 1.0f;
}

int run_visualiser(drawbuff_s *mesh[10]) {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(800, 600, "Mesh Visualizer", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetKeyCallback(window, key_callback);


    // Process the mesh bounding box
    CenterMeshAndCalculateScale(mesh[g_numberKey]);

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (g_numberKeychanged)
        {
            g_numberKeychanged = false;
            if (mesh[g_numberKey])
            {
                CenterMeshAndCalculateScale(mesh[g_numberKey]);
            }
            else
            {
                g_numberKey = 0;
            }
        }

        // --- Projection Matrix ---
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float aspect = (float)width / (float)(height ? height : 1);
        float fH = std::tan(45.0f / 360.0f * 3.14159f) * 0.1f;
        float fW = fH * aspect;
        glFrustum(-fW, fW, -fH, fH, 0.1f, 3000.0f);

        // --- ModelView Matrix (Camera) ---
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(camPosX, camPosY, -camDist - camPosZ);
        glRotatef(camRotX, 1.0f, 0.0f, 0.0f);
        glRotatef(camRotY, 0.0f, 1.0f, 0.0f);

        // 2. --- QUAKE 2 FLIP MATRIX ---
        // Pitch -90: Stands the model upright (rotates Quake's Z-axis to OpenGL's Y-axis)
        glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
        // Yaw -90: Faces the model towards the camera (rotates Quake's X-axis to OpenGL's -Z-axis)
        glRotatef(-90.0f, 0.0f, 0.0f, 1.0f);

        // 4. --- MESH CENTERING OFFSET ---
        // Push the mesh in the negative direction of its center to lock it to 0,0,0 locally
        glTranslatef(-meshOffsetX, -meshOffsetY, -meshOffsetZ);

        // --- Lighting ---
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);

        // W = 0.0f makes it a distant/directional light
        float lightDir[] = { 0.5f, 1.0f, 1.0f, 0.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, lightDir);

        // --- Draw Mesh (Shaded) ---
        if (mesh[g_numberKey]->numVertexes > 0 && mesh[g_numberKey]->numIndexes > 0) {
            glColor3f(0.7f, 0.7f, 0.7f); // Base mesh color
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_NORMAL_ARRAY);

            glVertexPointer(3, GL_FLOAT, sizeof(vertexData_s), mesh[g_numberKey]->vertexes[0].xyz);
            glNormalPointer(GL_FLOAT, sizeof(vertexData_s), mesh[g_numberKey]->vertexes[0].normal);

            glDrawElements(GL_TRIANGLES, mesh[g_numberKey]->numIndexes, GL_UNSIGNED_SHORT, mesh[g_numberKey]->indexes);

            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
        }

        // --- Draw Normals ---
        glDisable(GL_LIGHTING); // Turn off shading for debug lines
        glColor3f(1.0f, 1.0f, 0.0f); // Yellow lines
        glBegin(GL_LINES);
        for (int i = 0; i < mesh[g_numberKey]->numVertexes; ++i) {
            glVertex3fv(mesh[g_numberKey]->vertexes[i].xyz);
            glVertex3f(
                mesh[g_numberKey]->vertexes[i].xyz[0] + mesh[g_numberKey]->vertexes[i].normal[0] * normalScale,
                mesh[g_numberKey]->vertexes[i].xyz[1] + mesh[g_numberKey]->vertexes[i].normal[1] * normalScale,
                mesh[g_numberKey]->vertexes[i].xyz[2] + mesh[g_numberKey]->vertexes[i].normal[2] * normalScale
            );
        }
        glEnd();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}
