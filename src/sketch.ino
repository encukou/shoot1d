#include <Adafruit_NeoPixel.h>

#define LED_PIN 7
#define NUM_LEDS 60 * 4
#define BRIGHTNESS 10 // you want to look at the LEDs without getting blinded
#define UNCONNECTED_ANALOG_PIN 0  // for randomness

#define DEBUG 0

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB | NEO_KHZ800);

enum object_flag {
    OF_0 = 0,
    OF_PASSABLE  = 0x01,
};

struct object_type {
    char symbol;
    byte default_length;
    byte draw_priority;
    uint32_t default_color;
    object_flag flags;

    bool operator == (const object_type &b) const {
        return this == &b;
    };
};

static uint32_t rgba(byte r, byte g, byte b, byte a=0) {
    return uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
}

static byte getR(uint32_t color) { return color >> 16; }
static byte getG(uint32_t color) { return color >> 8; }
static byte getB(uint32_t color) { return color; }
static byte getA(uint32_t color) { return color >> 24; }

uint32_t mix(uint32_t color1, uint32_t color2) {
    // color1 - on top
    // color2 - on bottom
    // alpha: 0=opaque; 255=transparent
    uint32_t c1a = getA(color1);
    if (c1a == 255) return color2;
    if (c1a == 0 || getA(color2) == 255) return color1;
    return rgba(
        (getR(color1) * (255 - c1a) + getR(color2) * c1a) >> 8,
        (getG(color1) * (255 - c1a) + getG(color2) * c1a) >> 8,
        (getB(color1) * (255 - c1a) + getB(color2) * c1a) >> 8,
        c1a + getA(color2) * (255 - c1a) >> 8);
}

enum object_type_name {
    OT_PLAYER,
    OT_DOOR,
    OT_ENEMY,
    OT_BONUS,
    OT_BULLET,
    OT_HEALTH,
    OT_AMMO,
    OT_BLOOD,

    OT_LAST,
};

object_type obj_types[] = {
    {'@', 4, 200, rgba(255, 255, 255,   0), OF_0},           // PLAYER
    {'#', 3, 220, rgba(  0, 255, 255,   0), OF_0},           // DOOR
    {'!', 4, 150, rgba(255, 255, 255,   0), OF_0},           // ENEMY
    {'$', 2,  50, rgba(255, 255,   0,   0), OF_PASSABLE},    // BONUS
    {'-', 1, 220, rgba(170, 150,  17, 100), OF_PASSABLE},    // BULLET
    {'+', 2,  70, rgba(255,   0,   0,   0), OF_PASSABLE},    // HEALTH
    {'%', 2,  60, rgba(  0, 255,   0,   0), OF_PASSABLE},    // AMMO
    {'.', 4,  10, rgba( 30,   0,   8,   0), OF_PASSABLE},    // BLOOD

    {'?', 1,   0, rgba(100, 100, 100,   0), OF_0},           // LAST
};

enum collision_flag {
                    // Legend: s: self; o: other; X: both
    CF_BEHIND         = 0x001,  // -ooo----sss--------
    CF_TOUCH_BEHIND   = 0x002,  // -----ooosss--------
    CF_OVERLAP_BEHIND = 0x004,  // ------ooXss--------
    CF_CONTAINED      = 0x008,  // --------sXs--------
    CF_EXACT          = 0x010,  // --------XXX--------
    CF_OVERLAP        = 0x020,  // -------oXXXo-------
    CF_OVERLAP_FRONT  = 0x040,  // --------ssXoo------
    CF_TOUCH_FRONT    = 0x080,  // --------sssooo-----
    CF_FRONT          = 0x100,  // --------sss----ooo-

    CF_COLLIDING      = 0x0fe,
    CF_ALL            = 0xfff,
};

#define PLAYER_START_POS 5
#define PLAYER_MAX_POS 20

#define HEALTH_LED 0
#define AMMO_LED 1
#define SCORE_LED1 2

#define DOOR_CLOSE false
#define DOOR_OPEN true

#define PLAYERS_BULLET true
#define ENEMY_BULLET false

// Arduino tries to pre-declare all functions.
// This won't work when they use types defined here.
// We can use a nasty trick to supress the magic.
#define LPAREN (

byte health = 200;
byte ammo = 100;
byte gun_cooldown = 0;
byte enemy_gun_cooldown = 0;
uint32_t score = 0;
int level = 3;

