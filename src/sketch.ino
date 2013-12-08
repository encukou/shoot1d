#include <Adafruit_NeoPixel.h>

#define PIN 7
#define NUM_LEDS 60 * 4

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB | NEO_KHZ800);

enum object_type {
    OT_PLAYER,
    OT_DOOR,
    OT_ENEMY,
    OT_BONUS,
    OT_HEALTH,
    OT_AMMO,
    OT_BLOOD,

    OT_BOUNDARY,
};

#define PLAYER_START_POS 5
#define PLAYER_MAX_POS 20

#define COLOR_PLAYER   0xaaaaaa
#define COLOR_DOOR     0x00ffff
#define COLOR_ENEMY1   0x0000ff
#define COLOR_ENEMY2   0xc35a1a
#define COLOR_BONUS    0xffff00
#define COLOR_HEALTH   0x00ff00
#define COLOR_AMMO     0x00ffff

#define HEALTH_LED 1
#define AMMO_LED 2
#define SCORE_LED1 3

byte health = 255;
byte ammo = 100;
byte score = 0;
int level = 0;

int sfx_countdown = 0;
byte sfx_color[3];

int num_objs = 0;
long lastMillis;

struct object {
    object(byte type, uint16_t position, byte data, object* p):
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

    byte type;
    uint16_t position;
    byte data;
    object* prev;
    object* next;
};

object bound_start(OT_BOUNDARY, 0, 0, NULL);
object player(OT_PLAYER, PLAYER_START_POS, 0, &bound_start);
object bound_end(OT_BOUNDARY, NUM_LEDS+1, 1, &bound_start);

void sfx_flash(byte r, byte g, byte b) {
    sfx_countdown = 255;
    sfx_color[0] = r;
    sfx_color[1] = g;
    sfx_color[2] = b;
}

void setup() {
    Serial.begin(9600);
    strip.begin();
    strip.setBrightness(10);
    strip.show();
    lastMillis = millis();
    //player = new object(OT_PLAYER, PLAYER_START_POS, 0, NULL);
}

void loop() {
    bool report = false;
    long nowMillis = millis();
    int dt = nowMillis - lastMillis;

    if (nowMillis / 1000 != lastMillis / 1000) {
        health -= 1;
        ammo -= 1;
        score += 1;
        report = true;
    }

    int score_length = 0;
    for(int zscore = score; score_length<10; zscore /= 2, score_length++) {
        if (!zscore) break;
    }
    if (score_length != level) {
        // New level! Yellow flash
        sfx_flash(255, 255, 0);
        level = score_length;
    }

    memset(strip.getPixels(), 0, strip.numPixels() * 3);
    // health pixel
    strip.setPixelColor(0, strip.Color(255, 255, 255));
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
    for (i=0; i<score_length; i++) {
        strip.setPixelColor(i + SCORE_LED1,
                            (score & (1 << (score_length - i - 1)))
                                ? strip.Color(255, 255, 0)
                                : strip.Color(0, 0, 100));
    }
    i += SCORE_LED1;
    // border pixel
    strip.setPixelColor(i, 0);
    for (i++; i <= NUM_LEDS; i++) {
        if (sfx_countdown) {
            strip.setPixelColor(i, strip.Color(sfx_countdown * sfx_color[0] / 255,
                                               sfx_countdown * sfx_color[1] / 255,
                                               sfx_countdown * sfx_color[2] / 255));
        } else {
            strip.setPixelColor(i, 0);
        }
    }
    strip.show();

    if (report) {
        Serial.print('[');
        Serial.print(num_objs);
        Serial.print("] health=");
        Serial.print(health);
        Serial.print(" ammo=");
        Serial.print(ammo);
        Serial.print(" score=");
        Serial.print(score);
        Serial.print(" level=");
        Serial.print(level);
        Serial.println();
    }

    if (sfx_countdown) {
        sfx_countdown -= dt;
        if(sfx_countdown < 0) sfx_countdown = 0;
    }

    lastMillis = nowMillis;
}
