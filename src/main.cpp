#include <raylib.h>
#include <stdlib.h>

#define FUZZY_MY_H_IMPL
#include "my.h"

enum
{
    ACTION_NONE = 0,
    ACTION_ATTACK,
    ACTION_DEFEND,
    ACTION_PARRY,
    ACTION_RUSH,
    ACTION_COUNT,
};

struct Action {
    int type;
    int jump_to;
};

#define ACTION_CAPACITY 10
#define TILE 96

const Action base_actions[] = {
    { ACTION_ATTACK, 0 },
    { ACTION_DEFEND,  0 },
    { ACTION_PARRY,  0 },
    { ACTION_RUSH,   0 },
};

static float slot_strength[fz_COUNTOF(base_actions)];
static float slot_strength_target[fz_COUNTOF(base_actions)];

const char fragment_shader_for_invisibility[] = "#version 330\n"
"in vec2 fragTexCoord; in vec4 fragColor; \n"
"uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
"uniform float fTime; uniform float fStrength; \n"
"out vec4 finalColor; \n"
"float rand(vec2 n) { return fract(sin(dot(n.xy ,vec2(12.9898,78.233))) * 43758.5453); }\n"
"float noise(vec2 n) { return mix(rand(n), rand(n + 1), 0.2); }\n"
"void main() { float n = step(noise(fragTexCoord + fTime), fStrength * texture(texture0, fragTexCoord).a); finalColor = vec4(n, n, n, 1.0f); } \n";

int pixsort(const void *a, const void *b) {
    Color *ac = (Color *)a;
    Color *bc = (Color *)b;

    if (ac->r == bc->r) return 0;
    return (ac->r > bc->r) ? 1 : -1;
}

/*
 *   Solution for stage 1:
 *            1        2
 *   P        R        P
 * [ A, D, D, P, D, A, R, A, A, P ]
 *
 * [ D, A, A, A, D ]
 *
*/

/* Constants */
const float BASE_SHADER_EFFECT_THRESHOLD = 0.5;

/* Game globals */
static Rectangle window_size = { 0, 0, 800, 600 };
static float accumulator = 0;
static Vector2 mouse_pos = {};


static Vec(Action) action_queue;
static int execute_action_queue = 0;
static int global_action_index = 0;

/* Renderer globals */
static int time_loc     = -1;
static int strength_loc = -1;

static RenderTexture2D render_tex;

static Shader shader;
static Font   font;

/* Layouting */
enum
{
    TOP,
    MIDDLE,
    BOTTOM,

    LEFT,
    CENTER,
    RIGHT,
};


Vector2 position_of_pivot(Rectangle rect, int y_axis, int x_axis) {
    Vector2 result = {};

    switch(y_axis) {
        case TOP:    result.y = rect.y;                     break;
        case MIDDLE: result.y = rect.y + (rect.height / 2); break;
        case BOTTOM: result.y = rect.y + rect.height;       break;

        default: assert(0);
    }

    switch(x_axis) {
        case LEFT:   result.x = rect.x;                     break;
        case CENTER: result.x = rect.x + (rect.width / 2);  break;
        case RIGHT:  result.x = rect.x + rect.width;        break;

        default: assert(0);
    }

    return result;
}

Rectangle get_available_action_layout(void) {
    Rectangle rect = {};

    rect.width  = (TILE - 8) * fz_COUNTOF(base_actions);
    rect.height = (TILE - 8);

    rect.x = render_tex.texture.width  * 0.5  - TILE * fz_COUNTOF(base_actions) * 0.5;
    rect.y = render_tex.texture.height * 0.9 - rect.height * 0.5;

    return rect;
}

Rectangle get_action_queue_layout(void) {
    Rectangle queued_layout = {};
    queued_layout.width  = ACTION_CAPACITY * (TILE - 8);
    queued_layout.height = (TILE - 8);

    queued_layout.x = render_tex.texture.width * 0.5  - (ACTION_CAPACITY * TILE / 2);
    queued_layout.y = render_tex.texture.height * 0.75 - (queued_layout.height / 2);

    return queued_layout;
}

Rectangle get_rowslot_for_nth_tile(Rectangle layout, int index, int margin) {
    Rectangle rect = {0};
    rect.width  = TILE - margin;
    rect.height = TILE - margin;

    rect.x = layout.x + (TILE * index) + (margin / 2);
    rect.y = layout.y;
    return rect;
}

void update(float dt) {
    while (accumulator > 0.16) accumulator -= 0.16;
    if (accumulator < 0)       accumulator = 0;

    Vector2 screen_pos = GetMousePosition();
    mouse_pos = { screen_pos.x * 2.0f, screen_pos.y * 2.0f };

    /* animate tweens */
    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        float target      = slot_strength_target[i];
        slot_strength[i] += (target - slot_strength[i]) * 0.02;
    }

    /* Update available action / push scheme */
    {
        Rectangle layout = get_available_action_layout();
        int selected = -1;

        for (int i = 0; i < fz_COUNTOF(base_actions); ++i) {
            Rectangle r = get_rowslot_for_nth_tile(layout, i, 0);
            slot_strength_target[i] = 0.75;

            if (CheckCollisionPointRec(mouse_pos, r)) {
                selected = i;
                slot_strength_target[i] = 1;
            }
        }

        if (selected != -1 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Action a = base_actions[selected];

            if (VecLen(action_queue) < VecCap(action_queue)) {
                VecPush(action_queue, a);
            }
        }
    }

    /* Update active action / select & delete scheme */

    {
        Rectangle queued_layout = get_action_queue_layout();
        size_t queue_length = VecLen(action_queue);

        for (int i = 0; i < queue_length; ++i) {
            Rectangle r = get_rowslot_for_nth_tile(queued_layout, i, 8);

            if (CheckCollisionPointRec(mouse_pos, r) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                VecRemoveN(action_queue, i);
            }
        }
    }
}