byte sfx_countdown = 0;
byte sfx_color[3];

int num_objs = 0;
long lastMillis;

#if NUM_LEDS < 250
byte typedef position_type;
#else
uint16_t typedef position_type;
#endif

struct object;

object *freelist;
object *player = NULL;

struct objecttype_ptr {
    static const byte ptr_mask = 0x7f;
    static const byte flag_mask = 0x80;
    byte data;
    objecttype_ptr(object_type_name n, bool flag): data(n | (flag ? flag_mask : 0)) {};
    object_type &operator* () { return obj_types[data & ptr_mask]; };
    object_type *operator->() { return &obj_types[data & ptr_mask]; };
    bool operator==(const object_type_name &n) { return (data & ptr_mask) == n; };
    bool operator!=(const object_type_name &n) { return (data & ptr_mask) != n; };
    void set(const object_type_name n) { data = n; };
    bool get_flag(){ return bool(data & flag_mask); };
    void set_flag(bool flag){ data = (data & ptr_mask) | (flag ? flag_mask : 0); };
};

struct object {
    objecttype_ptr type;
    position_type position;
    byte data;
    object* prev;
    object* next;

    object(): type(OT_LAST, 0), position(0), data(0), prev(0), next(0) {}

    static object *create(object_type_name type, position_type position, byte data, bool flag, object* p) {
        if (freelist) {
            object *newobj = freelist;
            freelist = freelist->next;

            newobj->type.set(type);
            newobj->position = position;
            newobj->data = data;
            newobj->type.set_flag(flag);
            if (p) {
                while (p->position < position && p->next) p = p->next;
                while (p->position > position && p->prev) p = p->prev;
                newobj->next = p->next;
                newobj->prev = p;
                if (p->next) p->next->prev = newobj;
                p->next = newobj;
            } else {
                newobj->next = newobj->prev = NULL;
            }

            return newobj;
        } else {
            return NULL;
        }
    }

    void remove() {
        if (prev) prev->next = next;
        if (next) next->prev = prev;
        next = freelist;
        prev = NULL;
        freelist = this;
    }

    bool flag() {
        return type.get_flag();
    }

    void set_flag(bool flag) {
        type.set_flag(flag);
    }

    bool passable() {
        if (type == OT_DOOR && data == 255) return true;
        return this->type->flags & OF_PASSABLE;
    }

    uint32_t color(int pixel) {
        if (type == OT_PLAYER) {
            if (pixel == 2) return rgba(200, health, health, 2);
            if (!health) return rgba(0, 0, 0);
            if (pixel == 1) return rgba(ammo, ammo, 255, 1);
        } else if (type == OT_DOOR && data) {
            if (pixel == 1) return rgba(0, 255, 255, data);
            return rgba(0, 255 - data, 255 - data*2/3, data/4);
        }
        return this->type->default_color;
    }

    int length() {
        return this->type->default_length;
    }

    uint16_t end() {
        return position + length();
    }

    long cmp(object *other) {
        if (this->position > other->position) return 1;
        if (this->position < other->position) return -1;
        return long(this->length()) - long(other->length());
    }

    void switch_with_next() {
        object *o1 = this->prev;
        object *o2 = this->next;
        object *o3 = this;
        object *o4 = o2->next;

        if(o1) o1->next = o2;
        o2->prev = o1;
        o2->next = o3;
        o3->prev = o2;
        o3->next = o4;
        if(o4) o4->prev = o3;
    }

    collision_flag get_collision_flag(object *other) {
        uint16_t end = other->end();
        uint16_t pos = other->position;
        uint16_t self_end = this->end();
        if (end < this->position) return CF_BEHIND;
        if (end == this->position) return CF_TOUCH_BEHIND;
        if (pos < this->position) {
            if (end < self_end) return CF_OVERLAP_BEHIND;
            return CF_OVERLAP;
        }
        if (pos == this->position) {
            if (end < self_end) return CF_CONTAINED;
            if (end == self_end) return CF_EXACT;
            return CF_OVERLAP;
        }
        if (pos < self_end) {
            if (end <= self_end) return CF_CONTAINED;
            return CF_OVERLAP_FRONT;
        }
        if (pos == self_end) return CF_TOUCH_FRONT;
        return CF_FRONT;
    }

