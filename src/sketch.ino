#include <Adafruit_NeoPixel.h>

#define PIN 7
#define NUM_LEDS 60 * 4
#define BRIGHTNESS 10 // you want to look at the LEDs without getting blinded

#define DEBUG 0

#define OUTPUT(x) Serial.print(" " #x "="); Serial.print(x);

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB | NEO_KHZ800);

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

#define DEFOT(name, symbol, length, prio, r, g, b, a, flags) \
    const object_type _struct ## name = {symbol, length, prio, rgba(r, g, b, a), flags}; \
    const object_type *name = & _struct ## name;

DEFOT(OT_PLAYER,   '@', 4, 200, 255, 255, 255,   0, OF_0);
DEFOT(OT_DOOR,     '#', 3, 220,   0, 255, 255,   0, OF_0);
DEFOT(OT_ENEMY,    '!', 4, 150, 255, 255, 255,   0, OF_0);
DEFOT(OT_BONUS,    '$', 2,  50, 255, 255,   0,   0, OF_PASSABLE);
DEFOT(OT_BULLET,   '-', 1, 220, 170, 150,  17, 100, OF_PASSABLE);
DEFOT(OT_HEALTH,   '+', 2,  70, 255,   0,   0,   0, OF_PASSABLE);
DEFOT(OT_AMMO,     '%', 2,  60,   0, 255,   0,   0, OF_PASSABLE);
DEFOT(OT_BLOOD,    '.', 4,  10,  30,   0,   8,   0, OF_PASSABLE);
DEFOT(OT_BOUNDARY, '|', 1,   1,   0,   0,   0,   0, OF_0);

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

#define DOOR_CLOSE 1
#define DOOR_OPEN 2

byte health = 200;
byte ammo = 100;
byte gun_cooldown = 0;
uint32_t score = 0;
int level = 3;

int sfx_countdown = 0;
byte sfx_color[3];

int num_objs = 0;
long lastMillis;

struct object {
    const object_type *type;
    uint16_t position;
    byte data;
    byte data2;
    object* prev;
    object* next;

    object(const object_type *type, uint16_t position, byte data, object* p):
        type(type),
        position(position),
        data(data),
        data2(0)
    {
        if (p) {
            while (p->position < position && p->next) p = p->next;
            while (p->position > position && p->prev) p = p->prev;
            next = p->next;
            prev = p;
            p->next->prev = this;
            p->next = this;
        }
        num_objs += 1;
    }

    void remove() {
        if (prev) prev->next = next;
        if (next) next->prev = prev;
        delete this;
        num_objs -= 1;
    }

    bool passable() {
        if (type == OT_DOOR && data == 255) return true;
        return this->type->flags & OF_PASSABLE;
    }

