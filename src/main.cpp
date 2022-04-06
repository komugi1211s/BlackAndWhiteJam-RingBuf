#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>

#define FUZZY_MY_H_IMPL
#define fz_NO_WINDOWS_H
#include "my.h"

/* Constants */
const float BASE_SHADER_EFFECT_THRESHOLD = 0.5;
#define TILE 96

/* Game globals */
static Rectangle window_size = { 0, 0, 1600, 900 };
static float accumulator = 0;
static Vector2 mouse_pos = {};

static RenderTexture2D render_tex;

struct Shader_Loc {
    int time_loc;
    int strength_loc;
    int resolution_loc;
};

static Shader     noise_shader;
static Shader_Loc noise_shader_loc;

static Shader     dither_shader;
static Shader_Loc dither_shader_loc;

static Shader     fade_inout_shader;
static Shader_Loc fade_inout_shader_loc;

static Font   font;

const float CHARACTER_Y_POSITION_FROM_TOP = 0.55;

enum /* Core scene phase */
{
    TITLE_SCREEN,
    STAGE_SELECT,
    STAGE_LOADING,
    GAME_IN_PROGRESS,
    GAME_STATE_COUNT,
};

const char *game_state_to_char[GAME_STATE_COUNT] = {
    "Title Screen",
    "Stage Select",
    "Stage Loading",
    "Game In Progress",
};

enum /* Combat State */
{
    COMBAT_PLAYER_WON,
    COMBAT_ENEMY_WON,
    COMBAT_PLAYER_CONQUERED,
    COMBAT_IN_PROGRESS,
};

enum /* Asset state */
{
    ASSET_NONE,
    ASSET_BACKGROUND,
    ASSET_FRACTAL_FADE_TEX,

    /* Player / Animations */
    ASSET_PLAYER_BEGIN,
    ASSET_PLAYER_RESTING,
    ASSET_PLAYER_WALKING,
    ASSET_PLAYER_PLANNING,
    ASSET_PLAYER_ATTACK,
    ASSET_PLAYER_EVADE,
    ASSET_PLAYER_BLOCKING,
    ASSET_PLAYER_TACKLING,
    ASSET_PLAYER_INJURED,
    ASSET_PLAYER_DYING,
    ASSET_PLAYER_END,

    /* Skill Icons */
    ASSET_ACTION_ICON_BEGIN,
    ASSET_ACTION_ICON_SLASH,
    ASSET_ACTION_ICON_EVADE,
    ASSET_ACTION_ICON_PARRY,
    ASSET_ACTION_ICON_TACKLE,
    ASSET_ACTION_ICON_END,

    ASSET_STATE_COUNT,
};

struct Tex2DWrapper { /* Wrapper for Texture2D -- to really know if the assets are actually loaded. */
    int is_loaded;
    Texture2D t;
};

Tex2DWrapper art_assets[ASSET_STATE_COUNT];
int player_animation_states[ASSET_PLAYER_END - ASSET_PLAYER_BEGIN];

int load_tex_to_id(int position, const char *tex) {
    assert(position >= 0 && position < ASSET_STATE_COUNT);

    Tex2DWrapper *wrapper = &art_assets[position];
    if (wrapper->is_loaded) {
        UnloadTexture(wrapper->t);
        wrapper->is_loaded = 0;
    }

    Texture2D loaded = LoadTexture(tex);
    if (loaded.id != 0) {
        wrapper->t = loaded;
        wrapper->is_loaded = 1;
        return 1;
    }

    return 0;
}

#define ENEMY_CAPACITY 3

/* ============================================================
 *  Game Data And Core Structure.
 */

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

const Action base_actions[] = {
    { ACTION_SLASH,  0 },
    { ACTION_EVADE,  0 },
    { ACTION_PARRY,  0 },
    { ACTION_TACKLE, 0 },
};

/* TODO: replace it with other effect strength */
static float slot_strength[fz_COUNTOF(base_actions)];
static float slot_strength_target[fz_COUNTOF(base_actions)];

struct Actor {
    int health;
    int max_health;
    int action_index;
    int action_count;
    Action actions[ACTION_CAPACITY];
};

struct Event {
    const char *name;
    uint32_t    hash;
};

struct Game {
    int core_state;
    int next_core_state;

    float transition;
    float max_transition;

    int player_act;
    Actor player;

