#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>

#define FUZZY_MY_H_IMPL
#define fz_NO_WINDOWS_H
#include "my.h"

/* Constants */
const float CHARACTER_Y_POSITION_FROM_TOP = 0.5;
const float BASE_SHADER_EFFECT_THRESHOLD = 0.5;
#define TILE 80

fz_STATIC_ASSERT(TILE % 2 == 0);

/* Game globals */
static Rectangle window_size = { 0, 0, 1280,  720 };
static Rectangle render_size = { 0, 0, 1920, 1080 };
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


enum /* Asset state */
{
    ASSET_NONE,
    ASSET_TEXTURE_BEGIN,
        ASSET_STAGE_SELECT_BACKGROUND,
        ASSET_COMBAT_BACKGROUND,
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

        ASSET_EFFECT_SPRITE_BEGIN,
            ASSET_EFFECT_SPRITE_RECEIVED_SLASH,
        ASSET_EFFECT_SPRITE_END,
    ASSET_TEXTURE_END,

    ASSET_SOUND_BEGIN,
        ASSET_SOUND_START_GAME,
        ASSET_SOUND_ACTION_SELECT,
        ASSET_SOUND_ACTION_SUBMIT,
    ASSET_SOUND_END,

    ASSET_STATE_COUNT,
};

struct Tex2DWrapper { /* Wrapper for Texture2D -- to really know if the assets are actually loaded. */
    int is_loaded;
    Texture2D t;
};

struct SoundWrapper {
    int is_loaded;
    Sound sound;
};

Tex2DWrapper art_assets[ASSET_TEXTURE_END - ASSET_TEXTURE_BEGIN];
SoundWrapper sound_assets[ASSET_SOUND_END - ASSET_SOUND_BEGIN];
int player_animation_states[ASSET_PLAYER_END - ASSET_PLAYER_BEGIN];

int load_tex_to_id(int position, const char *tex) {
    assert(ASSET_TEXTURE_BEGIN < position && position < ASSET_TEXTURE_END);

    Tex2DWrapper *wrapper = &art_assets[position - ASSET_TEXTURE_BEGIN];
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

int load_sound_to_id(int position, const char *asset_name) {
    assert(ASSET_SOUND_BEGIN < position && position < ASSET_SOUND_END);

    SoundWrapper *wrapper = &sound_assets[position - ASSET_SOUND_BEGIN];
    if (wrapper->is_loaded) {
        UnloadSound(wrapper->sound);
        wrapper->is_loaded = 0;
    }

    Sound sound = LoadSound(asset_name);
    if (sound.stream.buffer != 0) {
        wrapper->sound = sound;
        wrapper->is_loaded = 1;
        return 1;
    }
    return 0;
}

int get_texture(int position, Texture2D *write_into) {
    assert(ASSET_TEXTURE_BEGIN < position && position < ASSET_TEXTURE_END);
    Tex2DWrapper *wrapper = &art_assets[position - ASSET_TEXTURE_BEGIN];

    if (wrapper->is_loaded) {
        *write_into = wrapper->t;
    }
    return wrapper->is_loaded;
}

int get_sound(int position, Sound *write_into) {
    assert(ASSET_SOUND_BEGIN < position && position < ASSET_SOUND_END);
    SoundWrapper *wrapper = &sound_assets[position - ASSET_SOUND_BEGIN];

    if (wrapper->is_loaded) {
        *write_into = wrapper->sound;
    }
    return wrapper->is_loaded;
}

#define ENEMY_CAPACITY 3

/* ============================================================
 *  Game Data And Core Structure.
 */

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
    COMBAT_STATE_BEGIN,
    COMBAT_STATE_PLAYER_PLANNING,
    COMBAT_STATE_RUNNING_TURN,
    COMBAT_STATE_PLAYER_DIED,
    COMBAT_STATE_ENEMY_DIED,
    COMBAT_STATE_GOING_NEXT_PHASE,
    COMBAT_STATE_STAGE_COMPLETE,
};

enum
{
    ACTION_NONE = 0,
    /* Volatile: order must match with asset enum */
    ACTION_SLASH,
    ACTION_EVADE,
    ACTION_PARRY,
    ACTION_TACKLE,
    ACTION_COUNT,
};

struct Action {
    int type;
};

#define ACTION_CAPACITY 10

