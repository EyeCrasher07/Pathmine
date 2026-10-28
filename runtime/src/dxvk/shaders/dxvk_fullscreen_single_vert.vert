#version 450

// NV-DXVK start: The fullscreen blit shader without Layer output for single-layer framebuffers.
layout(location = 0) out vec2 o_texcoord;

void main() {
  vec2 coord = vec2(
    float(gl_VertexIndex & 2),
    float(gl_VertexIndex & 1) * 2.0f);

  o_texcoord  = coord;
  gl_Position = vec4(-1.0f + 2.0f * coord, 0.0f, 1.0f);
}
// NV-DXVK end
