#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>

#define FUZZY_MY_H_IMPL
#define fz_NO_WINDOWS_H
#include "my.h"

/* Constants */
const float CHARACTER_Y_POSITION_FROM_TOP = 0.5;
#define TILE 80

fz_STATIC_ASSERT(TILE % 2 == 0);

/* Game globals */
static Rectangle window_size = { 0, 0, 1280,  720 };
static Rectangle render_size = { 0, 0, 1920, 1080 };
static Vector2 mouse_pos = {};

static RenderTexture2D render_tex;

struct Shader_Loc {
    int time_loc;
    int strength_loc;
    int resolution_loc;
};

static Shader     dither_shader;
static Shader_Loc dither_shader_loc;

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
            ASSET_PLAYER_STANDING,
            ASSET_PLAYER_SLASH,
            ASSET_PLAYER_EVADE,
            ASSET_PLAYER_PARRY,
            ASSET_PLAYER_TACKLE,
            ASSET_PLAYER_DIED,
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

    ASSET_MUSIC_BEGIN,
        ASSET_MUSIC_TITLE,
        ASSET_MUSIC_COMBAT,
    ASSET_MUSIC_END,

    ASSET_SOUND_BEGIN,
        ASSET_SOUND_START_GAME,
        ASSET_SOUND_ACTION_SELECT,
        ASSET_SOUND_ACTION_SUBMIT,
        ASSET_SOUND_ACTION_LOCKIN,

        ASSET_SOUND_SLASH,
        ASSET_SOUND_EVADE,
        ASSET_SOUND_PARRY,
        ASSET_SOUND_TACKLE,
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
Music        music_assets[ASSET_MUSIC_END - ASSET_MUSIC_BEGIN];

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