    void move(int dist) {
        this->position += dist;
        if (dist > 0) {
            while (this->next && this->cmp(this->next) > 0) {
                this->switch_with_next();
            }
        } else if (dist < 0) {
            while (this->prev && this->cmp(this->prev) < 0) {
                this->prev->switch_with_next();
            }
        }
    }
};

object objects[10];

struct obj_draw_list {
    object *obj;
    obj_draw_list *next;
    int pixel;

    obj_draw_list(object *obj):
        obj(obj),
        next(NULL),
        pixel(obj->length() - 1) {}

    obj_draw_list():
        obj(NULL),
        next(NULL),
        pixel(NUM_LEDS + 2) {}
};


struct collision_iterator {
    collision_iterator(object* o, int flags=CF_COLLIDING):
        self(o), next_obj(o), iter_flags(flags), flag(0)
    {
        if (iter_flags & CF_BEHIND) {
            while (next_obj->prev) next_obj = next_obj->prev;
        } else if (iter_flags & (CF_TOUCH_BEHIND | CF_OVERLAP_BEHIND | CF_OVERLAP)) {
            while (next_obj->prev && next_obj->prev->end() <= next_obj->position) {
                next_obj = next_obj->prev;
            }
        }
        next();
    }

    object *obj;
    int flag;

    object *next() {
        obj = NULL;
        while (next_obj && !obj) {
            flag = self->get_collision_flag(next_obj) & iter_flags;
            if (flag & this->iter_flags) obj = next_obj;
            next_obj = next_obj->next;
            if (!(iter_flags & CF_FRONT) && next_obj->position > self->end() + 1) {
                next_obj = NULL;
            }
        }
        return obj;
    }

    operator bool() const { return obj != NULL; }

private:
    object *self;
    object *next_obj;
    int iter_flags;
};


void sfx_flash(byte r, byte g, byte b, byte strength=255) {
    sfx_countdown = strength;
    sfx_color[0] = r;
    sfx_color[1] = g;
    sfx_color[2] = b;
}

inline void cool_down(int dt, byte* value) {
    if (dt > *value) {
        *value = 0;
    } else {
        *value -= dt;
    }
}

void print_objects() {
    object *it = player;
    while (it->prev) it = it->prev;
    Serial.print("FORW ");
    while (true) {
        Serial.print(it->type->symbol);
        if (!it->next) break;
        it = it->next;
    }
    Serial.println();

    Serial.print("BACK ");
    while (true) {
        Serial.print(it->type->symbol);
        if (!it->prev) break;
        it = it->prev;
    }
    Serial.println();

    while (true) {
        Serial.print(it->type->symbol);
        Serial.print(' ');
        Serial.print(it->position);
        Serial.print('+');
        Serial.print(it->length());
        Serial.print(' ');
        Serial.print(player->get_collision_flag(it), HEX);
        if (!it->next) break;
        if (it->cmp(it->next) > 0) {
            Serial.print(" > ");
        }else if (it->cmp(it->next) < 0) {
            Serial.print(" < ");
        } else {
            Serial.print(" = ");
        }
        it = it->next;
    }
    Serial.println();

    randomSeed(analogRead(UNCONNECTED_ANALOG_PIN));
}

void setup() {
    Serial.begin(9600);
    strip.begin();
    strip.setBrightness(BRIGHTNESS);
    strip.show();
    lastMillis = millis();

    memset(objects, 0, sizeof(objects));
    for (int i=1; i < sizeof(objects)/sizeof(*objects); i++) {
        objects[i - 1].next = &objects[i];
    }
    freelist = objects;

    Serial.println(sizeof(object));

    player = object::create(OT_PLAYER, PLAYER_START_POS, 0, 0, NULL);

    object::create(OT_DOOR, PLAYER_START_POS+5, 0, 0, player);
    object::create(OT_ENEMY, PLAYER_START_POS+12, 3, 0, player);

    Serial.println("A - move left");
    Serial.println("D - move right");
    Serial.println("W - action (open/close door)");
    Serial.println("Enter, Space - shoot!");
}

void loop() {
    long nowMillis = millis();
    int dt = nowMillis - lastMillis;

    bool report;

    if (health) {
        report |= update(dt, nowMillis);
        report |= handle_input();
    }

    draw(nowMillis, report);

    lastMillis = nowMillis;
}

