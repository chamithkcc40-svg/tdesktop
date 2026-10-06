// build trigger
// Liquid Glass effect fragment shader.
//
// Ported to GLSL (Qt OpenGL) from the Android AGSL implementation
// (TMessagesProj/src/main/res/raw/liquid_glass_shader.agsl).
//
// AGSL -> GLSL changes:
//  - `half` / `half2` / `half3` / `half4` -> `float` / `vec2` / `vec3` / `vec4`
//  - `uniform shader img` -> `uniform sampler2D img`
//  - `img.eval(uv)` (pixel coords) -> `texture2D(img, uv / resolution)`
//  - `main(in float2 fragCoord)` -> `gl_FragCoord` / `gl_FragColor`
//  - AGSL fragCoord has a top-left origin, gl_FragCoord is bottom-left,
//    so the Y coordinate is flipped to keep identical semantics.

#ifdef GL_ES
precision highp float;
#endif

uniform sampler2D img;

uniform vec2 resolution;
uniform vec2 center;
uniform vec2 size;
uniform vec4 radius;
uniform float thickness;
uniform float refract_index;
uniform float refract_intensity;
uniform vec4 foreground_color_premultiplied;

float sdfRect(vec2 p, vec4 r) {
  r.xy = (p.x > 0.0) ? r.xy : r.zw;
  r.x  = (p.y > 0.0) ? r.x  : r.y;
  vec2 q = abs(p) - size + r.x;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r.x;
}

vec4 srcOver(vec4 src, vec4 dst) {
  vec3 outRGB = (src.rgb + dst.rgb * (1.0 - src.a));
  float outA = src.a + (1.0 - src.a) * dst.a;
  return vec4(outRGB, outA);
}

void main() {
  // Flip Y: AGSL uses a top-left origin, OpenGL a bottom-left one.
  vec2 fragCoord = vec2(gl_FragCoord.x, resolution.y - gl_FragCoord.y);

  vec2 p = fragCoord - center;
  float sd = sdfRect(p, radius);
  vec2 uv = fragCoord;
  if (sd < 0.0) {
    float sdX = sdfRect(p + vec2(1.0, 0.0), radius);
    float sdY = sdfRect(p + vec2(0.0, 1.0), radius);

    float n_cos = max(thickness + sd, 0.0) / thickness;
    float n_cos2 = n_cos * n_cos;
    float n_sin = sqrt(1.0 - n_cos2);
    vec3 normal = normalize(vec3((sdX - sd) * n_cos, (sdY - sd) * n_cos, n_sin));

    vec3 refract_vec = refract(vec3(0.0, 0.0, -1.0), normal, 1.0 / refract_index);
    float h = (sd < -thickness) ? thickness : sqrt(sd * (-2.0 * thickness - sd));
    float refract_length = (h + 8.0 * thickness) / -refract_vec.z;

    uv += refract_vec.xy * refract_length * refract_intensity;
  }

  // img.eval() samples in pixel coordinates, texture2D() in normalized ones.
  vec2 texCoord = vec2(uv.x, resolution.y - uv.y) / resolution;
  gl_FragColor = srcOver(foreground_color_premultiplied, texture2D(img, texCoord));
}
