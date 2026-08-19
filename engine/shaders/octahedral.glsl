// Turns a unit normal into two numbers and back.
//
// Octahedral mapping: fold the sphere onto an octahedron, unfold that into a
// square. It costs a few instructions and is accurate to a fraction of a degree
// at eight bits a component, which is far below anything the eye finds on a
// diffuse surface.
//
// The six axis directions a voxel face can take land on 0, 0.5 or 1 in this
// encoding. Only 0.5 is not exactly representable in eight bits, so a face
// normal comes back slightly off - **measured, not estimated: 0.3203 degrees
// for +Z, 0.3190 for a blade at forty-five degrees, and 0.9302 degrees for the
// worst direction anywhere on the sphere.** An earlier note said "up to a
// quarter of a degree", which understated the very case it named. Deliberately
// accepted, in exchange for one uniform path that also carries a rotated
// creature limb and a plant blade standing at forty-five degrees - all three
// figures are far below anything the eye finds on a diffuse surface.

vec2 octEncodeNormal(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 p = n.xy;
    if (n.z < 0.0) {
        p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    return p * 0.5 + 0.5;
}

vec3 octDecodeNormal(vec2 encoded) {
    vec2 f = encoded * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