    int enemy_idx;
    Actor enemies[ENEMY_CAPACITY];

    Vec(Event) events;
};

uint32_t hash(const char *c) {
    uint32_t x = 5381;
    char a;
    while((a = *c++)) {
        x = ((x << 5) + x) ^ a;
    }
    return x;
}

/* ============================================================
 * Helper Functions.
 */

inline Rectangle
rectf4(float x, float y, float w, float h) {
    return { x, y, w, h };
}

inline Rectangle
rectv2(Vector2 pos, Vector2 size) {
    return { pos.x, pos.y, size.x, size.y };
}

inline Rectangle
rectv2f2(Vector2 pos, float w, float h) {
    return { pos.x, pos.y, w, h };
}

inline Rectangle
rectf2v2(float x, float y, Vector2 size) {
    return { x, y, size.x, size.y };
}

inline Rectangle
render_tex_rect(void) {
    if (render_tex.texture.id != 0) {
        return { 0, 0, (float)render_tex.texture.width, (float)render_tex.texture.height };
    } else {
        return { 0, 0, 1, 1 }; /* Prevent possible division by zero */
    }
}

/* ============================================================
 *  Layouting.
 */

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

/* ============================================================
 * Debug scenes / states.
 */
void DEBUG_createphase(Game *game) {
    game->core_state = TITLE_SCREEN;

    game->player.health     = 3;
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

void draw_debug_information(Game *game) {
    Vector2 pos = { 10, 10 };
    DrawTextEx(font, TextFormat("Core Game State: %s", game_state_to_char[game->core_state]), pos, 32, 0, YELLOW);

    Rectangle game_render_rect = render_tex_rect();

    Vector2 y_up    = position_of_pivot(window_size, TOP,    CENTER);
    Vector2 y_down  = position_of_pivot(window_size, BOTTOM, CENTER);
    Vector2 x_left  = position_of_pivot(window_size, MIDDLE, LEFT);
    Vector2 x_right = position_of_pivot(window_size, MIDDLE, RIGHT);

    DrawLineV(y_up,   y_down,  YELLOW);
    DrawLineV(x_left, x_right, RED);

    float x_ratio = (window_size.width  / game_render_rect.width);
    float y_ratio = (window_size.height / game_render_rect.height);

    int tile_x = floor(mouse_pos.x * x_ratio);
    int tile_y = floor(mouse_pos.y * x_ratio);

    tile_x -= (tile_x % (int)(TILE * x_ratio));
    tile_y -= (tile_y % (int)(TILE * x_ratio));

    Rectangle r = rectf4(tile_x, tile_y, TILE * x_ratio, TILE * y_ratio);
    DrawRectangleLinesEx(r, 1, YELLOW);

    pos.y += 32;
    DrawTextEx(font, TextFormat("This frame's event: %d", (int)VecLen(game->events)), pos, 32, 0, YELLOW);

    pos.y += 32;
    for (int i = 0; i < VecLen(game->events); ++i) {
        Event e = game->events[i];
        DrawTextEx(font, TextFormat("  %d: %s", i, e.name), pos, 32, 0, YELLOW);
        pos.y += 32;
    }
}

/* ============================================================
 * GUI.
 */

enum
{
    INTERACT_NONE        = 0,
    INTERACT_HOVERING    = 1 << 0,
    INTERACT_CLICK_LEFT  = 1 << 1,
    INTERACT_CLICK_RIGHT = 1 << 2,
};

int do_button_esque(uint32_t id, Rectangle rect, const char *label, float label_size, int deny_interact_mask) {
    int result = INTERACT_NONE;

    if (CheckCollisionPointRec(mouse_pos, rect)) {
        result |= INTERACT_HOVERING;

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  result |= INTERACT_CLICK_LEFT;
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) result |= INTERACT_CLICK_RIGHT;
    }

    result &= ~deny_interact_mask;

    if ((result & INTERACT_CLICK_LEFT) || (result & INTERACT_CLICK_RIGHT)) {
        DrawRectangleLinesEx(rect, 8, WHITE);
    } if (result & INTERACT_HOVERING) {
        DrawRectangleLinesEx(rect, 4, WHITE);
    } else {
        DrawRectangleLinesEx(rect, 1, WHITE);
    }

    if (label) {
        Vector2 size = MeasureTextEx(font, label, label_size, 0);
        Vector2 pos  = Vector2Subtract(position_of_pivot(rect, MIDDLE, CENTER), Vector2Scale(size, 0.5));
        DrawTextEx(font, label, pos, label_size, 0, WHITE);
    }

    return result;
}

