#include <Adafruit_NeoPixel.h>

#define PIN 7
#define NUM_LEDS 60 * 4

#define DEBUG 0

#define OUTPUT(x) Serial.print(" " #x "="); Serial.print(x);

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB | NEO_KHZ800);

enum object_type {
    OT_PLAYER   = '@',
    OT_DOOR     = '#',
    OT_ENEMY    = '!',
    OT_BONUS    = '$',
    OT_BULLET   = '-',
    OT_HEALTH   = '3',
    OT_AMMO     = '%',
    OT_BLOOD    = '.',

    OT_BOUNDARY = '|',
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
    object(object_type type, uint16_t position, byte data, object* p):
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

    int length() {
        switch (type) {
            case OT_PLAYER: return 3;
            case OT_DOOR: return 2;
            case OT_ENEMY: return 3;
            case OT_BONUS: return 1;
            case OT_BULLET: return 1;
            case OT_HEALTH: return 1;
            case OT_AMMO: return 1;
            case OT_BLOOD: return 3;
            default: return 1;
        }
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

    void move(int dist) {
        this->position += dist;
        if (dist > 0) {
            while (this->next && this->cmp(this->next) > 0) {
                OUTPUT(char(this->type));
                OUTPUT(char(this->next->type));
                Serial.println('<');
                print_objects();
                this->switch_with_next();
            }
        } else if (dist < 0) {
            while (this->prev && this->cmp(this->prev) < 0) {
                Serial.println('>');
                this->prev->switch_with_next();
            }
        }
    }

    uint32_t color(int pixel) {
        switch (type) {
            case OT_PLAYER: {
                if (pixel == 1) return strip.Color(ammo, ammo, 255);
                if (pixel == 2) return strip.Color(200, health, health);
                return strip.Color(200, 200, 200);
            }
            case OT_DOOR: return 0x00ffff;
            case OT_ENEMY: return (data % 1) ? 0x0000ff : 0xc35a1a;
            case OT_BONUS: return 0xffff00;
            case OT_BULLET: return 0xddaf11;
            case OT_HEALTH: return 0x00ff00;
            case OT_AMMO: return 0x00ffff;
            case OT_BLOOD: return 0x800020;
            default: return 0;
        }
    }

    object_type type;
    uint16_t position;
    byte data;
    object* prev;
    object* next;
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
        Serial.print(char(it->type));
        if (!it->next) break;
        it = it->next;
    }
    Serial.println();

    Serial.print("BACK ");
    while (true) {
        Serial.print(char(it->type));
        if (!it->prev) break;
        it = it->prev;
    }
    Serial.println();

    Serial.print("DEBUG ");
    while (true) {
        Serial.print(char(it->type));
        Serial.print(' ');
        Serial.print(it->position);
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
                if (player.position > 0) player.move(-1);
            } break;
            case 'd': case 'D': {
                player.move(1);
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
        if (millis() % 500 < 150) {
            strip.setPixelColor(HEALTH_LED, strip.Color(0, 0, 0));
        } else {
            strip.setPixelColor(HEALTH_LED, strip.Color(255 - health, health, 0));
        }
    }
    // ammo pixel
    if (millis() % 256 < ammo) {
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
            current_obj_pixel = current_obj->length();
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
