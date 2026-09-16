#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedo;

void main() {
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(ubo.lightDir.xyz);
    float wrap = 0.15;
    float diffuse = max(dot(n, l) + wrap, 0.0) / (1.0 + wrap);
    float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 3.0);
    vec3 base = texture(albedo, fragUV).rgb;
    vec3 lit = base * (0.22 + 0.78 * diffuse) + base * rim * 0.12;
    outColor = vec4(lit, 1.0);
}
