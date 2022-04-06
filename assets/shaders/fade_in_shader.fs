#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D texture1; // For effects?

uniform vec4 colDiffuse;

uniform float fTime;
uniform float fStrength;
uniform vec2  vResolution;

out vec4 finalColor;

void main() {
    vec4 t = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    finalColor = vec4(t.r, t.g, t.b, fStrength*t.a);
}