/* TODO: super weird */
void load_music_to_id(int position, const char *filename) {
    assert(ASSET_MUSIC_BEGIN < position && position < ASSET_MUSIC_END);
    music_assets[position - ASSET_MUSIC_BEGIN] = LoadMusicStream(filename);
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

#define ENEMY_CAPACITY 5

/* ============================================================
 *  Game Data And Core Structure.
 */

enum /* Core scene phase */
{
    TITLE_SCREEN,
    STAGE_SELECT,
    STAGE_LOADING,
    GAME_OVER,
    GAME_CLEAR,
    GAME_IN_PROGRESS,
    GAME_STATE_COUNT,
};

const char *game_state_to_char[GAME_STATE_COUNT] = {
    "Title Screen",
    "Stage Select",
    "Stage Loading",
    "Game Over",
    "Game Clear",
    "Game In Progress",
};

enum /* Combat State */
{
    COMBAT_STATE_NONE,
    COMBAT_STATE_BEGIN,
    COMBAT_STATE_PLAYER_PLANNING,
    COMBAT_STATE_RUNNING_TURN,
    COMBAT_STATE_PLAYER_DIED,
    COMBAT_STATE_ENEMY_DIED,
    COMBAT_STATE_GOING_NEXT_PHASE,
    COMBAT_STATE_STAGE_COMPLETE,
    COMBAT_STATE_COUNT,
};

const char *combat_state_to_char[COMBAT_STATE_COUNT] = {
    "None",
    "Begin",
    "Player Planning",
    "Running Turn",
    "Player Died",
    "Enemy Died",
    "Going next phase",
    "Stage Complete",
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

const char *action_type_to_name_char[ACTION_COUNT] = {
    "None",
    "Slash",
    "Evade",
    "Parry",
    "Tackle",
};

const char *action_type_to_description_char[ACTION_COUNT] = {
    "None",
    "Performs horizontal sweep with weapon, dealing 1 damage. blocked by Parry.",
    "Evades enemy attack by stepping sideways. evades Tackle.",
    "Parries enemy attack, preventing Slash.",
    "Charges straight towards enemy, dealing 1 damage. blocked by Evade.",
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

    int last_player_action;
    int last_enemy_action;

    float  player_hit_highlight_dt;
    float  enemy_hit_highlight_dt;

    int reset_count;

    int enemy_index;
    int chain_index;

    int current_music_playing;

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
    Rect_Builder builder = { { 0 } };
    builder.base   = base;
    builder.result = base;

    return builder;
}

/* ===================================================
 * Aligns current position of rectangle to passed position of parent.
 */

void rb_set_position_percent(Rect_Builder *b, float x_percent, float y_percent) {
    b->result.x = b->base.x + (b->base.width  * x_percent);
    b->result.y = b->base.y + (b->base.height * y_percent);
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

    rect.width  = (TILE * 1.25 - 8) * fz_COUNTOF(base_actions);
    rect.height = (TILE * 1.25 - 8);

    rect.x = render_size.width  * 0.5 - ((TILE * 1.25) * fz_COUNTOF(base_actions) * 0.5);
    rect.y = render_size.height * 0.9 - (TILE * 1.25) * 0.5;

    return rect;
}

Rectangle get_action_queue_layout(void) {
    Rectangle queued_layout = {};
    queued_layout.width  = ACTION_CAPACITY * (TILE - 8);
    queued_layout.height = (TILE - 8);

    queued_layout.x = render_size.width * 0.5   - (ACTION_CAPACITY * TILE / 2);
    queued_layout.y = render_size.height * 0.80 - (queued_layout.height / 2);

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
    game->reset_count = 3;
    game->player.health = game->player.max_health = 5;
    game->player.action_count = 0;

    VecClear(game->enemies);
}

float state_delta(State *state);

void load_stage_one(Game *game) {
    reset_combatstate(game);
    {
        Enemy_Chain chain = {0};

        /* First wave */
        chain.enemies[0].health = chain.enemies[0].max_health = 3;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_SLASH;
        chain.enemies[0].actions[chain.enemies[0].action_count++].type = ACTION_PARRY;

        chain.enemy_count++;
        chain.enemies[1].health = chain.enemies[1].max_health = 3;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_SLASH;
        chain.enemies[1].actions[chain.enemies[1].action_count++].type = ACTION_EVADE;

        chain.enemy_count++;
        chain.enemies[2].health = chain.enemies[2].max_health = 3;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_TACKLE;
        chain.enemies[2].actions[chain.enemies[2].action_count++].type = ACTION_SLASH;
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

        chain.enemy_count++;
        chain.enemies[3].health = chain.enemies[3].max_health = 3;
        chain.enemies[3].actions[chain.enemies[3].action_count++].type = ACTION_EVADE;
        chain.enemies[3].actions[chain.enemies[3].action_count++].type = ACTION_PARRY;
        chain.enemies[3].actions[chain.enemies[3].action_count++].type = ACTION_TACKLE;
        chain.enemies[3].actions[chain.enemies[3].action_count++].type = ACTION_TACKLE;

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

        chain.enemy_count++;
        chain.enemies[3].health = chain.enemies[3].max_health = 3;
        chain.enemies[3].actions[chain.enemies[3].action_count++].type = ACTION_EVADE;
        chain.enemies[3].actions[chain.enemies[3].action_count++].type = ACTION_PARRY;
        chain.enemies[3].actions[chain.enemies[3].action_count++].type = ACTION_TACKLE;
        chain.enemies[3].actions[chain.enemies[3].action_count++].type = ACTION_TACKLE;

        VecPush(game->enemies, chain);
    }
}

void draw_debug_information(Game *game) {
    Vector2 pos = { 10, 10 };
    DrawTextEx(font, "Game State:", pos, 32, 0, YELLOW);
    pos.y += 32;
    DrawTextEx(font, TextFormat("  Current: %s", game_state_to_char[game->core_state.current]), pos, 32, 0, YELLOW);

    pos.y += 32;
    DrawTextEx(font, TextFormat("  Next: %s", game_state_to_char[game->core_state.next]), pos, 32, 0, YELLOW);

    pos.y += 32;
    DrawTextEx(font, TextFormat("  Transition: %2.2f", state_delta(&game->core_state)), pos, 32, 0, YELLOW);

    pos.y += 32;
    DrawTextEx(font, "Combat State:", pos, 32, 0, YELLOW);

    pos.y += 32;
    DrawTextEx(font, TextFormat("  Current: %s", combat_state_to_char[game->combat_state.current]), pos, 32, 0, YELLOW);

    pos.y += 32;
    DrawTextEx(font, TextFormat("  Next: %s", combat_state_to_char[game->combat_state.next]), pos, 32, 0, YELLOW);

    pos.y += 32;
    DrawTextEx(font, TextFormat("  Transition: %2.2f", state_delta(&game->combat_state)), pos, 32, 0, YELLOW);

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
    static uint32_t last_hover = 0;

    DrawRectangleRec(rect, BLACK);
    if ((result & INTERACT_CLICK_LEFT) || (result & INTERACT_CLICK_RIGHT)) {
        Sound s;
        if(get_sound(ASSET_SOUND_ACTION_SUBMIT, &s)) {
            PlaySoundMulti(s);
        }

        DrawRectangleLinesEx(rect, 8, color);
    } if (result & INTERACT_HOVERING) {
        if (last_hover != id) {
            Sound s;
            if(get_sound(ASSET_SOUND_ACTION_SELECT, &s)) {
                PlaySoundMulti(s);
            }

            last_hover = id;
        }

        DrawRectangleLinesEx(rect, 4, color);
    } else {
        if (last_hover == id) last_hover = 0;
        DrawRectangleLinesEx(rect, 2, color);
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
    if (state->current == state->next) { /* Entering */
        if (state->max_transition == 0) return 1;

        return 1 - (state->transition / state->max_transition);
    } else { /* Leaving */
        if (state->max_transition == 0) return 0;

        return (state->transition / state->max_transition);
    }
}

float state_delta_against(State *state, int against) {
    if (state->max_transition == 0) return 0;

    if (state->current == against) {
        if (state->next == against) return 1;
        else {
            return (state->transition / state->max_transition);
        }
    } else if (state->next == against) {
        return 1 - (state->transition / state->max_transition);
    }

    return 0;
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

void update_music(Game *game) {
    Music title_music = music_assets[ASSET_MUSIC_TITLE - ASSET_MUSIC_BEGIN];
    Music combat_music = music_assets[ASSET_MUSIC_COMBAT - ASSET_MUSIC_BEGIN];

    if (game->current_music_playing == 0) {
        PlayMusicStream(title_music);
        game->current_music_playing = ASSET_MUSIC_TITLE;
        printf("Playing music.\n");
    }

    float fade = state_delta(&game->core_state);
    if (game->core_state.current == GAME_IN_PROGRESS) {
        if (game->current_music_playing == ASSET_MUSIC_TITLE) {
            StopMusicStream(title_music);
            PlayMusicStream(combat_music);

            game->current_music_playing = ASSET_MUSIC_COMBAT;
            printf("Changing music.\n");
        }
    } else {
        if (game->current_music_playing == ASSET_MUSIC_COMBAT) {
            StopMusicStream(combat_music);
            PlayMusicStream(title_music);

            game->current_music_playing = ASSET_MUSIC_TITLE;
            printf("Changing music.\n");
        }
    }

    Music music = music_assets[game->current_music_playing - ASSET_MUSIC_BEGIN];
    SetMusicVolume(music, fade);
    UpdateMusicStream(music);
}

void turn_tick(Game *game, float dt) {
    if (game->core_state.current != GAME_IN_PROGRESS) return;
    if (interval_tick(&game->turn_interval, dt)) {
        game->last_enemy_action = 0;
        game->last_player_action = 0;
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

        Actor *player = &game->player;
        Actor *enemy = &chain->enemies[game->enemy_index];

        if (enemy->health <= 0) { // Progress to next enemy then break
            if ((game->enemy_index + 1) == chain->enemy_count) {
                set_next_state(&game->combat_state, COMBAT_STATE_GOING_NEXT_PHASE, 1.0);
            } else {
                set_next_state(&game->combat_state, COMBAT_STATE_ENEMY_DIED, 0.25);
            }

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

        switch(player_action.type) {
            /* Offensive Maneuver */
            case ACTION_SLASH: switch(enemy_action.type) {
                case ACTION_PARRY:  /* blocks  */
                {
                    Sound s;
                    if (get_sound(ASSET_SOUND_PARRY, &s)) {
                        PlaySoundMulti(s);
                    }
                } break;


                default:
                {
                    Sound s;
                    if (get_sound(ASSET_SOUND_SLASH, &s)) {
                        PlaySoundMulti(s);
                    }
                    shake_camera(game, 8);
                    spawn_effect(game, ASSET_EFFECT_SPRITE_RECEIVED_SLASH, enemy_effect_rect);
                    enemy->health -= 1;
                    game->enemy_hit_highlight_dt = 0.2;
                } break;
            } break;

            case ACTION_TACKLE: switch(enemy_action.type) {
                case ACTION_EVADE:  /* blocked */
                {
                    Sound s;
                    if (get_sound(ASSET_SOUND_EVADE, &s)) {
                        PlaySoundMulti(s);
                    }
                } break;

                default:
                {
                    Sound s;
                    if (get_sound(ASSET_SOUND_TACKLE, &s)) {
                        PlaySoundMulti(s);
                    }

                    shake_camera(game, 8);
                    enemy->health -= 1;
                    game->enemy_hit_highlight_dt = 0.2;
                } break;
            } break;

            default:
                break;
        }

        switch(enemy_action.type) {
            /* Offensive Maneuver */
            case ACTION_SLASH: switch(player_action.type) {
                case ACTION_PARRY:  /* blocks  */
                {
                    Sound s;
                    if (get_sound(ASSET_SOUND_PARRY, &s)) {
                        PlaySoundMulti(s);
                    }
                } break;

                default:
                {
                    Sound s;
                    if (get_sound(ASSET_SOUND_SLASH, &s)) {
                        PlaySoundMulti(s);
                    }

                    shake_camera(game, 8);
                    spawn_effect(game, ASSET_EFFECT_SPRITE_RECEIVED_SLASH, player_effect_rect);
                    player->health -= 1;
                    game->player_hit_highlight_dt = 0.2;
                } break;
            } break;

            case ACTION_TACKLE: switch(player_action.type) {
                case ACTION_EVADE:  /* blocked */
                {
                    Sound s;
                    if (get_sound(ASSET_SOUND_EVADE, &s)) {
                        PlaySoundMulti(s);
                    }
                } break;

                default:
                {
                    Sound s;
                    if (get_sound(ASSET_SOUND_TACKLE, &s)) {
                        PlaySoundMulti(s);
                    }
                    shake_camera(game, 8);
                    player->health -= 1;
                    game->player_hit_highlight_dt = 0.2;
                } break;
            } break;

            default:
                break;
        }

        game->last_player_action = player_action.type;
        game->last_enemy_action  = enemy_action.type;
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
    float x_ratio = (render_size.width  / window_size.width);
    float y_ratio = (render_size.height / window_size.height);
    Vector2 m = GetMousePosition();
    mouse_pos.x = m.x * x_ratio;
    mouse_pos.y = m.y * y_ratio;

    /* animate camera */
    game->camerashake_shift_distance -= dt * 20;
    if (game->camerashake_shift_distance < 0)
        game->camerashake_shift_distance = 0;

    if ((game->turn_interval.current / game->turn_interval.max) > 0.5) {
        game->last_player_action = -1;
        game->last_enemy_action = -1;
    }

    game->player_hit_highlight_dt -= dt;
    game->enemy_hit_highlight_dt -= dt;

    if (game->player_hit_highlight_dt < 0) game->player_hit_highlight_dt = 0;
    if (game->enemy_hit_highlight_dt < 0)  game->enemy_hit_highlight_dt = 0;

    float a = 1 + (game->camerashake_shift_distance / 8);
    SetShaderValue(dither_shader, dither_shader_loc.strength_loc, &a, SHADER_UNIFORM_FLOAT);

    /* Update music */
    update_music(game);

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

    if(game->core_state.current == GAME_IN_PROGRESS) {
        switch(game->combat_state.current) {
            case COMBAT_STATE_BEGIN:
            {
                if (is_transition_done(&game->combat_state))
                    set_next_state(&game->combat_state, COMBAT_STATE_PLAYER_PLANNING, 0.25);
            } break;

            case COMBAT_STATE_GOING_NEXT_PHASE:
            {
                if (is_transition_done(&game->combat_state)) {
                    if (game->chain_index <= VecLen(game->enemies)) {
                        game->chain_index++;
                        game->enemy_index = 0;
                    } else {
                        set_next_state(&game->combat_state, COMBAT_STATE_STAGE_COMPLETE, 0.01);
                    }
                    set_next_state(&game->combat_state, COMBAT_STATE_PLAYER_PLANNING, 0.25);
                }
            } break;

            case COMBAT_STATE_PLAYER_PLANNING:
            {
                if (is_transition_done(&game->combat_state)) {
                    if(IsKeyPressed('H')) {
                        game->player.health = 0;
                        set_next_state(&game->combat_state, COMBAT_STATE_RUNNING_TURN, 0.25);
                    }
                    else if(IsKeyPressed('S')) {
                        game->chain_index = VecLen(game->enemies);
                        set_next_state(&game->combat_state, COMBAT_STATE_RUNNING_TURN, 0.25);
                    }
                    else if(IsKeyPressed('I')) {
                        Enemy_Chain *chain = &game->enemies[game->chain_index];
                        for (int i = 0; i < chain->enemy_count; ++i) {
                            chain->enemies[i].health = 0;
                        }
                        set_next_state(&game->combat_state, COMBAT_STATE_RUNNING_TURN, 0.25);
                    }
                }
            } break;

            case COMBAT_STATE_ENEMY_DIED:
            {
                if (is_transition_done(&game->combat_state)) {
                    Enemy_Chain *current = &game->enemies[game->chain_index];
                    if ((game->enemy_index + 1) < current->enemy_count) {
                        game->enemy_index++;
                    } else {
                        set_next_state(&game->combat_state, COMBAT_STATE_GOING_NEXT_PHASE, 0.05);
                    }
                    set_next_state(&game->combat_state, COMBAT_STATE_PLAYER_PLANNING, 0.25);
                }
            } break;

            case COMBAT_STATE_RUNNING_TURN:
            {
                if (is_transition_done(&game->combat_state)) {
                    turn_tick(game, dt);
                }
            } break;

            case COMBAT_STATE_PLAYER_DIED:
            {
                if (is_transition_done(&game->combat_state)) {
                    set_next_state(&game->core_state, GAME_OVER, 1.0);
                }
            } break;

            case COMBAT_STATE_STAGE_COMPLETE:
            {
                if (is_transition_done(&game->combat_state)) {
                    set_next_state(&game->core_state, GAME_CLEAR, 1.0);
                }
            } break;
        }
    }
}

/* ============================================================
 * Rendering / GUI.
 */

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
        health_bar.x += TILE + 2;
    }
}

void render_action_icon(Action *action, Rectangle rect, int line_thickness, float bg_activeness, float fg_activeness) {
    Color bg = Fade(BLACK, bg_activeness);
    Color fg = Fade(WHITE, fg_activeness);

    DrawRectangleRec(rect, bg);

    if (action) {
        Texture2D tex;
        if (get_texture((ASSET_ACTION_ICON_BEGIN + action->type), &tex)) {
            Rectangle texdest = rect;
            texdest.x += 1;
            texdest.y += 1;
            texdest.width  -= 2;
            texdest.height -= 2;

            draw_texture_sane(tex, texdest, fg);
        }
    }

    DrawRectangleLinesEx(rect, line_thickness, fg);
}

void render_action_queue(Actor *actor, Rectangle actor_rect) {
    Rectangle queue = {0};
    Vector2 position = position_of_pivot(actor_rect, TOP, CENTER);
    Vector2 action_size = v2tile(0.8, 0.8);

    queue.width  = action_size.x - 4;
    queue.height = action_size.y - 4;

    queue.x = position.x - (actor->action_count * action_size.x) * 0.5;
    queue.y = position.y - (action_size.y * 1.5);


    for (int i = 0; i < actor->action_count; ++i) {
        float bg_activeness = 1;
        float fg_activeness = (i == actor->action_index) ? 1 : 0.75;
        int   thickness     = (i == actor->action_index) ? 4 : 2;

        Action *a = &actor->actions[i];
        render_action_icon(a, queue, thickness, bg_activeness, fg_activeness);

        queue.x += action_size.x + 2;
    }

}


void render_enemy(Game *game, Actor *enemy, Rectangle rect, int is_active_participant) {
    if (is_active_participant) {
        render_healthbar(enemy, rect);
        render_action_queue(enemy, rect);
    }

    Color c = WHITE;
    if (game->enemy_hit_highlight_dt > 0 || !is_active_participant) {
        c = Fade(WHITE, 0.5);
    }

    DrawRectangleRec(rect, c);
}

void render_combat_phase_indicator(Game *game) {
    float x = render_size.width * 0.5;
    float y = render_size.height * 0.15;

    int chain_count = (int)VecLen(game->enemies);
    float gap_between    = TILE * 1.5;
    float indicator_size = 24; /* px */
    float fullwidth = chain_count * indicator_size + (chain_count - 1) * gap_between;
    x -= fullwidth * 0.5;

    for (int i = 0; i < chain_count; ++i) {
        if (i == game->chain_index) {
            DrawCircle(x, y, indicator_size * 0.85, WHITE);
        } else {
            DrawCircleLines(x, y, indicator_size * 0.75, WHITE);
        }

        x += indicator_size;

        if (i < (chain_count - 1)) {
            Vector2 line_begin = { x, y };
            Vector2 line_end   = { x + gap_between, y };

            DrawLineEx(line_begin, line_end, 8, WHITE);
            x += gap_between + indicator_size;
        }
    }
}

void render_player(Game *game) {
    Rectangle player = {};
    player.width  = 4.5 * TILE;
    player.height = 4.5 * TILE;
    player.x = render_tex.texture.width  * 0.25 - (player.width / 2);
    player.y = render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP - (player.height / 2);
    /* TODO: player position should be moved somewhere else */

    int texture_id = ASSET_PLAYER_STANDING;

    if (game->player.health <= 0) {
        texture_id = ASSET_PLAYER_DIED;
    } else {
        switch(game->last_player_action) {
            case ACTION_SLASH:  texture_id = ASSET_PLAYER_SLASH;  break;
            case ACTION_EVADE:  texture_id = ASSET_PLAYER_EVADE;  break;
            case ACTION_PARRY:  texture_id = ASSET_PLAYER_PARRY;  break;
            case ACTION_TACKLE: texture_id = ASSET_PLAYER_TACKLE; break;
            default:
                texture_id = ASSET_PLAYER_STANDING;
        }
    }

    Color c = WHITE;
    if (game->player_hit_highlight_dt > 0) {
        c = Fade(WHITE, 0.5);
    }

    Texture2D t;
    if (get_texture(texture_id, &t)) {
        draw_texture_sane(t, player, c);
    }

    render_healthbar(&game->player, player);
}

void do_combat_gui(Game *game) {
    render_player(game);

    /*  ===========================================
     *  Render Enemy.
     * */

    render_combat_phase_indicator(game);

    Enemy_Chain *chain = &game->enemies[game->chain_index];
    Rect_Builder b = rect_builder(render_size);

    Vector2 size = v2tile(1.5, 3.0);
    rb_set_position_percent(&b, 0.75, 0.5);
    rb_set_size_v2(&b, size);
    rb_reposition_by_pivot(&b, MIDDLE, CENTER);

    if (game->enemy_index < chain->enemy_count) {
        Actor *enemy = &chain->enemies[game->enemy_index];

        render_enemy(game, enemy, b.result, 1);
        for (int i = game->enemy_index + 1; i < chain->enemy_count; ++i) {
            Actor *enemy = &chain->enemies[i];
            rb_add_position_by(&b, TILE * 3, 0);
            render_enemy(game, enemy, b.result, 0);
        }
    }

    float state_activeness = state_delta(&game->combat_state);
    switch(game->combat_state.current) {
        case COMBAT_STATE_BEGIN:
        {
            Vector2 r = {
                render_size.width * 0.5f,
                render_size.height * 0.5f
            };

            Color c = Fade(WHITE, state_activeness);
            Vector2 pos = align_text_by(r, "BEGIN", MIDDLE, CENTER, TILE * 1.5);
            DrawTextEx(font, "BEGIN", pos, TILE * 1.5, 0, c);
        } break;

        case COMBAT_STATE_GOING_NEXT_PHASE:
        {
            Vector2 r = {
                render_size.width * 0.5f,
                render_size.height * 0.5f
            };

            Color c = Fade(WHITE, state_activeness);
            const char *format = TextFormat("Phase %d", game->chain_index + 1);
            Vector2 pos = align_text_by(r, format, MIDDLE, CENTER, TILE * 1.5);
            DrawTextEx(font, format, pos, TILE * 1.5, 0, c);
        } break;

        case COMBAT_STATE_ENEMY_DIED:
        {
        } break;

        case COMBAT_STATE_PLAYER_DIED:
        {
            Vector2 r = {
                render_size.width * 0.5f,
                render_size.height * 0.5f
            };

            Color c = Fade(WHITE, state_activeness);
            Vector2 pos = align_text_by(r, "You're killed", MIDDLE, CENTER, TILE * 1.5);
            DrawTextEx(font, "You're killed", pos, TILE * 1.5, 0, c);
        } break;

        case COMBAT_STATE_RUNNING_TURN:
        {
        } break;
    }

    /* ======================================================
     * Render Queue.
     */
    float usable_button_activeness = state_delta_against(&game->combat_state, COMBAT_STATE_PLAYER_PLANNING);
    usable_button_activeness = 0.5 + (usable_button_activeness * usable_button_activeness * 0.5);
    {
        Rectangle layout = get_action_queue_layout();

        {
            Vector2 play_button_size = v2tile(3, 1);
            Rectangle button;

            button.width  = play_button_size.x;
            button.height = play_button_size.y;
            button.x = (render_size.width  * 0.5)  - (play_button_size.x * 0.5);
            button.y = (render_size.height * 0.65) - (play_button_size.y * 0.5);

            float activeness = usable_button_activeness;
            if (game->player.action_count == 0) {
                activeness = 0.5;
            }

            int flags = 0;
            if (game->player.action_count > 0
                && game->combat_state.current == COMBAT_STATE_PLAYER_PLANNING
                && is_transition_done(&game->combat_state))
            {
                flags = (INTERACT_CLICK_LEFT | INTERACT_HOVERING);
            }
            int pressed = do_button_esque(hash("play"), button, "Lock in", TILE * 0.75, flags, Fade(WHITE, activeness));

            if (pressed & INTERACT_CLICK_LEFT) {
                game->locked_in_index = game->player.action_count;
                Sound s;
                if (get_sound(ASSET_SOUND_ACTION_LOCKIN, &s)) {
                    PlaySoundMulti(s);
                }
                set_next_state(&game->combat_state, COMBAT_STATE_RUNNING_TURN, 0.5);
            }
        }

        /* Queue number (as in ?/?) */
        {
            const char *format = TextFormat("%d / %d", game->player.action_count, ACTION_CAPACITY);
            float w = MeasureText(format, 18);
            DrawText(format, layout.x + (ACTION_CAPACITY * TILE * 0.5) - (w * 0.5), layout.y - 18 - 2, 18, WHITE);
        }

        Rectangle queued_layout = get_action_queue_layout();

        static int last_deleting_index = -1;
        int deleting = -1;

        for (int i = 0; i < ACTION_CAPACITY; ++i) {
            Rectangle r = get_rowslot_for_nth_tile(layout, i, 1, 8);
            float bg_activeness = (i < game->locked_in_index) ? 0.75 : 1;
            float fg_activeness = usable_button_activeness;

            Action *a = 0;
            if (i < game->player.action_count) {
                a = &game->player.actions[i];
            }

            int line_thickness = 1;
            if (i == game->player.action_index) {
                line_thickness = 4;
            }

            render_action_icon(a, r, line_thickness, bg_activeness, fg_activeness);

            if (CheckCollisionPointRec(mouse_pos, r) && (i < game->player.action_count)) {
                deleting = i;
            }
        }

        Vector2 reset = {
            queued_layout.x + queued_layout.width + TILE * 2,
            queued_layout.y
        };

        Vector2 size = { TILE * 2, queued_layout.height };
        Rectangle r = rectv2(reset, size);

        int   flag = 0;
        float reset_button_alpha = 0.5;
        if (game->combat_state.current == COMBAT_STATE_PLAYER_PLANNING
            && is_transition_done(&game->combat_state)
            && game->reset_count > 0
            && game->locked_in_index > -1)
        {
            flag = INTERACT_HOVERING | INTERACT_CLICK_LEFT;
            reset_button_alpha = 1;
        }

        const char *text = TextFormat("Reset (%d)", game->reset_count);
        int reset_has_been_pressed = do_button_esque(hash("Reset"), r, text, TILE * 0.75, flag, Fade(WHITE, reset_button_alpha));

        if (game->combat_state.current == COMBAT_STATE_PLAYER_PLANNING
            && is_transition_done(&game->combat_state))
        {
            if (reset_has_been_pressed & INTERACT_CLICK_LEFT) {
                game->locked_in_index = -1;
                game->player.action_count = 0;
                game->reset_count--;
            }

            if (deleting != -1 && game->locked_in_index <= deleting) {
                int action_type  = game->player.actions[deleting].type;
                const char *name = action_type_to_name_char[action_type];
 
                const char *text = TextFormat("Remove %s", name);
                Vector2 size = Vector2Add(MeasureTextEx(font, text, TILE * 0.75, 0), {10, 10});
                Vector2 pos = mouse_pos;

                DrawRectangleRec(rectv2(pos, size), Fade(BLACK, 0.5));
                DrawTextEx(font, text, Vector2Add(pos, { 5, 5 }), TILE*0.75, 0, WHITE);

                if (last_deleting_index != deleting) {
                    Sound s;
                    if(get_sound(ASSET_SOUND_ACTION_SELECT, &s)) {
                        PlaySoundMulti(s);
                    }
                    last_deleting_index = deleting;
                }

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    Sound s;
                    if(get_sound(ASSET_SOUND_ACTION_SUBMIT, &s)) {
                        PlaySoundMulti(s);
                    }

                    if (deleting < (game->player.action_count - 1)) {
                        memmove(&game->player.actions[deleting],
                                &game->player.actions[deleting + 1],
                                game->player.action_count - deleting);
                    }
                    game->player.action_count -= 1;
                }
            }
        }
        last_deleting_index = deleting;
    }

    /* ======================================================
     * Render Actions.
     **/
    {
        static int last_selected_index = -1; /* TODO */

        Rectangle layout = get_available_action_layout();
        int selected = -1;

        for (int i = 0; i < fz_COUNTOF(base_actions); ++i) {
            Rectangle dest = get_rowslot_for_nth_tile(layout, i, 1.25, 8);
            Action *a = (Action *)&base_actions[i];

            float bg_activeness = 1;
            float fg_activeness = usable_button_activeness;

            render_action_icon(a, dest, 1, bg_activeness, fg_activeness);

            if (CheckCollisionPointRec(mouse_pos, dest)) {
                selected                = i;
                slot_strength_target[i] = 1;
            }
        }


        if (game->combat_state.current == COMBAT_STATE_PLAYER_PLANNING
            && is_transition_done(&game->combat_state)) {
            if (selected != -1) {
                if (last_selected_index != selected) {
                    Sound s;
                    if(get_sound(ASSET_SOUND_ACTION_SELECT, &s)) {
                        PlaySoundMulti(s);
                    }
                    last_selected_index = selected;
                }

                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    Action a = base_actions[selected];
                    if (game->player.action_count < ACTION_CAPACITY) {
                        Sound s;
                        if(get_sound(ASSET_SOUND_ACTION_SUBMIT, &s)) {
                            PlaySoundMulti(s);
                        }
                        game->player.actions[game->player.action_count++] = a;
                    }
                }
            }
        }
        last_selected_index = selected;
    }
}

void do_title_gui(Game *game) {
    Rectangle render_rect = render_size;
    Color w = Fade(WHITE, state_delta(&game->core_state));

    Vector2 title = { (float)render_rect.width * 0.5f, (float)render_rect.height * 0.35f };
    title = align_text_by(title, "Ring Buffer", MIDDLE, CENTER, TILE * 3);
    DrawTextEx(font, "Ring Buffer", title, TILE * 3, 0, w);

    Vector2 pos = { (float)render_rect.width * 0.5f, (float)render_rect.height * 0.65f };
    pos = align_text_by(pos, "Press Enter", MIDDLE, CENTER, TILE);
    DrawTextEx(font, "Press Enter", pos, TILE, 0, w);

    if (is_transition_done(&game->core_state)) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_ENTER)) {
            set_next_state(&game->core_state, STAGE_SELECT, 0.5);
        }
    }
}