    uint32_t color(int pixel) {
        if (type == OT_PLAYER) {
            if (pixel == 1) return rgba(ammo, ammo, 255, 1);
            if (pixel == 2) return rgba(200, health, health, 2);
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


object bound_start(OT_BOUNDARY, 0, 0, NULL);
object player(OT_PLAYER, PLAYER_START_POS, 0, &bound_start);
object bound_end(OT_BOUNDARY, NUM_LEDS+1, 1, &bound_start);

void sfx_flash(byte r, byte g, byte b, byte strength=255) {
    sfx_countdown = strength;
    sfx_color[0] = r;
    sfx_color[1] = g;
    sfx_color[2] = b;
}

void print_objects() {
    object *it = &bound_start;
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

    Serial.print('[');
    Serial.print(num_objs);
    Serial.print("] ");
    while (true) {
        Serial.print(it->type->symbol);
        Serial.print(' ');
        Serial.print(it->position);
        Serial.print('+');
        Serial.print(it->length());
        Serial.print(' ');
        Serial.print(player.get_collision_flag(it), HEX);
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
}

void setup() {
    Serial.begin(9600);
    strip.begin();
    strip.setBrightness(BRIGHTNESS);
    strip.show();
    lastMillis = millis();
    print_objects();

    new object(OT_DOOR, PLAYER_START_POS+5, 0, &player);
    new object(OT_ENEMY, PLAYER_START_POS+12, 3, &bound_start);

    Serial.println("A - move left");
    Serial.println("D - move right");
    Serial.println("W - action (open/close door)");
    Serial.println("Enter, Space - shoot!");
}

void loop() {
    bool report = false;
    long nowMillis = millis();
    int dt = nowMillis - lastMillis;

    // Roughly every second (1024=2**10 ms)
    if (nowMillis >> 10 != lastMillis >> 10) {
        if (health > 200) {
            health -= 1;
            report = true;
        }
    }

    if (dt > gun_cooldown) {
        gun_cooldown = 0;
    } else {
        gun_cooldown -= dt;
    }

    // Player movement
    if (Serial.available()) {
        byte command = Serial.read();
        switch (command) {
            case 'a': case 'A': {
                bool moving = true;
                for (collision_iterator it(&player, CF_OVERLAP_BEHIND | CF_TOUCH_BEHIND); it; it.next()) {
                    if (!it.obj->passable()) {
                        moving = false;
                        break;
                    }
                }
                if (moving and player.position > 0) player.move(-1);
            } break;
            case 'd': case 'D': {
                bool moving = true;
                for (collision_iterator it(&player, CF_OVERLAP_FRONT | CF_TOUCH_FRONT); it; it.next()) {
                    if (!it.obj->passable()) {
                        moving = false;
                        break;
                    }
                }
                if (moving) player.move(1);
            } break;
            case 'w': case 'W': {
                for (collision_iterator it(&player, CF_TOUCH_FRONT | CF_TOUCH_BEHIND); it; it.next()) {
                    if (it.obj->type == OT_DOOR) {
                        if (it.obj->data == 255) {
                            it.obj->data2 = DOOR_CLOSE;
                        } else {
                            it.obj->data2 = DOOR_OPEN;
                        }
                    }
                }
            } break;
            case '\n': case ' ': {
                bool shooting = true;
                if (!gun_cooldown) {
                    for (collision_iterator it(&player, CF_OVERLAP_FRONT | CF_TOUCH_FRONT); it; it.next()) {
                        if (!it.obj->passable()) {
                            shooting = false;
                            break;
                        }
                    }
                    if (shooting) {
                        if (ammo) {
                            object *bullet = new object(OT_BULLET, player.end(), 0, &player);
                            bullet->data2 = 1;
                            gun_cooldown = 50;
                            ammo -= 1;
                            report = true;
                        } else {
                            sfx_flash(0, 100, 200);
                        }
                    }
                }
            } break;
            case '$': {
                score += 1;
                report = true;
            } break;
        }
        report = true;
    }

    for (collision_iterator it(&bound_start, CF_ALL); it; it.next()) {
        if (it.obj->type == OT_DOOR) {
            if (it.obj->data2 == DOOR_CLOSE) {
                if (it.obj->data <= dt) {
                    it.obj->data = 0;
                    it.obj->data2 = 0;
                } else {
                    it.obj->data -= dt;
                }
            } else if (it.obj->data2 == DOOR_OPEN) {
                if (it.obj->data + int(dt) >= 255) {
                    it.obj->data = 255;
                    it.obj->data2 = 0;
                } else {
                    it.obj->data += dt;
                }
            }
        } else if (it.obj->type == OT_BULLET) {
            int newdata = it.obj->data + dt;
            while (it.obj && newdata > 20) {
                newdata -= 20;
                bool have_space = true;
                int flag = (it.obj->data2 > 0) ? CF_TOUCH_FRONT : CF_TOUCH_BEHIND;
                for (collision_iterator et(it.obj, flag); et; et.next()) {
                    if (!et.obj->passable() && et.obj != it.obj) {
                        have_space = false;
                    }
                    if (et.obj->type == OT_ENEMY) {
                        et.obj->data -= 1;
                        if (et.obj->data == 0) {
                            et.obj->type = OT_BLOOD;
                        }
                    }
                }
                if (have_space) {
                    it.obj->move(it.obj->data2);
                } else {
                    it.obj->remove();
                    it.obj = NULL;
                }
            }
            if (it.obj) {
                it.obj->data = newdata;
            }
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
    // keep a priority-sorted list of objects we're currently drawing
    object *current_obj = &bound_start;
    obj_draw_list list_base;  // sentinel
    obj_draw_list *list = &list_base;
    for (int px=0; i <= NUM_LEDS; px++, i++) {
        while (current_obj && current_obj->next && current_obj->next->position <= px) {
            current_obj = current_obj->next;
            // add obj to list
            obj_draw_list *newitem = new obj_draw_list(current_obj);
            obj_draw_list **cur = &list;
            while (*cur && (*cur)->next &&
                    (*cur)->obj->type->draw_priority > newitem->obj->type->draw_priority) {
                cur = &((*cur)->next);
            }
            newitem->next = *cur;
            *cur = newitem;
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
        Serial.println();
    }

    if (sfx_countdown) {
        sfx_countdown -= dt;
        if(sfx_countdown < 0) sfx_countdown = 0;
    }

    lastMillis = nowMillis;
}
