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
static Rectangle window_size = { 0, 0, 1280, 720 };
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
    ASSET_TEXTURE_BEGIN,
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

        ASSET_EFFECT_SPRITE_BEGIN,
            ASSET_EFFECT_SPRITE_RECEIVED_SLASH,
        ASSET_EFFECT_SPRITE_END,
    ASSET_TEXTURE_END,

    ASSET_SOUND_BEGIN,
        ASSET_SOUND_START_GAME,
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

#define EVENT_LIST \
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

struct Game {
    int core_state;
    int next_core_state;

    Interval accum_interval;
    Interval turn_interval;
    Interval effect_interval;

    float transition;
    float max_transition;

    int player_act;
    Actor player;

    int enemy_index;
    int chain_index;

    Vec(Enemy_Chain) enemies;
    Vec(Effect)      effects;
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
}

/* ===================================================
 * Aligns current position of rectangle to passed position of parent.
 */

void rb_set_position_percent(Rect_Builder *b, float x_percent, float y_percent) {
    b->result.x = b->base.x  + (b->base.width  * x_percent);
    b->result.y = b->base.y  + (b->base.height * y_percent);
}

/* ===================================================
 * readjust the position of current rectangle to
 * match the given pivot.
 */
void rb_adjust_by_pivot(Rect_Builder *b, int y_pivot, int x_pivot) {
}

