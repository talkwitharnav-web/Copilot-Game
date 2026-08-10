#version 450

// One oversized triangle covering the screen, built from gl_VertexIndex alone -
// no vertex buffer, no index buffer, three vertices.
//
// Two triangles would also cover it, but every pixel along their shared diagonal
// is rasterised twice as a helper invocation. One triangle has no interior edge.

layout(location = 0) out vec2 fragUv;

void main() {
    fragUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUv * 2.0 - 1.0, 0.0, 1.0);
}
