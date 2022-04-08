#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform float fTime;
uniform float fStrength; // Unused for now?
uniform vec2  vResolution;

out vec4 finalColor;

mat4 bayer = mat4(15,135,45,165,195,75,225,105,60,180,30,150,240,120,210,90);

void main() {
    vec4 t = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    float b = 0.299 * t.r + 0.587 * t.g + 0.114 * t.b;
    int x = int(fragTexCoord.x * vResolution.x) % 4;
    int y = int(fragTexCoord.y * vResolution.y) % 4;
    float th = bayer[y][x] / 255.0f;
    float c = ceil(b-th);

    finalColor = vec4(c, c, c, 1);
}