void do_stage_select_gui(Game *game) {
    Rectangle render_rect = render_size;

    const char *text[] = {
        "This game is about you, a robot with corrupted memory issues, fight against the horde of defunct robots to stay alive.",
        "Each combat, you're forced to insert at LEAST one action into your `ring buffer` with maximum capacity of 10.",
        "when you press `Lock in` after your action is in place, your character will automatically act on your plan, each time moving to next action.",
        "when your predefined action runs out, the buffer `loops` and your character will start over from the beginning of the buffer.",
        "",
        "Each action will be:",
        "  Slash: depicted by sword icon, character deals 1 damage to enemy. will be blocked by parry.",
        "  Tackle: depicted by shield-charge icon, character deals 1 damage to enemy. will be blocked by evade.",
        "  Evade: depicted by boots with feather icon, character blocks enemy's Tackle attack.",
        "  Parry: depicted by breaking-sword icon, character blocks enemy's slash attack.",
        "",
        "BE CAREFUL: every time you lock in, your character literally LOCKS your decision in place and prevents you from removing in the future.",
        "exhausting every slot in the buffer early will put you into a very difficult position later on.",
        "plan ahead carefully, prepare for any possible situation, and destroy all enemies without getting hit 5 times!!",
    };

    {
        Vector2 tutorial_size = {0};
        tutorial_size.x = render_size.width * 0.85;
        tutorial_size.y = TILE * 0.75 * fz_COUNTOF(text);

        Rectangle r = {
            0,0,
            tutorial_size.x,
            tutorial_size.y,
        };

        r.x = (render_rect.width * 0.5) - (tutorial_size.x * 0.5);
        r.y = (render_rect.height * 0.5) - (tutorial_size.y * 0.5);

        DrawRectangleRec(r, BLACK);
        DrawRectangleLinesEx(r, 2, WHITE);
        Vector2 pos = { r.x + 5, r.y + 2 };
        for(int i = 0; i < fz_COUNTOF(text); ++i) {
            DrawTextEx(font, text[i], pos, TILE * 0.5, 0, WHITE);
            pos.y += TILE * 0.5;
        }
    }

    {
        Rect_Builder b = rect_builder(render_rect);
        Vector2 button_size = v2tile(4, 1.5);
        rb_set_position_percent(&b, 0.5, 0.85);
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
            pivot = align_text_by(pivot, "Begin Game", MIDDLE, CENTER, TILE);

            DrawTextEx(font, "Begin Game", pivot, TILE, 0, w);
        }

        if (stage_one & INTERACT_CLICK_LEFT) {
            load_stage_one(game);
            set_next_state(&game->core_state, GAME_IN_PROGRESS, 5.0);
            set_next_state(&game->combat_state, COMBAT_STATE_BEGIN, 7.0);
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
        case GAME_CLEAR:
        case GAME_OVER:
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
        DrawTexturePro(t, src, dest, origin, 1, WHITE);
    }

    EndMode2D();
    EndTextureMode();
}

