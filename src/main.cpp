#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>

#define FUZZY_MY_H_IMPL
#include "my.h"

enum
{
    ACTION_NONE = 0,
    ACTION_SLASH,
    ACTION_EVADE,
    ACTION_PARRY,
    ACTION_TACKLE,
    ACTION_COUNT,
};

struct Action {
    int type;
    int jump_to;
};

#define ACTION_CAPACITY 10
#define TILE 96

const Action base_actions[] = {
    { ACTION_SLASH,  0 },
    { ACTION_EVADE,  0 },
    { ACTION_PARRY,  0 },
    { ACTION_TACKLE, 0 },
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
static Rectangle window_size = { 0, 0, 1600, 900 };
static float accumulator = 0;
static Vector2 mouse_pos = {};

/* Renderer globals */
static int time_loc     = -1;
static int strength_loc = -1;

static RenderTexture2D render_tex;

static Shader shader;
static Font   font;

struct Actor {
    int health;
    int max_health;
    int action_index;
    int action_count;
    Action actions[ACTION_CAPACITY];
};

#define ENEMY_CAPACITY 3
struct Game {
    int prev_phase;

    int player_act;
    Actor player;

    int enemy_idx;
    Actor enemies[ENEMY_CAPACITY];  
};

enum /* Combat State */
{
    COMBAT_PLAYER_WON,
    COMBAT_ENEMY_WON,
    COMBAT_PLAYER_CONQUERED,
    COMBAT_IN_PROGRESS,
};

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

void DEBUG_createphase(Game *game) {
    game->player.health = 3;
    game->player.max_health = 3;

    game->player.action_index = 0;
    game->player.action_count = 0;
    memset(game->player.actions, 0, sizeof(Action) * ACTION_CAPACITY);

    game->enemies[0].health = game->enemies[0].max_health = 3;
    game->enemies[1].health = game->enemies[1].max_health = 3;
    game->enemies[2].health = game->enemies[2].max_health = 3;

    game->enemies[0].action_index = game->enemies[0].action_count = 0;
    game->enemies[1].action_index = game->enemies[1].action_count = 0;
    game->enemies[2].action_index = game->enemies[2].action_count = 0;

    game->enemies[0].actions[game->enemies[0].action_count++].type = ACTION_SLASH;
    game->enemies[0].actions[game->enemies[0].action_count++].type = ACTION_PARRY;
    game->enemies[0].actions[game->enemies[0].action_count++].type = ACTION_TACKLE;
    game->enemies[0].actions[game->enemies[0].action_count++].type = ACTION_PARRY;

    game->enemies[1].actions[game->enemies[1].action_count++].type = ACTION_EVADE;
    game->enemies[1].actions[game->enemies[1].action_count++].type = ACTION_SLASH;
    game->enemies[1].actions[game->enemies[1].action_count++].type = ACTION_PARRY;
    game->enemies[1].actions[game->enemies[1].action_count++].type = ACTION_PARRY;

    game->enemies[2].actions[game->enemies[2].action_count++].type = ACTION_TACKLE;
    game->enemies[2].actions[game->enemies[2].action_count++].type = ACTION_SLASH;
    game->enemies[2].actions[game->enemies[2].action_count++].type = ACTION_EVADE;
    game->enemies[2].actions[game->enemies[2].action_count++].type = ACTION_EVADE;
}

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

    rect.x = render_tex.texture.width  * 0.5 - TILE * fz_COUNTOF(base_actions) * 0.5;
    rect.y = render_tex.texture.height * 0.9 - rect.height * 0.5;

    return rect;
}