/* ============================================================
 * Game State / logic.
 */

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

void set_next_state(Game *game, int state_to, float transition_seconds) {
    if (game->core_state != state_to && game->next_core_state != state_to) {
        game->next_core_state = state_to;
        game->transition = game->max_transition = transition_seconds;
    }
}

void update(Game *game, float dt) {
    while (accumulator > 1)    accumulator -= 1;
    if (accumulator < 0)       accumulator  = 0;
    mouse_pos = Vector2Scale(GetMousePosition(), 2);

    if (game->core_state != game->next_core_state) {
        if (game->transition <= 0) {
            game->core_state = game->next_core_state;
            game->transition = game->max_transition;
        }
    }

    if (game->transition > 0) game->transition -= dt;
    if (game->transition < 0) game->transition = 0;

    /* Event that happened last frame. */
    for (int i = 0; i < VecLen(game->events); ++i) {
        Event e = game->events[i];

        /* TODO: This is the reason why the game get's extremely slow */
        if (e.hash == hash("Game Begin")) {
            set_next_state(game, STAGE_SELECT, 2.0);
            break;
        }
    }

    VecClear(game->events);
    turn_tick(game, dt);

    /* animate tweens */
    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        float target      = slot_strength_target[i];
        slot_strength[i] += (target - slot_strength[i]) * 0.05;
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

/* TODO: @limit Event label should always be a static literal. */
void fire_event(Game *game, const char *event_label) {
    Event event = {0};

    event.name = event_label;
    event.hash = hash(event_label);

    VecPush(game->events, event);
}

/* ============================================================
 * Rendering / GUI.
 */

void set_perframe_shader_uniform(Shader shader, Shader_Loc loc) {
    Rectangle a = render_tex_rect();
    Vector2 screen_size = { a.width, a.height };

    SetShaderValue(shader, loc.resolution_loc, &screen_size, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, loc.time_loc,       &accumulator, SHADER_UNIFORM_FLOAT);
}

void set_shaderloc(Shader *shader, Shader_Loc *shader_loc) {
    shader_loc->time_loc       = GetShaderLocation(*shader, "fTime");
    shader_loc->strength_loc   = GetShaderLocation(*shader, "fStrength");
    shader_loc->resolution_loc = GetShaderLocation(*shader, "vResolution");
}

void draw_texture_sane(Texture2D tex, Rectangle target) {
    Rectangle src = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };

    Vector2 origin = {0};
    float   rotation = 0;

    DrawTexturePro(tex, src, target, origin, rotation, WHITE);
}

void render_healthbar(Actor *actor, Rectangle actor_rect) {
    /* render health of player. */
    Rectangle health_bar = {};
    Vector2 position = position_of_pivot(actor_rect, TOP, CENTER);

    health_bar.width =  TILE * actor->max_health;
    health_bar.height = TILE * 0.25;
    health_bar.x = position.x - (health_bar.width  * 0.5);
    health_bar.y = position.y - (health_bar.height * 1.5);

    BeginShaderMode(noise_shader);
    DrawRectangleRec(health_bar, WHITE);
    EndShaderMode();

    health_bar.width = TILE;
    for (int i = 0; i < actor->health; ++i) {
        DrawRectangleRec(health_bar, WHITE);
        health_bar.x += TILE;
    }
}