const Action base_actions[] = {
    { ACTION_SLASH  },
    { ACTION_TACKLE },
    { ACTION_EVADE  },
    { ACTION_PARRY  },
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

#define EVENT_LIST          \
    EVENT(NONE)             \
    EVENT(PLAY_SOUND)       \
    EVENT(TRANSITION_STATE) \
    EVENT(BEGIN_GAME_STAGE) \

#define EVENT(ev) fz_CONCAT(EVENT_, ev),
enum
{
    EVENT_LIST
};
#undef EVENT

#define EVENT(ev) fz_STR(fz_CONCAT(EVENT_, ev)),
const char *event_name[] = {
    EVENT_LIST
};
#undef EVENT

struct Enemy_Chain {
    int   enemy_count;
    Actor enemies[ENEMY_CAPACITY];
};

struct Effect {
    int       asset_id;
    int       elapsed;
    int       max_life;
    Rectangle rect;
};

struct Interval {
    float max;
    float current;
};

struct State {
    int current;
    int next;

    float transition;
    float max_transition;
};

struct Game {
    State core_state;
    State combat_state;

    Interval turn_interval;
    Interval effect_interval;

    int locked_in_index;
    Actor player;

    int enemy_index;
    int chain_index;

    Vec(Enemy_Chain) enemies;
    Vec(Effect)      effects;

    Camera2D camera;
    float camerashake_shift_distance;
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

inline Vector2
v2tile(float w, float h) {
    return { w * TILE, h * TILE };
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

/* ===================================================
 * Rectangle builder for positioning / layouting.
 */

struct Rect_Builder {
    Rectangle base;
    Rectangle result;
};

Rect_Builder rect_builder(Rectangle base) {
    Rect_Builder builder = {0};
    builder.base = base;
    builder.result = base;

    return builder;
}

/* ===================================================
 * Aligns current position of rectangle to passed position of parent.
 */

void rb_set_position_percent(Rect_Builder *b, float x_percent, float y_percent) {
    b->result.x = b->base.x  + (b->base.width  * x_percent);
    b->result.y = b->base.y  + (b->base.height * y_percent);
}

void rb_set_size_v2(Rect_Builder *b, Vector2 element_size) {
    b->result.width  = element_size.x;
    b->result.height = element_size.y;
}

void rb_add_position_by(Rect_Builder *b, float x_pixels, float y_pixels) {
    b->result.x += x_pixels;
    b->result.y += y_pixels;
}

/* ===================================================
 * Assuming that current rectangle has top-left pivot,
 * readjust the position of rectangle by passed new pivot.
 *
 * e.g.
 * given rectangle -- Position x: 10, y: 10, Size w: 10, h: 10
 * calling rb_reposition_by_pivot(b, MIDDLE, CENTER) will
 * set the rectangle position to x: 5, y: 5.
 */
void rb_reposition_by_pivot(Rect_Builder *b, int y_pivot, int x_pivot) {
    switch(y_pivot) {
        case TOP: break;
        case MIDDLE: b->result.y -= b->result.height * 0.5; break;
        case BOTTOM: b->result.y -= b->result.height;       break;
        default: assert(0);
    }
    switch(x_pivot) {
        case LEFT:   break;
        case CENTER: b->result.x -= b->result.width * 0.5; break;
        case RIGHT:  b->result.x -= b->result.width;       break;
    }
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

/* ===========================================================
 * Get a position of point in rectangle,
 * specified by pivot.
 */
Vector2 rb_get_point_of(Rect_Builder *b, int y_axis, int x_axis) {
    return position_of_pivot(b->result, y_axis, x_axis);
}

/*
    Align position of text specified by pivot.
*/
Vector2 align_text_by(Vector2 pos, const char *text, int y_align, int x_align, float font_size) {
    Vector2 size = MeasureTextEx(font, text, font_size, 0);
    return position_of_pivot(
        rectv2(pos, Vector2Negate(size)),
        y_align,
        x_align
    );
}

Rectangle get_available_action_layout(void) {
    Rectangle rect = {};

    rect.width  = (TILE * 1.5 - 8) * fz_COUNTOF(base_actions);
    rect.height = (TILE * 1.5 - 8);

    rect.x = render_tex.texture.width  * 0.5 - (TILE * 1.5) * fz_COUNTOF(base_actions) * 0.5;
    rect.y = render_tex.texture.height * 0.9 - (TILE * 1.5) * 0.5;

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

Rectangle get_rowslot_for_nth_tile(Rectangle layout, int index, float scale, int margin) {
    Rectangle rect = {0};
    rect.width  = (TILE * scale) - margin;
    rect.height = (TILE * scale) - margin;

    rect.x = layout.x + (TILE * scale * index) + (margin / 2);
    rect.y = layout.y;
    return rect;
}

/* ============================================================
 * Debug scenes / states.
 */

void reset_combatstate(Game *game) {
    game->locked_in_index = -1;
    game->enemy_index = 0;
    game->chain_index = 0;
    game->player.health = game->player.max_health = 3;

    VecClear(game->enemies);
}

void load_stage_one(Game *game) {
    reset_combatstate(game);
    {
        Enemy_Chain chain = {0};

        /* First wave */
        chain.enemies[0].health = chain.enemies[0].max_health = 3;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_SLASH;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_PARRY;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_TACKLE;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_PARRY;

        chain.enemy_count++;
        chain.enemies[1].health = chain.enemies[1].max_health = 3;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_EVADE;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_SLASH;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_PARRY;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_PARRY;

        chain.enemy_count++;
        chain.enemies[2].health = chain.enemies[2].max_health = 3;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_TACKLE;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_SLASH;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_EVADE;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_EVADE;

        VecPush(game->enemies, chain);
    }

    {
        Enemy_Chain chain = {0};

        /* Second wave */
        chain.enemies[0].health = chain.enemies[0].max_health = 3;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_SLASH;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_PARRY;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_TACKLE;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_PARRY;

        chain.enemy_count++;
        chain.enemies[1].health = chain.enemies[1].max_health = 3;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_EVADE;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_SLASH;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_PARRY;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_PARRY;

        chain.enemy_count++;
        chain.enemies[2].health = chain.enemies[2].max_health = 3;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_TACKLE;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_SLASH;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_EVADE;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_EVADE;

        VecPush(game->enemies, chain);
    }
}

void draw_debug_information(Game *game) {
    Vector2 pos = { 10, 10 };
    DrawTextEx(font, TextFormat("Core Game State: %s", game_state_to_char[game->core_state.current]), pos, 32, 0, YELLOW);

    pos.y += 32;
    DrawTextEx(font, TextFormat("Effect count: %d", (int)VecLen(game->effects)), pos, 32, 0, YELLOW);

    if (game->core_state.current == GAME_IN_PROGRESS) {
        pos.y += 32;
        DrawTextEx(font, TextFormat("Chain: %d / %d", game->chain_index, (int)VecLen(game->enemies)), pos, 32, 0, YELLOW);

        if (game->chain_index < VecLen(game->enemies)) {
            pos.y += 32;
            Enemy_Chain *chain = &game->enemies[game->chain_index];
            DrawTextEx(font, TextFormat("Enemy chain: %d / %d", game->enemy_index, chain->enemy_count), pos, 32, 0, YELLOW);
        }
    }

    Rectangle game_render_rect = render_size;

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

    for (int i = 0; i < VecLen(game->effects); ++i) {
        Rectangle r = game->effects[i].rect;
        r.x *= x_ratio;
        r.y *= x_ratio;
        r.width *= x_ratio;
        r.height *= x_ratio;
        DrawRectangleLinesEx(r, 1, BLUE);
    }
}

/* ============================================================
 * GUI.
 */

void shake_camera(Game *game, int strength) {
    game->camerashake_shift_distance += strength;
}

void spawn_effect(Game *game, int effect_id, Rectangle rect) {
    assert(ASSET_EFFECT_SPRITE_BEGIN < effect_id && effect_id < ASSET_EFFECT_SPRITE_END);
    Texture2D tex;
    if (!get_texture(effect_id, &tex)) {
        printf("Asset %d is not loaded. cannot spawn effects.\n", effect_id);
        return;
    }
    assert(tex.height == 64 && tex.width % 64 == 0);
    int life = (int)(tex.width / 64);

    Effect effect = {0};

    effect.asset_id = effect_id;
    effect.elapsed  = 0;
    effect.max_life = life;
    effect.rect     = rect;

    VecPush(game->effects, effect);
}

enum
{
    INTERACT_NONE        = 0,
    INTERACT_HOVERING    = 1 << 0,
    INTERACT_CLICK_LEFT  = 1 << 1,
    INTERACT_CLICK_RIGHT = 1 << 2,
};

int do_button_esque(uint32_t id, Rectangle rect, const char *label, float label_size, int interact_mask, Color color) {
    int result = INTERACT_NONE;

    if (CheckCollisionPointRec(mouse_pos, rect)) {
        result |= INTERACT_HOVERING;

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  result |= INTERACT_CLICK_LEFT;
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) result |= INTERACT_CLICK_RIGHT;
    }

    result &= interact_mask;

    if ((result & INTERACT_CLICK_LEFT) || (result & INTERACT_CLICK_RIGHT)) {
        DrawRectangleLinesEx(rect, 8, color);
    } if (result & INTERACT_HOVERING) {
        DrawRectangleLinesEx(rect, 4, color);
    } else {
        DrawRectangleLinesEx(rect, 1, color);
    }

    if (label) {
        Vector2 size = MeasureTextEx(font, label, label_size, 0);
        Vector2 pos  = Vector2Subtract(position_of_pivot(rect, MIDDLE, CENTER), Vector2Scale(size, 0.5));
        DrawTextEx(font, label, pos, label_size, 0, color);
    }

    return result;
}

/* ============================================================
 * Game State / logic.
 */


void set_next_state(State *state, int state_to, float transition_seconds) {
    if (state->current != state_to && state->next != state_to) {
        state->next = state_to;
        state->transition = state->max_transition = (transition_seconds * 0.5);
    }
}

int is_transition_done(State *state) {
    return (state->current == state->next) && (state->transition <= 0);
}

float state_delta(State *state) {
    if (state->current == state->next) {
        if (state->max_transition == 0) return 1;

        return 1 - (state->transition / state->max_transition);
    } else {
        if (state->max_transition == 0) return 0;

        return (state->transition / state->max_transition);
    }
}

float state_tick(State *state, float dt) {
    if (state->current != state->next) {
        if (state->transition <= 0) {
            state->current = state->next;
            state->transition = state->max_transition;
        }
    }

    if (state->transition > 0) state->transition -= dt;
    if (state->transition < 0) state->transition = 0;

    return state_delta(state);
}

int interval_tick(Interval *interval, float dt) {
    interval->current += dt;
    if (interval->max < interval->current) {
        interval->current -= interval->max;
        if(interval->current < 0) interval->current = 0;

        return 1;
    }
    return 0;
}

Action get_next_action_for(Actor *actor) {
    Action action = actor->actions[actor->action_index];
    actor->action_index = (actor->action_index + 1) % actor->action_count;

    return action;
}

void turn_tick(Game *game, float dt) {
    if (game->core_state.current != GAME_IN_PROGRESS) return;
    if (interval_tick(&game->turn_interval, dt)) {
        /* Check if player / enemies are allowed to continue fighting. */
        Enemy_Chain *chain = &game->enemies[game->chain_index];

        if (game->player.health <= 0) { 
            set_next_state(&game->combat_state, COMBAT_STATE_PLAYER_DIED, 4.0);
            return;
        }

        if (game->chain_index == VecLen(game->enemies)) { 
            set_next_state(&game->combat_state, COMBAT_STATE_STAGE_COMPLETE, 1);
            return;
        }

        if (game->enemy_index == chain->enemy_count) {
            set_next_state(&game->combat_state, COMBAT_STATE_GOING_NEXT_PHASE, 1);
            game->enemy_index = 0;
            game->chain_index++;
            return;
        }

        Actor *enemy = &chain->enemies[game->enemy_index];

        if (enemy->health <= 0) { // Progress to next enemy then break
            set_next_state(&game->combat_state, COMBAT_STATE_ENEMY_DIED, 1);
            game->enemy_index++;
            return;
        }

        Action player_action = get_next_action_for(&game->player);
        Action enemy_action  = get_next_action_for(enemy);

        Vector2 enemy_effect_pos;
        Vector2 enemy_effect_size;
        enemy_effect_pos.x = (render_tex.texture.width * 0.75) - TILE;
        enemy_effect_pos.y = (render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP) - TILE;
        enemy_effect_size = { TILE * 2, TILE * 2 };

        Vector2 player_effect_pos;
        Vector2 player_effect_size;
        player_effect_pos.x = (render_tex.texture.width * 0.25) + TILE * 0.5;
        player_effect_pos.y = (render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP);
        player_effect_size = { TILE * 2, TILE * 2 };

        Rectangle enemy_effect_rect  = rectv2(enemy_effect_pos, enemy_effect_size);
        Rectangle player_effect_rect = rectv2(player_effect_pos, player_effect_size);

        int player_dealt_action = -1;
        int enemy_dealt_action = -1;

        switch(player_action.type) {
            /* Offensive Maneuver */
            case ACTION_SLASH: switch(enemy_action.type) {
                case ACTION_PARRY:  /* blocks  */
                    break;

                case ACTION_SLASH:  /* deals   */
                case ACTION_EVADE:  /* deals   */
                case ACTION_TACKLE: /* deals   */
                {
                    shake_camera(game, 8);
                    spawn_effect(game, ASSET_EFFECT_SPRITE_RECEIVED_SLASH, enemy_effect_rect);
                    enemy->health -= 1;
                } break;
            } break;

            case ACTION_TACKLE: switch(enemy_action.type) {
                case ACTION_EVADE:  /* blocked */
                    break;

                case ACTION_PARRY:  /* deals   */
                case ACTION_SLASH:  /* deals   */
                case ACTION_TACKLE: /* deals   */
                {
                    shake_camera(game, 8);
                    enemy->health -= 1;
                } break;
            } break;

            /* Defensive Maneuver */
            case ACTION_PARRY: switch(enemy_action.type) {
                case ACTION_PARRY:  /* nothing */
                case ACTION_EVADE:  /* nothing */
                    break;

                case ACTION_SLASH:  /* avoids  */
                    break; /* TODO: generate animation */

                case ACTION_TACKLE: /* damaged */
                    shake_camera(game, 3);
                    game->player.health -= 1;
            } break;

            case ACTION_EVADE: switch(enemy_action.type) {
                case ACTION_PARRY:  /* nothing */
                case ACTION_EVADE:  /* nothing */
                    break;

                case ACTION_TACKLE: /* avoids  */
                    break; /* TODO: generate animation */

                case ACTION_SLASH:  /* damaged */
                {
                    shake_camera(game, 3);
                    spawn_effect(game, ASSET_EFFECT_SPRITE_RECEIVED_SLASH, player_effect_rect);
                    game->player.health -= 1;
                } break;
            } break;
        }
    }
}


void effects_tick(Game *game, float dt) {
    if (interval_tick(&game->effect_interval, dt)) {
        Vec(int) deleting_index = VecCreateEx(int, VecLen(game->effects) + 1, fz_global_temp_allocator);
        for (int i = 0; i < VecLen(game->effects); ++i) {
            Effect *e = &game->effects[i];
            e->elapsed += 1;
            if (e->elapsed >= e->max_life) {
                VecPush(deleting_index, i);
            }
        }

        for (int i = VecLen(deleting_index) - 1; i >= 0; --i) {
            /*
             * deleting_index is sorted.
             * traversing it in opposite order and performing unordered remove is
             * faster and guaranteed to work without accidentally removing other effects */
            VecRemoveUnorderedN(game->effects, deleting_index[i]);
        }

        VecRelease(deleting_index);
    }
}

void update(Game *game, float dt) {
    while (accumulator > 1)    accumulator -= 1;
    if (accumulator < 0)       accumulator  = 0;
    float x_ratio = (render_size.width  / window_size.width);
    float y_ratio = (render_size.height / window_size.height);
    Vector2 m = GetMousePosition();
    mouse_pos.x = m.x * x_ratio;
    mouse_pos.y = m.y * y_ratio;

    /* animate tweens */
    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        float target      = slot_strength_target[i];
        slot_strength[i] += (target - slot_strength[i]) * 0.05;
    }

    /* animate camera */
    game->camerashake_shift_distance -= dt * 20;
    if (game->camerashake_shift_distance < 0)
        game->camerashake_shift_distance = 0;

    float shake_x = (GetRandomValue(0, 100) / 100.0f) - 0.5;
    float shake_y = (GetRandomValue(0, 100) / 100.0f) - 0.5;
    game->camera.target.x = shake_x * game->camerashake_shift_distance;
    game->camera.target.y = shake_y * game->camerashake_shift_distance;

    game->camera.offset.x = (m.x / window_size.width) - 0.5;
    game->camera.offset.y = (m.y / window_size.height) - 0.5;
    game->camera.offset = Vector2Scale(game->camera.offset, TILE * 0.1);

    /* Ticks */
    state_tick(&game->core_state,   dt);
    state_tick(&game->combat_state, dt);
    effects_tick(game, dt);

    switch(game->combat_state.current) {
        case COMBAT_STATE_BEGIN:
        {
            if (is_transition_done(&game->combat_state))
                set_next_state(&game->combat_state, COMBAT_STATE_PLAYER_PLANNING, 0.25);
        } break;

        case COMBAT_STATE_GOING_NEXT_PHASE:
        {
            if (is_transition_done(&game->combat_state))
                set_next_state(&game->combat_state, COMBAT_STATE_PLAYER_PLANNING, 0.25);
        } break;

        case COMBAT_STATE_ENEMY_DIED:
        {
            if (is_transition_done(&game->combat_state))
                set_next_state(&game->combat_state, COMBAT_STATE_PLAYER_PLANNING, 0.25);
        } break;

        case COMBAT_STATE_RUNNING_TURN:
        {
            if (is_transition_done(&game->combat_state)) turn_tick(game, dt);
        } break;

        case COMBAT_STATE_PLAYER_PLANNING:
        {
            if (is_transition_done(&game->combat_state)) {
                if (IsKeyPressed('P')) {
                    if (game->player.action_count > 0) {
                        set_next_state(&game->combat_state, COMBAT_STATE_RUNNING_TURN, 2);
                        game->locked_in_index = game->player.action_count;
                    }
                }

                /* Update available action / push scheme */
                {
                    Rectangle layout = get_available_action_layout();
                    int selected = -1;

                    for (int i = 0; i < fz_COUNTOF(base_actions); ++i) {
                        Rectangle r = get_rowslot_for_nth_tile(layout, i, 1.5, 8);
                        slot_strength_target[i] = 0.75;

                        if (CheckCollisionPointRec(mouse_pos, r)) {
                            selected                = i;
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
                        Rectangle r = get_rowslot_for_nth_tile(queued_layout, i, 1, 8);

                        if (CheckCollisionPointRec(mouse_pos, r) &&
                            IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                        {
                            deleting_index = i;
                        }
                    }

                    if (deleting_index != -1 && game->locked_in_index <= deleting_index) {
                        if (deleting_index < (game->player.action_count - 1)) {
                            memmove(&game->player.actions[deleting_index],
                                    &game->player.actions[deleting_index + 1],
                                    game->player.action_count - deleting_index);
                        }
                        game->player.action_count -= 1;
                    }
                }
            }
        } break;
    }
}

/* ============================================================
 * Rendering / GUI.
 */
void set_perframe_shader_uniform(Shader shader, Shader_Loc loc) {
    Rectangle a = render_size;
    Vector2 screen_size = { a.width, a.height };

    SetShaderValue(shader, loc.resolution_loc, &screen_size, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, loc.time_loc,       &accumulator, SHADER_UNIFORM_FLOAT);
}

void set_shaderloc(Shader *shader, Shader_Loc *shader_loc) {
    shader_loc->time_loc       = GetShaderLocation(*shader, "fTime");
    shader_loc->strength_loc   = GetShaderLocation(*shader, "fStrength");
    shader_loc->resolution_loc = GetShaderLocation(*shader, "vResolution");
}

void draw_texture_sane(Texture2D tex, Rectangle target, Color color) {
    Rectangle src = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };

    Vector2 origin = {0};
    float   rotation = 0;

    DrawTexturePro(tex, src, target, origin, rotation, color);
}

void render_healthbar(Actor *actor, Rectangle actor_rect) {
    Rectangle health_bar = {};
    Vector2 position = position_of_pivot(actor_rect, TOP, CENTER);

    health_bar.width =  TILE - 4;
    health_bar.height = TILE * 0.15;
    health_bar.x = position.x - ((actor->max_health * TILE) * 0.5);
    health_bar.y = position.y - (health_bar.height * 1.5);

    for (int i = 0; i < actor->health; ++i) {
        DrawRectangleRec(health_bar, WHITE);
        health_bar.x += TILE + 4;
    }
}

void render_enemy(Actor *enemy, Rectangle rect, int is_active_participant) {
    if (is_active_participant) {
        render_healthbar(enemy, rect);
    }

    DrawRectangleRec(rect, Fade(WHITE, (is_active_participant) ? 1 : 0.5));
}

void render_combat_phase_indicator(Game *game) {
    float x = render_size.width * 0.5;
    float y = render_size.height * 0.15;

    int count = (int)VecLen(game->enemies);
    float gap_between    = TILE * 1.5;
    float indicator_size = 24; /* px */
    float fullwidth      = count * indicator_size + (count - 1) * gap_between;
    x -= fullwidth * 0.5;

    for (int i = 0; i < count; ++i) {
        if (i == game->chain_index) {
            DrawCircle(x, y, indicator_size * 0.85, WHITE);
        } else {
            DrawCircleLines(x, y, indicator_size * 0.75, WHITE);
        }

        x += indicator_size;

        if (i < (count - 1)) {
            Vector2 line_begin = { x, y };
            Vector2 line_end   = { x + gap_between, y };

            DrawLineEx(line_begin, line_end, 8, WHITE);
            x += gap_between + indicator_size;
        }
    }

    {
        float delta;

        if (game->combat_state.current == COMBAT_STATE_GOING_NEXT_PHASE
            || game->combat_state.next  == COMBAT_STATE_GOING_NEXT_PHASE)
        {
            delta = state_delta(&game->combat_state);
        }
        else
        {
            delta = 1;
        }

        Color c = Fade(WHITE, delta);
        const char *text = TextFormat("Phase %d / %d", game->chain_index + 1, count);

        float x = render_size.width * 0.5;
        float y = render_size.height * 0.15 + TILE * 0.25;

        Vector2 pos = { x, y };
        pos = align_text_by(pos, text, TOP, CENTER, TILE * 0.75);

        DrawTextEx(font, text, pos, TILE * 0.75, 0, c);
    }
}

void do_combat_gui(Game *game) {
    /* TODO: player position should be moved somewhere else */
    Rectangle player = {};
    player.width  = 1.5 * TILE;
    player.height = 3   * TILE;
    player.x = render_tex.texture.width  * 0.25 - (player.width / 2);
    player.y = render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP - (player.height / 2);

    render_healthbar(&game->player, player);
    DrawRectangleRec(player, WHITE);


    /*  ===========================================
     *  Render Enemy.
     * */

    render_combat_phase_indicator(game);

    int rendered_enemies = 0;
    Enemy_Chain *chain = &game->enemies[game->chain_index];
    Rect_Builder b = rect_builder(render_size);

    Vector2 size = v2tile(1.5, 3.0);
    rb_set_position_percent(&b, 0.75, 0.5);
    rb_set_size_v2(&b, size);
    rb_reposition_by_pivot(&b, MIDDLE, CENTER);

    if (game->enemy_index < chain->enemy_count) {
        Actor *enemy = &chain->enemies[game->enemy_index];

        render_enemy(enemy, b.result, 1);
        for (int i = game->enemy_index + 1; i < chain->enemy_count; ++i) {
            Actor *enemy = &chain->enemies[i];
            rb_add_position_by(&b, TILE * 3, 0);
            render_enemy(enemy, b.result, 0);
        }
    }

    /* ======================================================
     * Render Queue.
     */
    switch(game->combat_state.current) {
    }

    {
        Rectangle layout = get_action_queue_layout();

        /* Queue number (as in ?/?) */
        {
            const char *format = TextFormat("%d / %d", game->player.action_count, ACTION_CAPACITY);
            float w = MeasureText(format, 18);
            DrawText(format, layout.x + (ACTION_CAPACITY * TILE * 0.5) - (w * 0.5), layout.y - 18 - 2, 18, WHITE);
        }

        for (int i = 0; i < ACTION_CAPACITY; ++i) {
            Rectangle r = get_rowslot_for_nth_tile(layout, i, 1, 8);

            if (i < game->player.action_count) {
                Action a = game->player.actions[i];
                Texture2D tex;

                if (get_texture((ASSET_ACTION_ICON_BEGIN + a.type), &tex)) {
                    Rectangle texdest = r;
                    texdest.x += 1;
                    texdest.y += 1;
                    texdest.width  -= 2;
                    texdest.height -= 2;

                    DrawRectangleRec(texdest, BLACK);
                    draw_texture_sane(tex, texdest, WHITE);
                }
            }

            int line_thickness = 1;
            if (i == game->player.action_index) {
                line_thickness = 4;
            }

            DrawRectangleLinesEx(r, line_thickness, WHITE);
        }
    }

    /* ======================================================
     * Render Actions.
     **/
    Rectangle layout = get_available_action_layout();

    for (int i = 0; i < fz_COUNTOF(base_actions); ++i) {
        Action a = base_actions[i];
        Texture2D tex;

        if (get_texture((ASSET_ACTION_ICON_BEGIN + a.type), &tex)) {
            Rectangle dest = get_rowslot_for_nth_tile(layout, i, 1.5, 8);
            Rectangle texdest = dest;
            texdest.x += 1;
            texdest.y += 1;
            texdest.width  -= 2;
            texdest.height -= 2;
            SetShaderValue(noise_shader, noise_shader_loc.strength_loc, &slot_strength[i], SHADER_UNIFORM_FLOAT);
            DrawRectangleRec(dest, BLACK);
            draw_texture_sane(tex, texdest, Fade(WHITE, slot_strength[i]));
            DrawRectangleLinesEx(dest, 1, WHITE);
        }
    }

    /* Reset shader options */
    SetShaderValue(noise_shader, noise_shader_loc.strength_loc, &BASE_SHADER_EFFECT_THRESHOLD, SHADER_UNIFORM_FLOAT);

}

void do_title_gui(Game *game) {
    Rectangle render_rect = render_size;
    Vector2 pos = { (float)render_rect.width * 0.5f, (float)render_rect.height * 0.65f };
    pos = align_text_by(pos, "Press Enter", MIDDLE, CENTER, TILE);

    Color w = Fade(WHITE, state_delta(&game->core_state));
    DrawTextEx(font, "Press Enter", pos, TILE, 0, w);

    if (is_transition_done(&game->core_state)) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_ENTER)) {
            set_next_state(&game->core_state, STAGE_SELECT, 0.5);
        }
    }
}

void do_stage_select_gui(Game *game) {
    Rectangle render_rect = render_size;

    Vector2 button_size = v2tile(4, 1.5);
    Rect_Builder b = rect_builder(render_rect);

    rb_set_position_percent(&b, 0.5, 0.65);
    rb_set_size_v2(&b, button_size);
    rb_reposition_by_pivot(&b, MIDDLE, CENTER);

    Rectangle r1 = b.result;

    Color w = Fade(WHITE, state_delta(&game->core_state));
    int flag = is_transition_done(&game->core_state) 
        ? (INTERACT_CLICK_LEFT | INTERACT_HOVERING)
        : (INTERACT_NONE);

    int stage_one = do_button_esque(hash("StageOne"), r1, 0, 0, flag, w);
    {
        Vector2 pivot = position_of_pivot(r1, MIDDLE, CENTER);
        pivot = align_text_by(pivot, "Stage 1", MIDDLE, CENTER, TILE);

        DrawTextEx(font, "Stage 1", pivot, TILE, 0, w);
    }

    r1.y += (TILE * 2.5);
    Rectangle r2 = r1;
    int stage_two = do_button_esque(hash("StageTwo"), r2, 0, 0, flag, w);
    {
        Vector2 pivot = position_of_pivot(r2, MIDDLE, CENTER);
        pivot = align_text_by(pivot, "Stage 2", MIDDLE, CENTER, TILE);

        DrawTextEx(font, "Stage 2", pivot, TILE, 0, w);
    }

    if (is_transition_done(&game->core_state)) {
        if (stage_one & INTERACT_CLICK_LEFT) {
            load_stage_one(game);
            set_next_state(&game->core_state, GAME_IN_PROGRESS, 2.0);
        }
        if (stage_two & INTERACT_CLICK_LEFT) {
            assert(0);
            set_next_state(&game->core_state, GAME_IN_PROGRESS, 2.0);
        }
    }

}

int effectsort(const void *a, const void *b) {
    return ((Effect*)a)->asset_id - ((Effect*)b)->asset_id;
}

void do_gui(Game *game) {

    BeginTextureMode(render_tex);
    ClearBackground(BLACK);
    /* Draw loading image */
    BeginMode2D(game->camera);

    {
        Texture2D t = {0};
        if (game->core_state.current == GAME_IN_PROGRESS) {
            assert(get_texture(ASSET_COMBAT_BACKGROUND, &t));
        } else {
            assert(get_texture(ASSET_STAGE_SELECT_BACKGROUND, &t));
        }
        Rectangle dest = render_size;
        draw_texture_sane(t, dest, WHITE);
    }

    switch(game->core_state.current) {
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

    VecSort(game->effects, effectsort);

    int current_asset_id = -1;
    Texture2D t = {0};
    for (int i = 0; i < VecLen(game->effects); ++i) {
        Effect *e = &game->effects[i];
        if (current_asset_id != e->asset_id) {
            if (get_texture(e->asset_id, &t)) {
                assert(t.height == 64 && (t.width % 64) == 0);
            }
            current_asset_id = e->asset_id;
        }

        assert((e->elapsed * 64) < t.width);
        Rectangle src  = { (float)e->elapsed * 64, 0, 64, 64 };
        Rectangle dest = e->rect;

        Vector2 origin = {0};
        DrawTexturePro(t, src, dest, origin, 1, BLACK);
    }

    EndMode2D();
    EndTextureMode();
}

int main(void) {
    void *arena_mem = fz_heapalloc(32 * fz_KB);

    fz_Arena arena = {0};
    fz_arena_init(&arena, arena_mem, 32 * fz_KB);
    fz_set_temp_allocator(fz_arena_allocator(&arena));

    InitWindow(window_size.width, window_size.height, "MainWindow");
    noise_shader       = LoadShader(0, "assets/shaders/noise_shader.fs");
    dither_shader      = LoadShader(0, "assets/shaders/dither_shader.fs");
    fade_inout_shader  = LoadShader(0, "assets/shaders/fade_in_shader.fs");

    set_shaderloc(&noise_shader, &noise_shader_loc);
    set_shaderloc(&dither_shader, &dither_shader_loc);
    set_shaderloc(&fade_inout_shader, &fade_inout_shader_loc);

    font = LoadFontEx("assets/fonts/EBGaramond-Regular.ttf", TILE, 0, 0);

    render_tex = LoadRenderTexture(render_size.width, render_size.height);
    SetTextureFilter(render_tex.texture, TEXTURE_FILTER_POINT);

    float a = 1;
    SetShaderValue(fade_inout_shader, fade_inout_shader_loc.strength_loc, &a, SHADER_UNIFORM_FLOAT);

    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        slot_strength[i] = slot_strength_target[i] = 0.5;
    }

    Game game = {0};
    game.enemies = VecCreate(Enemy_Chain, 10);
    game.effects = VecCreate(Effect, 32);
    set_next_state(&game.core_state,   TITLE_SCREEN,       0.1);
    set_next_state(&game.combat_state, COMBAT_STATE_BEGIN, 0.1);

    game.turn_interval.max = 0.5;
    game.effect_interval.max = 0.10;
    game.camera.zoom = 1.0;

    reset_combatstate(&game);

    /* Debug */

    load_tex_to_id(ASSET_STAGE_SELECT_BACKGROUND, "assets/images/loading.png");
    load_tex_to_id(ASSET_COMBAT_BACKGROUND,       "assets/images/background.png");
    load_tex_to_id(ASSET_ACTION_ICON_SLASH,  "assets/images/spinning-sword.png");
    load_tex_to_id(ASSET_ACTION_ICON_EVADE,  "assets/images/wingfoot.png");
    load_tex_to_id(ASSET_ACTION_ICON_PARRY,  "assets/images/sword-break.png");
    load_tex_to_id(ASSET_ACTION_ICON_TACKLE, "assets/images/shield-bash.png");

    load_tex_to_id(ASSET_EFFECT_SPRITE_RECEIVED_SLASH, "assets/images/slash_effect_sprite.png");

    load_sound_to_id(ASSET_SOUND_START_GAME, "assets/sounds/start_game.wav");
    load_sound_to_id(ASSET_SOUND_ACTION_SELECT, "assets/sounds/action_selection.wav");
    load_sound_to_id(ASSET_SOUND_ACTION_SUBMIT, "assets/sounds/action_submit.wav");
    accumulator = 0;

    while(!WindowShouldClose()) {
        fz_Temp_Memory t = fz_begin_temp(&arena);

        float dtime  = GetFrameTime();
        accumulator += dtime;

        {
            set_perframe_shader_uniform(noise_shader, noise_shader_loc);
            set_perframe_shader_uniform(fade_inout_shader, fade_inout_shader_loc);
        }
        Vector2 ws = { window_size.width, window_size.height };
        SetShaderValue(dither_shader, dither_shader_loc.resolution_loc, &ws, SHADER_UNIFORM_VEC2);

        update(&game, dtime);
        do_gui(&game);

        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(dither_shader);
            Rectangle swapped = render_size;
            swapped.height *= -1;
            Vector2 offset = {};
            DrawTexturePro(render_tex.texture, swapped, window_size, offset, 0, WHITE);
        EndShaderMode();
        draw_debug_information(&game);
        EndDrawing();

        fz_end_temp(t);
    }

    for (int i = 0; i < fz_COUNTOF(art_assets); ++i) {
        if (art_assets[i].is_loaded)
            UnloadTexture(art_assets[i].t);
    }

    for (int i = 0; i < fz_COUNTOF(sound_assets); ++i) {
        if (sound_assets[i].is_loaded)
            UnloadSound(sound_assets[i].sound);
    }

    VecRelease(game.enemies);
    VecRelease(game.effects);
    UnloadFont(font);
    UnloadRenderTexture(render_tex);
    UnloadShader(noise_shader);
    UnloadShader(dither_shader);
    UnloadShader(fade_inout_shader);
    CloseWindow();

    fz_heapfree(arena_mem);
    return 0;
}