Rectangle get_action_queue_layout(void) {
    Rectangle queued_layout = {};
    queued_layout.width  = ACTION_CAPACITY * (TILE - 8);
    queued_layout.height = (TILE - 8);

    queued_layout.x = render_tex.texture.width * 0.5   - (ACTION_CAPACITY * TILE / 2);
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

Action get_next_action_for(Actor *actor) {
    Action action = actor->actions[actor->action_index];
    actor->action_index = (actor->action_index + 1) % actor->action_count;

    return action;
}

void turn_tick(Game *game, float dt) {
    static float turn_timer = 0;
    turn_timer += dt;

    if (turn_timer > 0.5) {
        /* Exhaust the timer here so I can use early return */
        while (turn_timer > 0.5) turn_timer -= 0.5;
        if (turn_timer < 0)      turn_timer = 0;

        /*
        Do turn stuff.
        */
        /* Check if player / enemies are allowed to continue fighting. */

        if (game->player.health <= 0)          return; // Player is dead
        if (game->enemy_idx == ENEMY_CAPACITY) return; // Enemy index is same as enemy capacity: enemy is all dead

        if (game->player_act) {
            /* Do actual matchup! */

            Actor *enemy = &game->enemies[game->enemy_idx];
            if (enemy->health <= 0) { // Progress to next enemy then break
                game->enemy_idx++;
                return;
            }

            Action player_action = get_next_action_for(&game->player);
            Action enemy_action = get_next_action_for(enemy);

            switch(player_action.type) {
                /* Offensive Maneuver */
                case ACTION_SLASH: switch(enemy_action.type) {
                    case ACTION_PARRY:  /* blocks  */
                        break;

                    case ACTION_SLASH:  /* deals   */
                    case ACTION_EVADE:  /* deals   */
                    case ACTION_TACKLE: /* deals   */
                        enemy->health -= 1;
                } break;

                case ACTION_TACKLE: switch(enemy_action.type) {
                    case ACTION_EVADE:  /* blocked */
                         break;

                    case ACTION_PARRY:  /* deals   */
                    case ACTION_SLASH:  /* deals   */
                    case ACTION_TACKLE: /* deals   */
                        enemy->health -= 1;
                } break;

                /* Defensive Maneuver */
                case ACTION_PARRY: switch(enemy_action.type) {
                    case ACTION_PARRY:  /* nothing */
                    case ACTION_EVADE:  /* nothing */
                        break;

                    case ACTION_SLASH:  /* avoids  */
                        break; /* TODO: generate animation */

                    case ACTION_TACKLE: /* damaged */
                        game->player.health -= 1;
                } break;

                case ACTION_EVADE: switch(enemy_action.type) {
                    case ACTION_PARRY:  /* nothing */
                    case ACTION_EVADE:  /* nothing */
                        break;

                    case ACTION_TACKLE: /* avoids  */
                        break; /* TODO: generate animation */

                    case ACTION_SLASH:  /* damaged */
                        game->player.health -= 1;
                } break;
            }
        }
    }
}

void update(Game *game, float dt) {
    while (accumulator > 1)    accumulator -= 1;
    if (accumulator < 0)       accumulator  = 0;
    mouse_pos = Vector2Scale(GetMousePosition(), 2);

    turn_tick(game, dt);

    /* animate tweens */
    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        float target      = slot_strength_target[i];
        slot_strength[i] += (target - slot_strength[i]) * dt;
    }

    /*
    is player deciding to act with current action queue?
    */
    int is_player_act_allowed = 1;
    
    if (game->player_act) { // Already acting
        is_player_act_allowed = 0;
    }

    if (game->player.action_count <= 0) { // Empty action count
        game->player.action_count = 0;
        is_player_act_allowed = 0;
    }

    if (is_player_act_allowed && IsKeyPressed('P')) {
        game->player_act = 1;
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

            if (game->player.action_count < ACTION_CAPACITY) {
                game->player.actions[game->player.action_count++] = a;
            }
        }
    }

    /* Update active action / select & delete scheme */

    {
        Rectangle queued_layout = get_action_queue_layout();

        int deleting_index = -1;
        for (int i = 0; i < game->player.action_count; ++i) {
            Rectangle r = get_rowslot_for_nth_tile(queued_layout, i, 8);

            if (CheckCollisionPointRec(mouse_pos, r) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                deleting_index = i;
            }
        }

        if (deleting_index != -1) {
            if (deleting_index < (game->player.action_count - 1)) {
                memmove(&game->player.actions[deleting_index],
                        &game->player.actions[deleting_index + 1],
                        game->player.action_count - deleting_index);
            }
            game->player.action_count -= 1;
        }
    }
}

void render_healthbar(Actor *actor, Rectangle actor_rect) {
    /* render health of player. */
    Rectangle health_bar = {};
    Vector2 position = position_of_pivot(actor_rect, TOP, CENTER);

    health_bar.width =  TILE * actor->max_health;
    health_bar.height = TILE * 0.25;
    health_bar.x = position.x - (health_bar.width  * 0.5);
    health_bar.y = position.y - (health_bar.height * 1.5);

    BeginShaderMode(shader);
    DrawRectangleRec(health_bar, WHITE);
    EndShaderMode();

    health_bar.width = TILE;
    for (int i = 0; i < actor->health; ++i) {
        DrawRectangleRec(health_bar, WHITE);
        health_bar.x += TILE;
    }
}