void do_combat_gui(Game *game) {
    BeginShaderMode(dither_shader);
    Tex2DWrapper wrapper = art_assets[ASSET_BACKGROUND];
    if (wrapper.is_loaded) {
        Rectangle dest = render_tex_rect();
        draw_texture_sane(wrapper.t, dest);
    }
    EndShaderMode();

    /* TODO: DONT SET THE SHADER HERE!!?!?!? */
    SetShaderValue(noise_shader, noise_shader_loc.strength_loc, &BASE_SHADER_EFFECT_THRESHOLD, SHADER_UNIFORM_FLOAT);

    /* TODO: player position should be moved somewhere else */
    Rectangle player = {};
    player.width  = 1 * TILE;
    player.height = 2 * TILE;
    player.x = render_tex.texture.width * 0.25 - (player.width  / 2);
    player.y = render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP - (player.height / 2);

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
    float enemy_group_y = (render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP) - (enemy_group_height * 0.5);
    float enemy_group_x =  render_tex.texture.width * 0.75  - (TILE * 0.5); /* Just a bit of shift by tile size.*/

    /* Based on enemy's count, render enemy sprites. */
    int rendered_enemies = 0;
    for (int i = 0; i < ENEMY_CAPACITY; ++i) {
        if(game->enemies[i].health <= 0) continue; // Ignore dead enemy (TODO: should have effect that enemy dies)

        Actor *enemy = &game->enemies[i];
        Rectangle rect = {0};

        rect.width  = TILE;
        rect.height = TILE * 2;
        rect.x = enemy_group_x + ((rendered_enemies % 2 == 0) ? (TILE * 0.5) : 0); /* Zigzag effect by adding half size of tile when enemy is even. */
        rect.y = enemy_group_y + (rendered_enemies * TILE * 2.5);

        /* Inactive enemies will be shadered */
        if (game->enemy_idx != i) BeginShaderMode(noise_shader);
        DrawRectangleRec(rect, WHITE);
        render_healthbar(enemy, rect);
        if (game->enemy_idx != i) EndShaderMode();

        rendered_enemies++;
    }

    /*
     * Render Base Actions
     **/
    BeginShaderMode(noise_shader);
    for (int i = 0; i < fz_COUNTOF(base_actions); ++i) {
        Action a = base_actions[i];

        Texture2D tex = art_assets[ASSET_ACTION_ICON_BEGIN + a.type].t;
        Rectangle dest = get_rowslot_for_nth_tile(layout, i, 0);
        Rectangle texdest = dest;
        texdest.x += 1;
        texdest.y += 1;
        texdest.width -= 2;
        texdest.height -= 2;
        SetShaderValue(noise_shader, noise_shader_loc.strength_loc, &slot_strength[i], SHADER_UNIFORM_FLOAT);

        draw_texture_sane(tex, texdest);
        DrawRectangleLinesEx(dest, 1, WHITE);
    }
    EndShaderMode();

    /* Reset shader options */
    SetShaderValue(noise_shader, noise_shader_loc.strength_loc, &BASE_SHADER_EFFECT_THRESHOLD, SHADER_UNIFORM_FLOAT);

    /* ======================================================
     * Render Active Action list
     */
    Rectangle queued_layout = get_action_queue_layout();

    for (int i = 0; i < ACTION_CAPACITY; ++i) {
        if (i >= game->player.action_count) BeginShaderMode(noise_shader);
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

void do_title_gui(Game *game) {
    Rectangle render_rect = render_tex_rect();

    BeginShaderMode(dither_shader);
    Tex2DWrapper wrapper = art_assets[ASSET_BACKGROUND];
    if (wrapper.is_loaded) {
        Rectangle dest = render_tex_rect();
        draw_texture_sane(wrapper.t, dest);
    }
    EndShaderMode();

    Vector2 pos = { (float)render_rect.width * 0.5f, (float)render_rect.height * 0.65f };
    Vector2 size = MeasureTextEx(font, "Press Enter", TILE, 0);

    pos = Vector2Subtract(pos, Vector2Scale(size, 0.5));
    DrawTextEx(font, "Press Enter", pos, TILE, 0, WHITE);

    if (game->next_core_state == game->core_state) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_ENTER)) {
            fire_event(game, "Game Begin");
        }
    }
}

void do_stage_select_gui(Game *game) {
    Rectangle render_rect = render_tex_rect();

    BeginShaderMode(dither_shader);
    Tex2DWrapper wrapper = art_assets[ASSET_BACKGROUND];
    if (wrapper.is_loaded) {
        Rectangle dest = render_tex_rect();
        draw_texture_sane(wrapper.t, dest);
    }
    EndShaderMode();

    Vector2 button_size = { TILE * 8, TILE * 2 };

    Vector2 pos         = { (float)render_rect.width * 0.5f, (float)render_rect.height * 0.65f };
    pos = Vector2Subtract(pos, Vector2Scale(button_size, 0.5));

    Rectangle r1 = rectv2(pos, button_size);
    int stage_one = do_button_esque(hash("StageOne"), r1, 0, 0, INTERACT_CLICK_LEFT);
    {
        Vector2 size = MeasureTextEx(font, "Stage 1", TILE, 0);
        Vector2 pivot = position_of_pivot(r1, TOP, CENTER);
        pivot.y += 12;
        pivot.x -= size.x * 0.5;

        DrawTextEx(font, "Stage 1", pivot, TILE, 0, WHITE);
    }

    pos.y += (TILE * 2.5);
    int stage_two = do_button_esque(hash("StageTwo"), rectv2(pos, button_size), 0, 0, INTERACT_CLICK_LEFT);
    {
        Vector2 size = MeasureTextEx(font, "Stage 2", TILE, 0);
    }
}

