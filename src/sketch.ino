#include <Adafruit_NeoPixel.h>

#define PIN 7
#define NUM_LEDS 60 * 4

#define DEBUG 1

#define OUTPUT(x) Serial.print(" " #x "="); Serial.print(x);

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB | NEO_KHZ800);

enum object_flag {
    OF_0 = 0,
    OF_PASSABLE  = 0x01,
};

struct object_type {
    char symbol;
    byte default_length;
    byte priority;
    uint32_t default_color;
    object_flag flags;

    bool operator == (const object_type &b) const {
        return this == &b;
    };
};

const object_type OT_PLAYER   = {'@', 4, 200, 0xffffff, OF_0};
const object_type OT_DOOR     = {'#', 3, 100, 0x00ffff, OF_0};
const object_type OT_ENEMY    = {'!', 4, 150, 0xffffff, OF_0};
const object_type OT_BONUS    = {'$', 2,  50, 0xffff00, OF_PASSABLE};
const object_type OT_BULLET   = {'-', 2, 120, 0xddaf11, OF_PASSABLE};
const object_type OT_HEALTH   = {'+', 2,  70, 0xff0000, OF_PASSABLE};
const object_type OT_AMMO     = {'%', 2,  60, 0x00ff00, OF_PASSABLE};
const object_type OT_BLOOD    = {'.', 4,  10, 0x800020, OF_PASSABLE};
const object_type OT_BOUNDARY = {'|', 1,   0, 0x000000, OF_0};

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

byte health = 200;
byte ammo = 100;
uint32_t score = 0;
int level = 3;

int sfx_countdown = 0;
byte sfx_color[3];

int num_objs = 0;
long lastMillis;

struct object {
    object(const object_type &type, uint16_t position, byte data, object* p):
        type(type),
        position(position),
        data(data)
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
        if (next) next->prev = next;
        delete this;
        num_objs -= 1;
    }

    bool passable() {
        return this->type.flags & OF_PASSABLE;
    }

    uint32_t color(int pixel) {
        if (type == OT_PLAYER) {
            if (pixel == 1) return strip.Color(ammo, ammo, 255);
            if (pixel == 2) return strip.Color(200, health, health);
            return strip.Color(200, 200, 200);
        }
        return this->type.default_color;
    }

    int length() {
        return this->type.default_length;
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
                print_objects();
                this->switch_with_next();
            }
        } else if (dist < 0) {
            while (this->prev && this->cmp(this->prev) < 0) {
                this->prev->switch_with_next();
            }
        }
    }

    const object_type &type;
    uint16_t position;
    byte data;
    object* prev;
    object* next;
};


struct collision_iterator {
    collision_iterator(object* o, int flags=CF_COLLIDING):
        self(o), next_obj(o), iter_flags(flags), flag(0)
    {
        if (iter_flags & CF_BEHIND) {
            while (next_obj->prev) next_obj = next_obj->prev;
        } else if (iter_flags & (CF_TOUCH_BEHIND | CF_OVERLAP_BEHIND | CF_OVERLAP)) {
            while (next_obj->prev && next_obj->prev->end() < next_obj->position) {
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
        Serial.print(it->type.symbol);
        if (!it->next) break;
        it = it->next;
    }
    Serial.println();

    Serial.print("BACK ");
    while (true) {
        Serial.print(it->type.symbol);
        if (!it->prev) break;
        it = it->prev;
    }
    Serial.println();

    Serial.print("DEBUG ");
    while (true) {
        Serial.print(it->type.symbol);
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
    strip.setBrightness(10);
    strip.show();
    lastMillis = millis();
    //player = new object(OT_PLAYER, PLAYER_START_POS, 0, NULL);
    print_objects();

    new object(OT_DOOR, PLAYER_START_POS+5, 0, &player);
    new object(OT_ENEMY, PLAYER_START_POS+10, 0, &bound_start);

    Serial.println("A - move left");
    Serial.println("D - move right");
}

void loop() {
    bool report = false;
    long nowMillis = millis();
    int dt = nowMillis - lastMillis;

    // Roughly every second
    if (nowMillis / 1024 != lastMillis / 1024) {
        if (health > 200) {
            health -= 1;
            report = true;
        }
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
            case '$': {
                score += 1;
                report = true;
            } break;
        }
        report = true;
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
        strip.setPixelColor(HEALTH_LED, strip.Color(255 - health, health, 0));
    } else {
        if (nowMillis % 500 < 150) {
            strip.setPixelColor(HEALTH_LED, strip.Color(0, 0, 0));
        } else {
            strip.setPixelColor(HEALTH_LED, strip.Color(255 - health, health, 0));
        }
    }
    // ammo pixel
    if (nowMillis % 256 < ammo) {
        strip.setPixelColor(AMMO_LED, strip.Color(ammo, ammo, ammo));
    } else {
        if (ammo > 20) {
            strip.setPixelColor(AMMO_LED, strip.Color(0, 0, 0));
        } else {
            strip.setPixelColor(AMMO_LED, strip.Color(255, 0, 0));
        }
    }
    // score pixels
    int i = 0;
    for (i=0; i<level; i++) {
        strip.setPixelColor(i + SCORE_LED1,
                            (score & (1 << (level - i - 1)))
                                ? strip.Color(255, 255, 0)
                                : strip.Color(0, 0, 100));
    }
    i += SCORE_LED1;
    // status border pixel
    strip.setPixelColor(i, 0);
    // playing field
    i++;
    object* current_obj = &bound_start;
    int current_obj_pixel = -1;
    for (int px=0; i <= NUM_LEDS; px++, i++) {
        while (current_obj->next && current_obj->next->position <= px) {
            current_obj = current_obj->next;
            current_obj_pixel = current_obj->length() - 1;
            if (current_obj == &bound_end) {
                Serial.println(" ERROR! Went past boundary");
                break;
            }
        }
        if (current_obj_pixel > -1) {
            strip.setPixelColor(i, current_obj->color(current_obj_pixel));
            current_obj_pixel -= 1;
        } else if (sfx_countdown) {
            // background with SFX happening
            strip.setPixelColor(i, strip.Color(sfx_countdown * sfx_color[0] / 255,
                                               sfx_countdown * sfx_color[1] / 255,
                                               sfx_countdown * sfx_color[2] / 255));
        } else {
            // background
            strip.setPixelColor(i, 0);
        }
    }
    strip.show();

    if (report) {
        if (DEBUG) {
            print_objects();
            Serial.print('[');
            Serial.print(num_objs);
            Serial.print("] ");
        }
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