void render(Game *game) {
    BeginTextureMode(render_tex);
    ClearBackground(BLACK);
    {
        /* TODO: DONT SET THE SHADER HERE!!?!?!? */
        SetShaderValue(shader, time_loc, &accumulator, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, strength_loc, &BASE_SHADER_EFFECT_THRESHOLD, SHADER_UNIFORM_FLOAT);

        /* TODO: player position should be moved somewhere else */
        Rectangle player = {};
        player.width  = 1 * TILE;
        player.height = 2 * TILE;
        player.x = render_tex.texture.width * 0.25 - (player.width  / 2);
        player.y = render_tex.texture.height * 0.5 - (player.height / 2);

        render_healthbar(&game->player, player);

        DrawRectangleRec(player, WHITE);
        Rectangle layout = get_available_action_layout();

        /* 
        enemy position:
         since enemy's count is variadic, you start from the middle line of screen,
         then push y axis to the top by the count of active enemies * tile size + margin.
        */
        int alive_enemy_count = 0;

        for (int i = 0; i < ENEMY_CAPACITY; ++i) {
            if(game->enemies[i].health > 0) alive_enemy_count++;
        }

        float enemy_group_height = alive_enemy_count * (TILE * 2.5); /* half the tile size as margins. */

        /* Calculate position for enemy group, then... */
        float enemy_group_y = (render_tex.texture.height * 0.5) - (enemy_group_height * 0.5);
        float enemy_group_x =  render_tex.texture.width * 0.75  - (TILE * 0.5); /* Just a bit of shift by tile size.*/

        /* Based on enemy's count, render enemy sprites. */
        int rendered_enemies = 0;
        for (int i = 0; i < ENEMY_CAPACITY; ++i) {
            if(game->enemies[i].health <= 0) continue; // Ignore dead enemy (TODO: should have effect that enemy dies)

            Actor *actor = &game->enemies[i];
            Rectangle rect = {0};

            rect.width  = TILE;
            rect.height = TILE * 2;
            rect.x = enemy_group_x + ((rendered_enemies % 2 == 0) ? (TILE * 0.5) : 0); /* Zigzag effect by adding half size of tile when enemy is even. */ 
            rect.y = enemy_group_y + (rendered_enemies * TILE * 2.5);
        
            /* Inactive enemies will be shadered */
            if (game->enemy_idx != i) BeginShaderMode(shader);
            DrawRectangleRec(rect, WHITE);
            render_healthbar(&game->enemies[i], rect);
            if (game->enemy_idx != i) EndShaderMode();

            rendered_enemies++;
        }

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

        /* ======================================================
         * Render Active Action list
         */
        Rectangle queued_layout = get_action_queue_layout();

        for (int i = 0; i < ACTION_CAPACITY; ++i) {
            if (i >= game->player.action_count) BeginShaderMode(shader);
            Rectangle r = get_rowslot_for_nth_tile(queued_layout, i, 8);

            if (i < game->player.action_count) {
                Action a = game->player.actions[i];

                char chara[2];
                chara[0] = '0' + a.type;
                chara[1] = '\0';

                float w = MeasureTextEx(font, chara, TILE, 0).x;
                Vector2 pos = { r.x + (r.width / 2) - w / 2, r.y };
                DrawTextEx(font, chara, pos, TILE, 0, WHITE);
            }

            int line_thickness = 1;
            if (i == game->player.action_index) {
                line_thickness = 2;
            }

            DrawRectangleLinesEx(r, line_thickness, WHITE);
            if (i >= game->player.action_count) EndShaderMode();
        }


        const char *format = TextFormat("%d / %d", game->player.action_count, ACTION_CAPACITY);
        float w = MeasureText(format, 18);
        DrawText(format, queued_layout.x + (ACTION_CAPACITY * TILE * 0.5) - (w * 0.5), queued_layout.y - 18 - 2, 18, WHITE);
    }

    EndTextureMode();
}

int main(void) {
    window_size = { 0, 0, 1600, 900 };
    InitWindow(window_size.width, window_size.height, "MainWindow");
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

    Game game = {0};
    DEBUG_createphase(&game);

    while(!WindowShouldClose()) {
        float dtime  = GetFrameTime();
        accumulator += dtime;

        update(&game, dtime);
        render(&game);

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
    CloseWindow();
    return 0;
}
