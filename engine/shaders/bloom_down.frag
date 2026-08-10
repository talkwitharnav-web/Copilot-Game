#version 450

// Call of Duty's 13-tap downsample, as presented in "Next Generation Post
// Processing in Call of Duty: Advanced Warfare". The extra taps over a plain
// box filter are what stop the pyramid pulsing as the camera moves: a naive
// halving samples a different set of texels every frame and the bloom shimmers.
//
// There is no brightness threshold anywhere in this chain. A threshold is the
// cause of the "everything glows like neon" look, because every pixel above the
// bar gets the same halo and a torch ends up looking like the sun.

#include "post_common.glsl"

layout(location = 0) in vec2 fragUv;
layout(set = 0, binding = 0) uniform sampler2D source;
layout(location = 0) out vec4 outColor;

vec3 at(vec2 uv) {
    return texture(source, uv).rgb;
}

void main() {
    float x = post.filterParams.x;
    float y = post.filterParams.y;

    vec3 a = at(fragUv + vec2(-2.0 * x,  2.0 * y));
    vec3 b = at(fragUv + vec2( 0.0,      2.0 * y));
    vec3 c = at(fragUv + vec2( 2.0 * x,  2.0 * y));
    vec3 d = at(fragUv + vec2(-2.0 * x,  0.0));
    vec3 e = at(fragUv);
    vec3 f = at(fragUv + vec2( 2.0 * x,  0.0));
    vec3 g = at(fragUv + vec2(-2.0 * x, -2.0 * y));
    vec3 h = at(fragUv + vec2( 0.0,     -2.0 * y));
    vec3 i = at(fragUv + vec2( 2.0 * x, -2.0 * y));

    vec3 j = at(fragUv + vec2(-x,  y));
    vec3 k = at(fragUv + vec2( x,  y));
    vec3 l = at(fragUv + vec2(-x, -y));
    vec3 m = at(fragUv + vec2( x, -y));

    // Weights sum to exactly 1, so the pyramid neither gains nor loses energy.
    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;

    outColor = vec4(max(result, vec3(0.0)), 1.0);
}
