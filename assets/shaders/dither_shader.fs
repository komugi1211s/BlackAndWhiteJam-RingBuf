precision mediump float;

varying vec2 fragTexCoord;
varying vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform float fTime;
uniform float fStrength; // Unused for now?
uniform vec2  vResolution;

// out vec4 finalColor;

mat4 bayer = mat4(15,135,45,165,195,75,225,105,60,180,30,150,240,120,210,90);

float get_threshold(int y, int x) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if ((i == y) && (j == x)) {
                return bayer[i][j];
            }
        }
    }
}

void main() {
    vec4 t = texture2D(texture0, fragTexCoord) * colDiffuse * fragColor;
    float b = 0.299 * t.r + 0.587 * t.g + 0.114 * t.b;
    int x = int(mod(fragTexCoord.x * vResolution.x, 4.0));
    int y = int(mod(fragTexCoord.y * vResolution.y, 4.0));

    float th = get_threshold(y, x) / 255.0;
    float c = ceil(b - th);
    gl_FragColor = vec4(c, c, c, t.a);
}