void do_gui(Game *game) {
    BeginTextureMode(render_tex);
    ClearBackground(BLACK);

    switch(game->core_state) {
        case TITLE_SCREEN:
        {
            do_title_gui(game);
        } break;

        case STAGE_SELECT:
        {
            do_stage_select_gui(game);
        } break;

        case STAGE_LOADING:
        {
            // render_loading_screen(game);
        } break;

        case GAME_IN_PROGRESS:
        {
            do_combat_gui(game);
        } break;
    }

    EndTextureMode();
}
int main(void) {
    window_size = { 0, 0, 1600, 900 };
    InitWindow(window_size.width, window_size.height, "MainWindow");
    noise_shader       = LoadShader(0, "assets/shaders/noise_shader.fs");
    dither_shader      = LoadShader(0, "assets/shaders/dither_shader.fs");
    fade_inout_shader = LoadShader(0, "assets/shaders/fade_in_shader.fs");

    set_shaderloc(&noise_shader, &noise_shader_loc);
    set_shaderloc(&dither_shader, &dither_shader_loc);
    set_shaderloc(&fade_inout_shader, &fade_inout_shader_loc);

    font = LoadFontEx("assets/fonts/EBGaramond-Regular.ttf", TILE, 0, 0);
    Rectangle render_rect = { 0, 0, window_size.width * 2.0f, window_size.height * 2.0f };

    render_tex = LoadRenderTexture(render_rect.width, render_rect.height);
    SetTextureFilter(render_tex.texture, TEXTURE_FILTER_BILINEAR);

    float a = 1;
    SetShaderValue(fade_inout_shader, fade_inout_shader_loc.strength_loc, &a, SHADER_UNIFORM_FLOAT);

    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        slot_strength[i] = slot_strength_target[i] = 0.5;
    }

    Game game = {0};
    game.events = VecCreate(Event, 512);
    game.max_transition = 1; /* division by zero prevention */

    DEBUG_createphase(&game);

    load_tex_to_id(ASSET_BACKGROUND,         "assets/images/background.png");
    load_tex_to_id(ASSET_ACTION_ICON_SLASH,  "assets/images/spinning-sword.png");
    load_tex_to_id(ASSET_ACTION_ICON_EVADE,  "assets/images/wingfoot.png");
    load_tex_to_id(ASSET_ACTION_ICON_PARRY,  "assets/images/sword-break.png");
    load_tex_to_id(ASSET_ACTION_ICON_TACKLE, "assets/images/shield-bash.png");
    accumulator = 0;

    while(!WindowShouldClose()) {
        float dtime  = GetFrameTime();
        accumulator += dtime;

        {
            set_perframe_shader_uniform(noise_shader, noise_shader_loc);
            set_perframe_shader_uniform(dither_shader, dither_shader_loc);
            set_perframe_shader_uniform(fade_inout_shader, fade_inout_shader_loc);
        }

        update(&game, dtime);
        do_gui(&game);

        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(fade_inout_shader);
            Rectangle swapped = render_rect;
            swapped.height *= -1;
            Vector2 offset = {};
            DrawTexturePro(render_tex.texture, swapped, window_size, offset, 0, WHITE);
        EndShaderMode();
        draw_debug_information(&game);

        EndDrawing();
    }

    for (int i = 0; i < ASSET_STATE_COUNT; ++i) {
        if (art_assets[i].is_loaded)
            UnloadTexture(art_assets[i].t);
    }

    VecRelease(game.events);
    UnloadFont(font);
    UnloadRenderTexture(render_tex);
    UnloadShader(noise_shader);
    UnloadShader(dither_shader);
    UnloadShader(fade_inout_shader);
    CloseWindow();
    return 0;
}
