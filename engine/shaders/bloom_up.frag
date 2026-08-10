#version 450

// The 9-tap tent filter that walks the pyramid back up, blending each coarse
// mip into the finer one below it.
//
// The blend is **additive at the pipeline**, with the weight applied here rather
// than through the fragment's alpha. The bloom chain is R11G11B10, which has no
// alpha channel at all, so a SRC_ALPHA blend factor would be relying on
// behaviour that is easy to get subtly wrong and impossible to see.

#include "post_common.glsl"

layout(location = 0) in vec2 fragUv;
layout(set = 0, binding = 0) uniform sampler2D source;
layout(location = 0) out vec4 outColor;

vec3 at(vec2 uv) {
    return texture(source, uv).rgb;
}

void main() {
    float r = post.filterParams.z;

    vec3 a = at(fragUv + vec2(-r,  r));
    vec3 b = at(fragUv + vec2(0.0, r));
    vec3 c = at(fragUv + vec2( r,  r));
    vec3 d = at(fragUv + vec2(-r, 0.0));
    vec3 e = at(fragUv);
    vec3 f = at(fragUv + vec2( r, 0.0));
    vec3 g = at(fragUv + vec2(-r, -r));
    vec3 h = at(fragUv + vec2(0.0, -r));
    vec3 i = at(fragUv + vec2( r, -r));

    vec3 result = e * 4.0;
    result += (b + d + f + h) * 2.0;
    result += (a + c + g + i);
    result *= 1.0 / 16.0;

    outColor = vec4(max(result, vec3(0.0)) * post.filterParams.w, 1.0);
}
