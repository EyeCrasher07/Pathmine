#version 450

layout(set = 0, binding = 0)
uniform sampler2DArray s_texture;

layout(location = 0) in  vec2 i_pos;
layout(location = 0) out vec4 o_color;

layout(push_constant)
uniform push_block {
  vec3 p_src_coord0;
  vec3 p_src_coord1;
  uint p_layer_count;
};

void main() {
  vec2 coord = mix(p_src_coord0.xy, p_src_coord1.xy, i_pos);
  // NV-DXVK start: Layer is not written by the single-layer fullscreen shader.
  int layer = p_layer_count == 1 ? 0 : gl_Layer;
  o_color = texture(s_texture, vec3(coord, layer));
  // NV-DXVK end
}