bool handle_input() {
    // Player movement
    if (Serial.available()) {
        byte command = Serial.read();
        switch (command) {
            case 'a': case 'A': {
                bool moving = true;
                for (collision_iterator it(player, CF_OVERLAP_BEHIND | CF_TOUCH_BEHIND); it; it.next()) {
                    if (!it.obj->passable()) {
                        moving = false;
                        break;
                    }
                }
                if (moving and player->position > 0) player->move(-1);
            } break;
            case 'd': case 'D': {
                bool moving = true;
                for (collision_iterator it(player, CF_OVERLAP_FRONT | CF_TOUCH_FRONT); it; it.next()) {
                    if (!it.obj->passable()) {
                        moving = false;
                        break;
                    }
                }
                if (moving) player->move(1);
            } break;
            case 'w': case 'W': {
                for (collision_iterator it(player, CF_TOUCH_FRONT | CF_TOUCH_BEHIND); it; it.next()) {
                    if (it.obj->type == OT_DOOR) {
                        if (it.obj->data == 255) {
                            it.obj->set_flag(DOOR_CLOSE);
                        } else {
                            it.obj->set_flag(DOOR_OPEN);
                        }
                    }
                }
            } break;
            case '\n': case ' ': {
                if (!gun_cooldown) {
                    if (ammo) {
                        object::create(OT_BULLET, player->end(), 0, PLAYERS_BULLET, player);
                        gun_cooldown = 50;
                        ammo -= 1;
                    } else {
                        sfx_flash(0, 100, 200);
                    }
                }
            } break;
            case '$': {
                score += 1;
            } break;
        }
        return true;
    }
    return false;
}

bool update(int dt, long nowMillis) {
    bool report;
    // Roughly every second (1024=2**10 ms)
    if (nowMillis >> 10 != lastMillis >> 10) {
        if (health > 200) {
            health -= 1;
            report = true;
        }
    }

    cool_down(dt, &gun_cooldown);
    cool_down(dt/3, &enemy_gun_cooldown);
    cool_down(dt, &sfx_countdown);

    bool finding_active_enemy = false;
    for (collision_iterator it(player, CF_ALL); it; it.next()) {
        if (it.obj->type == OT_PLAYER) {
            finding_active_enemy = true;
        } else if (it.obj->type == OT_ENEMY) {
            if (finding_active_enemy && !enemy_gun_cooldown) {
                if (random(255) < 200) {
                    object::create(OT_BULLET, it.obj->position - 1, 0, ENEMY_BULLET, it.obj);
                }
                enemy_gun_cooldown = random(100, 255);
            }
        } else if (it.obj->type == OT_DOOR) {
            if (it.obj->flag() == DOOR_CLOSE) {
                if (it.obj->data <= dt) {
                    it.obj->data = 0;
                } else {
                    it.obj->data -= dt;
                }
            } else if (it.obj->flag() == DOOR_OPEN) {
                if (it.obj->data + int(dt) >= 255) {
                    it.obj->data = 255;
                } else {
                    it.obj->data += dt;
                }
            }
        } else if (it.obj->type == OT_BULLET) {
            int newdata = it.obj->data + dt;
            while (it.obj && newdata > 20) {
                newdata -= 20;
                bool have_space = true;
                int flag = (it.obj->flag() == PLAYERS_BULLET) ? CF_TOUCH_FRONT : CF_TOUCH_BEHIND;
                for (collision_iterator et(it.obj, flag); et; et.next()) {
                    if (et.obj->type == OT_ENEMY) {
                        et.obj->data -= 1;
                        if (et.obj->data == 0) {
                            et.obj->type.set(OT_BLOOD);
                        }
                    } else if (et.obj->type == OT_PLAYER) {
                        byte damage = random(5, 15);
                        if (health > damage) {
                            health -= damage;
                        } else {
                            health = 0;
                        }
                        sfx_flash(255, 0, 0, 255);
                        report = true;
                    }
                    if (!et.obj->passable() && et.obj != it.obj) {
                        have_space = false;
                        break;
                    }
                }
                if (have_space) {
                    it.obj->move((it.obj->flag() == PLAYERS_BULLET) ? 1 : -1);
                } else {
                    it.obj->remove();
                    it.obj = NULL;
                }
            }
            if (it.obj && (it.obj->position < 0 || it.obj->position > NUM_LEDS)) {
                it.obj->remove();
                it.obj = NULL;
            }
            if (it.obj) {
                it.obj->data = newdata;
            }
        }
        if (finding_active_enemy && !it.obj->passable() && it.obj->type != OT_PLAYER) {
            finding_active_enemy = false;
        }
    }

    // level = log₂(score) (displayed minus 3; first 3 levels are 0)
    int score_length = 0;
    for(int zscore = score; score_length<20; zscore /= 2, score_length++) {
        if (!zscore) break;
    }
    if (score_length > level) {
        // New level! Yellow flash
        sfx_flash(255, 255, 0);
        level = score_length;
    }

    return report;
}