void rb_set_size_v2(Rect_Builder *b, Vector2 element_size, int y_pivot, int x_pivot) {
    b->result.width  = element_size.x;
    b->result.height = element_size.x;
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

Vector2 rb_get_coord_of(Rect_Builder *b, int y_axis, int x_axis) {
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

void load_stage_one(Game *game) {
    game->player.health = game->player.max_health = 5;

    VecClear(game->enemies);
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
    DrawTextEx(font, TextFormat("Core Game State: %s", game_state_to_char[game->core_state]), pos, 32, 0, YELLOW);

    pos.y += 32;
    DrawTextEx(font, TextFormat("Effect count: %d", (int)VecLen(game->effects)), pos, 32, 0, YELLOW);

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

void spawn_effect(Game *game, int effect_id, Rectangle rect) {
    assert(ASSET_EFFECT_SPRITE_BEGIN < effect_id && effect_id < ASSET_EFFECT_SPRITE_END);
    Texture2D tex;
    if (!get_texture(effect_id, &tex)) {
        printf("Asset %d is not loaded. cannot spawn effects.\n", effect_id);
        return;
    }
    assert(tex.height == 64);
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
// 

int interval_tick(Interval *interval, float dt) {
    interval->current += dt;
    if (interval->max < interval->current) {
        interval->current -= interval->max;
        if(interval->current < 0) interval->current = 0;

        return 1;
    }
    return 0;
}

void set_next_state(Game *game, int state_to, float transition_seconds) {
    printf("next state: %d\n", state_to);
    if (game->core_state != state_to && game->next_core_state != state_to) {
        game->next_core_state = state_to;
        game->transition = game->max_transition = transition_seconds;
    }
}

float state_activeness(Game *game, int state_for) {
    if (game->core_state == state_for) {
        if (game->next_core_state == state_for) {
            return 1 - (game->transition / game->max_transition);
        } else {
            /* Leaving -- slowly fade out */
            return (game->transition / game->max_transition);
        }
    }

    return 0;
}

Action get_next_action_for(Actor *actor) {
    Action action = actor->actions[actor->action_index];
    actor->action_index = (actor->action_index + 1) % actor->action_count;

    return action;
}

void turn_tick(Game *game, float dt) {
    if (interval_tick(&game->turn_interval, dt)) {
        /*
        Do turn stuff.
        */
        /* Check if player / enemies are allowed to continue fighting. */
        Enemy_Chain *chain = &game->enemies[game->chain_index];

        if (game->player.health <= 0) { printf("Player is dead!\n"); return; } // Player is dead
        if (game->chain_index == VecLen(game->enemies)) { printf("Enemy is all dead!\n"); return; }// Enemy index is same as enemy capacity: enemy is all dead

        if (game->player_act) {
            /* Do actual matchup! */
            printf("Player acting...\n");

            Actor *enemy = &chain->enemies[game->enemy_index];
            if (enemy->health <= 0) { // Progress to next enemy then break
                game->enemy_index++;
                game->player_act = 0;

                if (game->enemy_index == chain->enemy_count) {
                    /* phase transition */
                    game->chain_index++;
                }

                return;
            }


            Action player_action = get_next_action_for(&game->player);
            Action enemy_action  = get_next_action_for(enemy);

            Vector2 enemy_effect_pos;
            Vector2 enemy_effect_size;
            enemy_effect_pos.x = (render_tex.texture.width * 0.75);
            enemy_effect_pos.y = (render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP);
            enemy_effect_size = { TILE * 2, TILE * 2 };

            Vector2 player_effect_pos;
            Vector2 player_effect_size;
            player_effect_pos.x = (render_tex.texture.width * 0.25) + TILE;
            player_effect_pos.y = (render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP);
            player_effect_size = { -TILE * 2, TILE * 2 };

            Rectangle enemy_effect_rect  = rectv2(enemy_effect_pos, enemy_effect_size);
            Rectangle player_effect_rect = rectv2(player_effect_pos, player_effect_size);

            switch(player_action.type) {
                /* Offensive Maneuver */
                case ACTION_SLASH: switch(enemy_action.type) {
                    case ACTION_PARRY:  /* blocks  */
                        break;

                    case ACTION_SLASH:  /* deals   */
                    case ACTION_EVADE:  /* deals   */
                    case ACTION_TACKLE: /* deals   */
                    {
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
                        spawn_effect(game, ASSET_EFFECT_SPRITE_RECEIVED_SLASH, player_effect_rect);
                        game->player.health -= 1;
                    } break;
                } break;
            }
        }
    }
}

void transition_tick(Game *game, float dt) {
    if (game->core_state != game->next_core_state) {
        if (game->transition <= 0) {
            game->core_state = game->next_core_state;
            game->transition = game->max_transition;
        }
    }

    if (game->transition > 0) game->transition -= dt;
    if (game->transition < 0) game->transition = 0;
}


void effects_tick(Game *game, float dt) {
    if (interval_tick(&game->effect_interval, dt)) {
        Vec(int) deleting_index = VecCreateEx(int, VecLen(game->effects) + 1, fz_global_temp_allocator);
        for (int i = 0; i < VecLen(game->effects); ++i) {
            Effect *e = &game->effects[i];
            e->elapsed += 1;
            if (e->elapsed == e->max_life) {
                VecPush(deleting_index, i);
            }
        }

        for (int i = VecLen(deleting_index); i > 0; --i) {
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
    mouse_pos = Vector2Scale(GetMousePosition(), 2);

    transition_tick(game, dt);
    turn_tick(game, dt);
    effects_tick(game, dt);

    /* animate tweens */
    for (int i = 0; i < fz_COUNTOF(slot_strength); ++i) {
        float target      = slot_strength_target[i];
        slot_strength[i] += (target - slot_strength[i]) * 0.05;
    }

    /*
    is player deciding to act with current action queue?
    */
    if (IsKeyPressed('P')) {
        if (game->player_act) { printf("Player is already acting. \n"); }
        else if (game->player.action_count <= 0) { printf("Empty Action Count.\n"); }
        else {
            game->player_act = 1;
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

void render_combat_phase_indicator(Game *game) {
    float x = render_tex_rect().width * 0.5;
    float y = render_tex_rect().height * 0.15;

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
        const char *text = TextFormat("Phase %d / %d", game->chain_index + 1, count);

        float x = render_tex_rect().width * 0.5;
        float y = render_tex_rect().height * 0.15 + TILE * 0.25;

        Vector2 pos = { x, y };
        pos = align_text_by(pos, text, TOP, CENTER, TILE * 0.75);

        DrawTextEx(font, text, pos, TILE * 0.75, 0, WHITE);
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
    player.x = render_tex.texture.width * 0.25 - (player.width / 2);
    player.y = render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP - (player.height / 2);

    render_healthbar(&game->player, player);
    DrawRectangleRec(player, WHITE);


    /*  ===========================================
     *  Render Enemy.
     * */

    render_combat_phase_indicator(game);

    int rendered_enemies = 0;
    Enemy_Chain *chain = &game->enemies[game->chain_index];

    for (int i = game->chain_index; i < chain->enemy_count; ++i) {
        if(chain->enemies[i].health <= 0) continue; // Ignore dead enemy (TODO: should have effect that enemy dies)

        Actor *enemy = &chain->enemies[i];
        Rectangle rect = {0};

        rect.width  = TILE;
        rect.height = TILE * 2;
        rect.x      = (render_tex.texture.width * 0.75) - (rect.width * 0.5) + (rendered_enemies * TILE * 4);
        rect.y      = (render_tex.texture.height * CHARACTER_Y_POSITION_FROM_TOP) - (rect.height * 0.5);

        /* Inactive enemies will be shadered */
        if (i > game->enemy_index) BeginShaderMode(noise_shader);
        DrawRectangleRec(rect, WHITE);
        if (i == game->enemy_index) render_healthbar(enemy, rect);
        if (i > game->enemy_index)  EndShaderMode();
        rendered_enemies++;
    }

    /* ======================================================
     * Render Queue.
     */
    {
        Rectangle layout = get_action_queue_layout();

        /* Queue number (as in ?/?) */
        {
            const char *format = TextFormat("%d / %d", game->player.action_count, ACTION_CAPACITY);
            float w = MeasureText(format, 18);
            DrawText(format, layout.x + (ACTION_CAPACITY * TILE * 0.5) - (w * 0.5), layout.y - 18 - 2, 18, WHITE);
        }

        for (int i = 0; i < ACTION_CAPACITY; ++i) {
            if (i >= game->player.action_count) BeginShaderMode(noise_shader);
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
                    draw_texture_sane(tex, texdest);
                }
            }

            int line_thickness = 1;
            if (i == game->player.action_index) {
                line_thickness = 2;
            }

            DrawRectangleLinesEx(r, line_thickness, WHITE);
            if (i >= game->player.action_count) EndShaderMode();
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
            BeginShaderMode(noise_shader);
                SetShaderValue(noise_shader, noise_shader_loc.strength_loc, &slot_strength[i], SHADER_UNIFORM_FLOAT);
                draw_texture_sane(tex, texdest);
                DrawRectangleLinesEx(dest, 1, WHITE);
            EndShaderMode();
        }
    }

    /* Reset shader options */
    SetShaderValue(noise_shader, noise_shader_loc.strength_loc, &BASE_SHADER_EFFECT_THRESHOLD, SHADER_UNIFORM_FLOAT);

}

void do_title_gui(Game *game) {
    Rectangle render_rect = render_tex_rect();

    BeginShaderMode(dither_shader);
    Texture2D t;
    if (get_texture(ASSET_BACKGROUND, &t)) {
        Rectangle dest = render_tex_rect();
        draw_texture_sane(t, dest);
    }
    EndShaderMode();

    Vector2 pos = { (float)render_rect.width * 0.5f, (float)render_rect.height * 0.65f };
    pos = align_text_by(pos, "Press Enter", MIDDLE, CENTER, TILE);

    DrawTextEx(font, "Press Enter", pos, TILE, 0, WHITE);

    if (game->next_core_state == TITLE_SCREEN) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsKeyPressed(KEY_ENTER)) {
            set_next_state(game, STAGE_SELECT, 2.0);
        }
    }
}

void do_stage_select_gui(Game *game) {
    Rectangle render_rect = render_tex_rect();

    BeginShaderMode(dither_shader);
    Texture2D t;
    if (get_texture(ASSET_BACKGROUND, &t)) {
        Rectangle dest = render_tex_rect();
        draw_texture_sane(t, dest);
    }
    EndShaderMode();

    Rect_Builder b = rect_builder(render_rect);
    rb_set_position_by_percent(&b, 0.5, 0.65);
    rb_set_size_v2(&b, button_size);
    rb_adjust_pivot(&b, MIDDLE, CENTER);

    Rectangle r1 = b.result;
    int stage_one = do_button_esque(hash("StageOne"), r1, 0, 0, INTERACT_CLICK_RIGHT);

    {
        Vector2 pivot = rb_get_coord_of(&b, TOP, CENTER);
        pivot = align_text_by(pivot, "Stage 1", TOP, CENTER, TILE);
        pivot.y += 12;

        DrawTextEx(font, "Stage 1", pivot, TILE, 0, WHITE);
    }

    pos.y += (TILE * 2.5);
    Rectangle r2 = rectv2(pos, button_size);
    int stage_two = do_button_esque(hash("StageTwo"), r2, 0, 0, INTERACT_CLICK_RIGHT);

    {
        Vector2 pivot = position_of_pivot(r2, TOP, CENTER);
        pivot = align_text_by(pivot, "Stage 2", TOP, CENTER, TILE);
        pivot.y += 12;

        DrawTextEx(font, "Stage 2", pivot, TILE, 0, WHITE);
    }

    if (game->next_core_state == STAGE_SELECT && (stage_one & INTERACT_CLICK_LEFT)) {
        load_stage_one(game);
        set_next_state(game, GAME_IN_PROGRESS, 2.0);
    }

    if (game->next_core_state == STAGE_SELECT && (stage_two & INTERACT_CLICK_LEFT)) {
        // load_stage_two(game);
        set_next_state(game, GAME_IN_PROGRESS, 2.0);
    }
}

int effectsort(const void *a, const void *b) {
    return ((Effect*)a)->asset_id - ((Effect*)b)->asset_id;
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

    VecSort(game->effects, effectsort);

    int current_asset_id = -1;
    Texture2D t;
    BeginShaderMode(dither_shader);
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
        DrawTextureTiled(t, src, dest, origin, 0, 1, BLACK);
    }

    EndShaderMode();
    EndTextureMode();
}
int main(void) {
    void *stack_mem = fz_heapalloc(1024 * fz_KB);

    fz_StackAlloc stack = {0};
    fz_stack_init(&stack, stack_mem, 1024 * fz_KB);
    fz_set_temp_allocator(fz_stack_allocator(&stack));

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
    game.enemies = VecCreate(Enemy_Chain, 6);
    game.effects = VecCreate(Effect, 256);
    game.max_transition = 1; /* division by zero prevention */

    game.turn_interval.max = 0.5;
    game.effect_interval.max = 0.10;

    /* Debug */
    load_stage_one(&game);
    game.core_state = game.next_core_state = GAME_IN_PROGRESS;

    load_tex_to_id(ASSET_BACKGROUND,         "assets/images/background.png");
    load_tex_to_id(ASSET_ACTION_ICON_SLASH,  "assets/images/spinning-sword.png");
    load_tex_to_id(ASSET_ACTION_ICON_EVADE,  "assets/images/wingfoot.png");
    load_tex_to_id(ASSET_ACTION_ICON_PARRY,  "assets/images/sword-break.png");
    load_tex_to_id(ASSET_ACTION_ICON_TACKLE, "assets/images/shield-bash.png");

    load_tex_to_id(ASSET_EFFECT_SPRITE_RECEIVED_SLASH, "assets/images/slash_effect_sprite.png");

    load_sound_to_id(ASSET_SOUND_START_GAME, "assets/sounds/start_game.wav");
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

    fz_heapfree(stack_mem);
    return 0;
}