void render(void) {
    BeginTextureMode(render_tex);

    ClearBackground(BLACK);
    {
        SetShaderValue(shader, time_loc, &accumulator, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, strength_loc, &BASE_SHADER_EFFECT_THRESHOLD, SHADER_UNIFORM_FLOAT);

        Rectangle player = {};
        player.width  = 1 * TILE;
        player.height = 2 * TILE;
        player.x = render_tex.texture.width * 0.25 - (player.width  / 2);
        player.y = render_tex.texture.height * 0.5 - (player.height / 2);

        DrawRectangleRec(player, WHITE);
        Rectangle layout = get_available_action_layout();

        /*
         * Render Base Actions
         **/
        for (int i = 0; i < fz_COUNTOF(base_actions); ++i) {
            Action a = base_actions[i];
            Rectangle r = get_rowslot_for_nth_tile(layout, i, 0);

            char chara[2];
            chara[0] = '0' + a.type;
            chara[1] = '\0';

            float w = MeasureTextEx(font, chara, TILE, 0).x;

            Vector2 pos = position_of_pivot(r, MIDDLE, CENTER);
            pos.x -= w / 2;

            DrawTextEx(font, chara, pos, TILE, 0, WHITE);

            BeginShaderMode(shader);
            SetShaderValue(shader, strength_loc, &slot_strength[i], SHADER_UNIFORM_FLOAT);
            DrawRectangleLinesEx(r, 1, WHITE);
            EndShaderMode();
        }

        /* Reset shader options */
        SetShaderValue(shader, strength_loc, &BASE_SHADER_EFFECT_THRESHOLD, SHADER_UNIFORM_FLOAT);

        /*
         * Render Active Action list
         */
        Rectangle queued_layout = get_action_queue_layout();

        for (int i = 0; i < VecCap(action_queue); ++i) {
            if (i >= VecLen(action_queue)) BeginShaderMode(shader);
            Rectangle r = get_rowslot_for_nth_tile(queued_layout, i, 8);

            if (i < VecLen(action_queue)) {
                Action a = action_queue[i];

                if (a.type == ACTION_PARRY) {
                    int jump_to = a.jump_to;
                    Rectangle jump_to_rect = get_rowslot_for_nth_tile(queued_layout, jump_to, 8);

                    Vector2 jump_to_pos   = position_of_pivot(jump_to_rect, TOP, CENTER);
                    Vector2 jump_from_pos = position_of_pivot(r, TOP, CENTER);

                    Vector2 to_control = jump_to_pos, from_control = jump_from_pos;
                    to_control.y -= queued_layout.height;
                    from_control.y -= queued_layout.height;

                    DrawLineBezierCubic(jump_from_pos, jump_to_pos,
                                        from_control, to_control, 2, WHITE);
                }

                char chara[2];
                chara[0] = '0' + a.type;
                chara[1] = '\0';

                float w = MeasureTextEx(font, chara, TILE, 0).x;
                Vector2 pos = { r.x + (r.width / 2) - w / 2, r.y };
                DrawTextEx(font, chara, pos, TILE, 0, WHITE);
            }

            DrawRectangleLinesEx(r, 1, WHITE);
            if (i >= VecLen(action_queue)) EndShaderMode();
        }


        const char *format = TextFormat("%d / %d", (int)VecLen(action_queue), (int)VecCap(action_queue));
        float w = MeasureText(format, 18);
        DrawText(format, queued_layout.x + (VecCap(action_queue) * TILE * 0.5) - (w * 0.5), queued_layout.y - 18 - 2, 18, WHITE);
    }

    EndTextureMode();
}

int main(void) {
    window_size = { 0, 0, 800, 600 };
    InitWindow(window_size.width, window_size.height, "MainWindow");

    action_queue = VecCreate(Action, ACTION_CAPACITY);
    shader = LoadShaderFromMemory(0, fragment_shader_for_invisibility);
    font = LoadFontEx("assets/fonts/EBGaramond-Regular.ttf", TILE, 0, 0);
    time_loc     = GetShaderLocation(shader, "fTime");
    strength_loc = GetShaderLocation(shader, "fStrength");

    Rectangle render_rect = { 0, 0, window_size.width * 2.0f, window_size.height * 2.0f };

    render_tex = LoadRenderTexture(render_rect.width, render_rect.height);
    SetTextureFilter(render_tex.texture, TEXTURE_FILTER_BILINEAR);


    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        slot_strength[i] = slot_strength_target[i] = 0.5;
    }

    while(!WindowShouldClose()) {
        float dtime  = GetFrameTime();
        accumulator += dtime;

        update(dtime);
        render();

        BeginDrawing();
        ClearBackground(BLACK);
            Rectangle swapped = render_rect;
            swapped.height *= -1;
            Vector2 offset = {};
            DrawTexturePro(render_tex.texture, swapped, window_size, offset, 0, WHITE);
        EndDrawing();
    }

    UnloadFont(font);
    UnloadRenderTexture(render_tex);
    UnloadShader(shader);
    VecRelease(action_queue);
    CloseWindow();
    return 0;
}