void draw(long nowMillis, bool report) {
    memset(strip.getPixels(), 0, strip.numPixels() * 3);
    // health pixel
    if (health > 10) {
        strip.setPixelColor(HEALTH_LED, rgba(255 - health, health, 0));
    } else {
        if (nowMillis % 500 < 150) {
            strip.setPixelColor(HEALTH_LED, rgba(0, 0, 0));
        } else {
            strip.setPixelColor(HEALTH_LED, rgba(255 - health, health, 0));
        }
    }
    // ammo pixel
    if (nowMillis % 256 < ammo) {
        strip.setPixelColor(AMMO_LED, rgba(ammo, ammo, ammo));
    } else {
        if (ammo > 20) {
            strip.setPixelColor(AMMO_LED, rgba(0, 0, 0));
        } else {
            strip.setPixelColor(AMMO_LED, rgba(255, 0, 0));
        }
    }
    // score pixels
    int i = 0;
    for (i=0; i<level; i++) {
        strip.setPixelColor(i + SCORE_LED1,
                            (score & (1 << (level - i - 1)))
                                ? rgba(255, 255, 0)
                                : rgba(0, 0, 100));
    }
    i += SCORE_LED1;
    // status border pixel
    strip.setPixelColor(i, 0);

    // playing field
    i++;
    uint32_t backgroundColor = sfx_countdown
                               ? strip.Color(sfx_countdown * sfx_color[0] >> 8,
                                             sfx_countdown * sfx_color[1] >> 8,
                                             sfx_countdown * sfx_color[2] >> 8)
                               : 0;
    if (!health) backgroundColor |= rgba(255, 0, 0);
    // keep a priority-sorted list of objects we're currently drawing
    object *current_obj = player;
    while (current_obj->prev) current_obj = current_obj->prev;
    obj_draw_list list_base;  // sentinel
    obj_draw_list *list = &list_base;
    for (int px=0; i <= NUM_LEDS; px++, i++) {
        while (current_obj && current_obj->position <= px) {
            // add obj to list
            obj_draw_list *newitem = new obj_draw_list(current_obj);
            obj_draw_list **cur = &list;
            while (*cur && (*cur)->next &&
                    (*cur)->obj->type->draw_priority > newitem->obj->type->draw_priority) {
                cur = &((*cur)->next);
            }
            newitem->next = *cur;
            *cur = newitem;
            current_obj = current_obj->next;
        }

        uint32_t color = rgba(0, 0, 0, 255);
        for (obj_draw_list *cur = list; cur && getA(color); cur=cur->next) {
            if (cur->obj) {
                // draw the thing on top of list
                color = mix(color, cur->obj->color(list->pixel));
            } else {
                // draw background
                color = mix(color, backgroundColor);
            }
        }
        strip.setPixelColor(i, color);

        // remove drawn things from list
        obj_draw_list **cur = &list;
        while (*cur) {
            (*cur)->pixel -= 1;
            if ((*cur)->pixel < 0) {
                obj_draw_list *deleting = *cur;
                *cur = deleting->next;
                delete deleting;
            } else {
                cur = &((*cur)->next);
            }
        }
    }
    // destroy list at end
    while(list) {
        obj_draw_list *next = list->next;
        if (list != &list_base) delete list;
        list = next;
    }
    strip.show();

    if (report || nowMillis >> 8 != lastMillis >> 8) {
        if (report && DEBUG) print_objects();
        Serial.print("♥");
        Serial.print(health);
        Serial.print(" a");
        Serial.print(ammo);
        Serial.print(" $");
        Serial.print(score);
        Serial.print(" lv");
        Serial.print(level - 3);
        for (object *f = freelist; f; f = f->next) {
            Serial.print('.');
        }
        Serial.println();
    }
}
