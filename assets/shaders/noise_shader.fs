#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform float fTime;
uniform float fStrength;
uniform vec2 vResolution;

out vec4 finalColor;

float rand(vec2 n) {
    return fract(sin(dot(n.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

float noise(vec2 n) {
    return mix(rand(n), rand(n + 1), 0.5);
}

void main() {
    vec2 p = (fragTexCoord * vResolution) / (vec2(10.0f, 10.0f));
    vec2 seed = floor(p + vec2(fTime * 0.1, fTime* 0.1));
    float a = noise(seed);
    float n = smoothstep(0, 1, fStrength * a * texture(texture0, fragTexCoord).a);

    finalColor = vec4(n, n, n, 1.0f);
}
