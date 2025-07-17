#version 440
layout(location = 0) in vec4 qt_VertexPosition;
layout(location = 1) in vec2 qt_VertexTexCoord;
layout(location = 0) out vec2 qt_TexCoord0;
out gl_PerVertex {
    vec4 gl_Position;
};
void main() {
    gl_Position = qt_VertexPosition;
    qt_TexCoord0 = qt_VertexTexCoord;
}