int main(void) {
    InitWindow(window_size.width, window_size.height, "MainWindow");
    InitAudioDevice();

    void *arena_mem = fz_heapalloc(32 * fz_KB);

    fz_Arena arena = {0};
    fz_arena_init(&arena, arena_mem, 32 * fz_KB);
    fz_set_temp_allocator(fz_arena_allocator(&arena));

    dither_shader = LoadShader(0, "assets/shaders/dither_shader.fs");
    set_shaderloc(&dither_shader, &dither_shader_loc);

    float a = 1;
    SetShaderValue(dither_shader, dither_shader_loc.strength_loc, &a, SHADER_UNIFORM_FLOAT);

    font = LoadFontEx("assets/fonts/EBGaramond-Regular.ttf", TILE, 0, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    render_tex = LoadRenderTexture(render_size.width, render_size.height);

    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        slot_strength[i] = slot_strength_target[i] = 0.5;
    }

    Game game = {{0}};
    game.enemies = VecCreate(Enemy_Chain, 10);
    game.effects = VecCreate(Effect, 32);
    set_next_state(&game.core_state,   TITLE_SCREEN,       0.1);
    set_next_state(&game.combat_state, COMBAT_STATE_NONE, 1);

    game.turn_interval.max = 0.5;
    game.effect_interval.max = 0.10;
    game.camera.zoom = 1.0;

    reset_combatstate(&game);

    /* Debug */
    load_tex_to_id(ASSET_STAGE_SELECT_BACKGROUND, "assets/images/loading2.png");
    load_tex_to_id(ASSET_COMBAT_BACKGROUND,   "assets/images/background.png");
    load_tex_to_id(ASSET_ACTION_ICON_SLASH,  "assets/images/spinning-sword.png");
    load_tex_to_id(ASSET_ACTION_ICON_EVADE,  "assets/images/wingfoot.png");
    load_tex_to_id(ASSET_ACTION_ICON_PARRY,  "assets/images/sword-break.png");
    load_tex_to_id(ASSET_ACTION_ICON_TACKLE, "assets/images/shield-bash.png");

    load_tex_to_id(ASSET_PLAYER_STANDING, "assets/images/player_standing.png");
    load_tex_to_id(ASSET_PLAYER_SLASH, "assets/images/player_slash.png");
    load_tex_to_id(ASSET_PLAYER_EVADE, "assets/images/player_evade.png");
    load_tex_to_id(ASSET_PLAYER_PARRY, "assets/images/player_parry.png");
    load_tex_to_id(ASSET_PLAYER_TACKLE, "assets/images/player_tackle.png");
    load_tex_to_id(ASSET_PLAYER_DIED, "assets/images/player_died.png");

    load_tex_to_id(ASSET_EFFECT_SPRITE_RECEIVED_SLASH, "assets/images/slash_effect_sprite.png");

    load_sound_to_id(ASSET_SOUND_START_GAME, "assets/sounds/start_game.wav");
    load_sound_to_id(ASSET_SOUND_ACTION_SELECT, "assets/sounds/action_selection.wav");
    load_sound_to_id(ASSET_SOUND_ACTION_SUBMIT, "assets/sounds/action_submit.wav");

    load_sound_to_id(ASSET_SOUND_START_GAME, "assets/sounds/start_game.wav");
    load_sound_to_id(ASSET_SOUND_ACTION_SELECT, "assets/sounds/action_selection.wav");
    load_sound_to_id(ASSET_SOUND_ACTION_SUBMIT, "assets/sounds/action_submit.wav");
    load_sound_to_id(ASSET_SOUND_ACTION_LOCKIN, "assets/sounds/lockin.wav");

    load_sound_to_id(ASSET_SOUND_SLASH,  "assets/sounds/slash.wav");
    load_sound_to_id(ASSET_SOUND_EVADE,  "assets/sounds/evade.wav");
    load_sound_to_id(ASSET_SOUND_PARRY,  "assets/sounds/parry.wav");
    load_sound_to_id(ASSET_SOUND_TACKLE, "assets/sounds/tackle.wav");

    load_music_to_id(ASSET_MUSIC_TITLE, "assets/sounds/terrible_loading_screen.wav");
    load_music_to_id(ASSET_MUSIC_COMBAT, "assets/sounds/terrible_combat_bgm.wav");


    while(!WindowShouldClose()) {
        fz_Temp_Memory t = fz_begin_temp(&arena);

        float dtime  = GetFrameTime();

        Vector2 ws = { window_size.width, window_size.height };
        SetShaderValue(dither_shader, dither_shader_loc.resolution_loc, &ws, SHADER_UNIFORM_VEC2);

        update(&game, dtime);
        do_gui(&game);

        BeginDrawing();
        ClearBackground(BLACK);
        BeginShaderMode(dither_shader);
            float x = state_delta(&game.core_state);
            x *= x;
            Rectangle swapped = render_size;
            swapped.height *= -1;
            Vector2 offset = {};
            DrawTexturePro(render_tex.texture, swapped, window_size, offset, 0, Fade(WHITE, x));
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

    for (int i = 0; i < fz_COUNTOF(music_assets); ++i) {
        UnloadMusicStream(music_assets[i]);
    }


    VecRelease(game.enemies);
    VecRelease(game.effects);
    UnloadFont(font);
    UnloadRenderTexture(render_tex);
    UnloadShader(dither_shader);
    CloseAudioDevice();
    CloseWindow();

    fz_heapfree(arena_mem);
    return 0;
}
