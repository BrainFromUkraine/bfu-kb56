/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * Host tests for the hardware-independent firmware logic.
 * Build & run:  make -C tests
 * The expected layout below is written independently of src/app/keymap.c on purpose.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/app/app.h"
#include "../src/app/utf8.h"

void app_lock(void) {}
void app_unlock(void) {}

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static app_t A;
static uint32_t now;

static void run(uint64_t raw, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        app_tick(&A, raw, now++);
    }
}

#define K(r, c) (1ull << UAKB_KEYCODE(r, c))

/* press keys (bitmask) for 20 ms then release for 20 ms */
static void tap(uint64_t hold)
{
    run(hold, 20);
    run(0, 20);
}

/* Read one whole event through the I2C emulation, like a real master:
 * write pointer, then read 8 bytes; the slave preloads one extra byte that is never sent. */
static int read_event(uint8_t ev[8])
{
    app_i2c_write_begin(&A);
    app_i2c_write_byte(&A, UAKB_REG_FIFO);
    app_i2c_end(&A, false, 0);
    app_i2c_read_begin(&A);
    for (int i = 0; i < 8; i++) ev[i] = app_i2c_read_byte(&A);
    (void)app_i2c_read_byte(&A);              /* preloaded, not transmitted */
    app_i2c_end(&A, true, 1);
    return ev[0] != UAKB_EV_NONE;
}

static uint8_t read_reg(uint8_t reg)
{
    app_i2c_write_begin(&A);
    app_i2c_write_byte(&A, reg);
    app_i2c_end(&A, false, 0);
    app_i2c_read_begin(&A);
    uint8_t v = app_i2c_read_byte(&A);
    (void)app_i2c_read_byte(&A);
    app_i2c_end(&A, true, 1);
    return v;
}

static void write_reg(uint8_t reg, uint8_t v)
{
    app_i2c_write_begin(&A);
    app_i2c_write_byte(&A, reg);
    app_i2c_write_byte(&A, v);
    app_i2c_end(&A, false, 0);
}

static void drain(void)
{
    uint8_t ev[8];
    while (read_event(ev)) {}
}

static void reset(void)
{
    app_init(&A);
    now = 1000;
}

/* --------------------------------------------------------------- expected layout */
/* "" = no text; then the special code is checked. NULL = position not populated. */
typedef struct { const char *txt; uint8_t sp; } exp_t;
#define T(s)  { s, 0 }
#define S(c)  { "", c }
#define N     { NULL, 0 }
#define MOD   { "MOD", 0 }

static const char *UA_TXT[5][12] = {
    { "ʼ","1","2","3","4","5","6","7","8","9","0","\b" },
    { "й","ц","у","к","е","н","г","ш","щ","з","х","ї" },
    { "ф","і","в","а","п","р","о","л","д","ж","є","\n" },
    { NULL,"я","ч","с","м","и","т","ь","б","ю",".",NULL },
    { NULL,NULL,NULL,NULL,NULL," ",NULL,NULL,NULL,NULL,NULL,NULL },
};
static const char *UA_S_TXT[5][12] = {
    { "₴","!","\"","№",";","%",":","?","*","(",")","\b" },
    { "Й","Ц","У","К","Е","Н","Г","Ш","Щ","З","Х","Ї" },
    { "Ф","І","В","А","П","Р","О","Л","Д","Ж","Є","\n" },
    { NULL,"Я","Ч","С","М","И","Т","Ь","Б","Ю",",",NULL },
    { NULL,NULL,NULL,NULL,NULL," ",NULL,NULL,NULL,NULL,NULL,NULL },
};
static const char *EN_TXT[5][12] = {
    { "`","1","2","3","4","5","6","7","8","9","0","\b" },
    { "q","w","e","r","t","y","u","i","o","p","[","]" },
    { "a","s","d","f","g","h","j","k","l",";","'","\n" },
    { NULL,"z","x","c","v","b","n","m",",",".","/",NULL },
    { NULL,NULL,NULL,NULL,NULL," ",NULL,NULL,NULL,NULL,NULL,NULL },
};
static const char *EN_S_TXT[5][12] = {
    { "~","!","@","#","$","%","^","&","*","(",")","\b" },
    { "Q","W","E","R","T","Y","U","I","O","P","{","}" },
    { "A","S","D","F","G","H","J","K","L",":","\"","\n" },
    { NULL,"Z","X","C","V","B","N","M","<",">","?",NULL },
    { NULL,NULL,NULL,NULL,NULL," ",NULL,NULL,NULL,NULL,NULL,NULL },
};
static const char *FN_TXT[5][12] = {
    { "\x1b",NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,"\x7f" },
    { "\t","-","_","=","+","~","ґ","[","]","{","}","\\" },
    { "!","@","#","$","%","^","&","*","(",")","\"",NULL },
    { NULL,"`","|","/","?",":",";","'","<",">",",",NULL },
    { NULL,NULL,NULL,NULL,NULL," ",NULL,NULL,NULL,NULL,NULL,NULL },
};

static void check_text(const char *layer, int r, int c, uint64_t mods_keys, const char *want)
{
    tap(mods_keys | K(r, c));
    uint8_t ev[8];
    int got = read_event(ev);
    CHECK(got, "%s r%d c%d: no event", layer, r, c);
    if (!got) return;
    char txt[5] = {0};
    memcpy(txt, &ev[4], 4);
    CHECK(strcmp(txt, want) == 0, "%s r%d c%d: got '%s' want '%s'", layer, r, c, txt, want);
    CHECK(ev[1] == UAKB_KEYCODE(r, c), "%s r%d c%d: keycode %u", layer, r, c, ev[1]);
    drain();
}

static void test_layers(void)
{
    const char *(*tables[])[12] = { UA_TXT, UA_S_TXT, EN_TXT, EN_S_TXT, FN_TXT };
    const char *names[] = { "UA", "UA+Shift", "EN", "EN+Shift", "Fn" };
    const uint64_t mods[] = { 0, K(3, 0), 0, K(3, 0), K(4, 0) };
    const int ua[] = { 1, 1, 0, 0, 1 };
    for (int L = 0; L < 5; L++) {
        reset();
        A.lang_ua = (uint8_t)ua[L];
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 12; c++)
                if (tables[L][r][c]) check_text(names[L], r, c, mods[L], tables[L][r][c]);
    }
}

static void check_special(uint64_t keys, uint8_t code, const char *what)
{
    tap(keys);
    uint8_t ev[8];
    int got = read_event(ev);
    CHECK(got && ev[4] == 0 && ev[5] == code, "%s: got=%d b4=%u b5=0x%02x want 0x%02x", what, got, ev[4], ev[5], code);
    drain();
}

static void test_specials(void)
{
    reset();
    check_special(K(3, 11), UAKB_SP_UP, "Up");
    check_special(K(4, 9), UAKB_SP_LEFT, "Left");
    check_special(K(4, 10), UAKB_SP_DOWN, "Down");
    check_special(K(4, 11), UAKB_SP_RIGHT, "Right");
    check_special(K(4, 0) | K(3, 11), UAKB_SP_PGUP, "Fn+Up=PgUp");
    check_special(K(4, 0) | K(4, 9), UAKB_SP_HOME, "Fn+Left=Home");
    check_special(K(4, 0) | K(4, 10), UAKB_SP_PGDN, "Fn+Down=PgDn");
    check_special(K(4, 0) | K(4, 11), UAKB_SP_END, "Fn+Right=End");
    for (int n = 1; n <= 10; n++)
        check_special(K(4, 0) | K(0, n), (uint8_t)(UAKB_SP_F1 + n - 1), "Fn+digit=F1..F10");
    check_special(K(4, 0) | K(2, 11), UAKB_SP_F1 + 10, "Fn+Enter=F11");
    check_special(K(4, 0) | K(4, 1), UAKB_SP_F1 + 11, "Fn+Ctrl=F12");
    /* Fn+Shift+Г = Ґ */
    A.lang_ua = 1;
    check_text("Fn+Shift", 1, 6, K(4, 0) | K(3, 0), "Ґ");
    check_text("Fn+Shift", 1, 11, K(4, 0) | K(3, 0), "|");
    CHECK(A.caps == 0, "Fn+Shift+key must not toggle Caps");
    /* modifiers pressed in the same millisecond as the key still apply */
    run(K(4, 0) | K(0, 1), 10); run(0, 20);
    uint8_t ev[8];
    read_event(ev);
    CHECK(ev[4] == 0 && ev[5] == UAKB_SP_F1, "simultaneous Fn+1 = F1");
    drain();
    /* Shift released while a letter is still held: next letter is lower case */
    run(K(3, 0) | K(1, 0), 10); run(K(1, 0), 10); run(K(1, 0) | K(1, 1), 10); run(0, 20);
    read_event(ev); CHECK(ev[4] == 0xD0 && ev[5] == 0x99, "Shift+й = Й");
    read_event(ev); CHECK(ev[4] == 0xD1 && ev[5] == 0x86, "then ц lower case");
    drain();
}

static void test_lang_caps_light(void)
{
    reset();
    CHECK(read_reg(UAKB_REG_LANG) == 1, "default language must be UA");
    check_special(K(4, 3), UAKB_SP_LANG, "Lang key");
    CHECK(A.lang_ua == 0, "lang toggled to EN");
    check_text("EN after toggle", 1, 0, 0, "q");
    write_reg(UAKB_REG_LANG, 1);
    check_text("UA via register", 1, 0, 0, "й");

    check_special(K(4, 0) | K(3, 0), UAKB_SP_CAPS, "Fn+Shift=Caps");
    CHECK(A.caps == 1, "caps on");
    CHECK(read_reg(UAKB_REG_STATUS) & UAKB_STATUS_CAPS, "STATUS shows caps");
    check_text("caps", 1, 0, 0, "Й");
    check_text("caps+shift", 1, 0, K(3, 0), "й");
    check_text("caps digit", 0, 1, 0, "1");                 /* caps does not affect digits */
    check_text("caps ʼ", 0, 0, 0, "ʼ");
    A.lang_ua = 0;
    check_text("caps EN", 2, 0, 0, "A");
    check_text("caps EN ;", 2, 9, 0, ";");                  /* not a letter */
    A.lang_ua = 1; A.caps = 0;

    check_special(K(4, 0) | K(4, 3), UAKB_SP_LIGHT, "Fn+UA=Light");
    CHECK(read_reg(UAKB_REG_BACKLIGHT) == 64, "backlight step 1 = 64");
    check_special(K(4, 0) | K(4, 3), UAKB_SP_LIGHT, "Light");
    check_special(K(4, 0) | K(4, 3), UAKB_SP_LIGHT, "Light");
    CHECK(read_reg(UAKB_REG_BACKLIGHT) == 255, "backlight step 3 = 255");
    check_special(K(4, 0) | K(4, 3), UAKB_SP_LIGHT, "Light");
    CHECK(read_reg(UAKB_REG_BACKLIGHT) == 0, "backlight wraps to 0");
}

static void test_apostrophe(void)
{
    reset();
    check_text("apos default", 0, 0, 0, "ʼ");
    write_reg(UAKB_REG_CONFIG, UAKB_CFG_DEFAULT | UAKB_CFG_APOS_ASCII);
    check_text("apos ascii", 0, 0, 0, "'");
    check_text("₴ unaffected", 0, 0, K(3, 0), "₴");
}

static void test_modifiers_in_event(void)
{
    reset();
    tap(K(4, 1) | K(1, 0));                     /* Ctrl + й */
    uint8_t ev[8];
    read_event(ev);
    CHECK(ev[2] == UAKB_MOD_CTRL, "Ctrl bit in event, got 0x%02x", ev[2]);
    CHECK(ev[3] == UAKB_STATE_LANG_UA, "state byte UA");
    drain();
    tap(K(4, 1));                               /* modifier alone: no event */
    CHECK(fifo_count(&A.fifo) == 0, "modifier alone must not produce an event");
}

static void test_debounce(void)
{
    reset();
    run(K(1, 0), DEBOUNCE_MS - 1);
    run(0, 20);
    CHECK(fifo_count(&A.fifo) == 0, "glitch of %u ms must be ignored", DEBOUNCE_MS - 1);
    run(K(1, 0), DEBOUNCE_MS);
    CHECK(fifo_count(&A.fifo) == 1, "press after %u ms registered", DEBOUNCE_MS);
    /* bouncing release: 2 ms open, 1 ms closed, then open */
    run(0, 2); run(K(1, 0), 1); run(0, 20);
    CHECK(fifo_count(&A.fifo) == 1, "bounce on release must not create a second press");
}

static void test_repeat_and_release(void)
{
    reset();
    run(K(1, 0), DEBOUNCE_MS);                  /* press registered at t0 */
    CHECK(fifo_count(&A.fifo) == 1, "press");
    run(K(1, 0), 500 - 1);
    CHECK(fifo_count(&A.fifo) == 1, "no repeat before 500 ms");
    run(K(1, 0), 1);
    CHECK(fifo_count(&A.fifo) == 2, "first repeat at 500 ms");
    run(K(1, 0), 50);
    CHECK(fifo_count(&A.fifo) == 3, "next repeat 50 ms later");
    uint8_t ev[8];
    read_event(ev); CHECK(ev[0] == UAKB_EV_PRESS, "type press");
    read_event(ev); CHECK(ev[0] == UAKB_EV_REPEAT, "type repeat");
    drain();
    run(0, 20);
    CHECK(fifo_count(&A.fifo) == 0, "no release events by default");

    write_reg(UAKB_REG_CONFIG, UAKB_CFG_DEFAULT | UAKB_CFG_RELEASES);
    tap(K(1, 0));
    read_event(ev); CHECK(ev[0] == UAKB_EV_PRESS, "press");
    read_event(ev); CHECK(ev[0] == UAKB_EV_RELEASE && ev[1] == UAKB_KEYCODE(1, 0), "release reported");
    drain();

    write_reg(UAKB_REG_CONFIG, UAKB_CFG_INT_EN);         /* repeat off */
    run(K(1, 1), 1000); run(0, 20);
    CHECK(fifo_count(&A.fifo) == 1, "repeat disabled: only one event");
    drain();
    /* lang key must never repeat */
    write_reg(UAKB_REG_CONFIG, UAKB_CFG_DEFAULT);
    run(K(4, 3), 1000); run(0, 20);
    CHECK(fifo_count(&A.fifo) == 1, "lang key does not repeat");
}

static void test_fifo_i2c_semantics(void)
{
    reset();
    for (int i = 0; i < 3; i++) tap(K(1, i));
    CHECK(read_reg(UAKB_REG_COUNT) == 3, "3 events queued");
    CHECK(app_int_asserted(&A), "INT asserted while events wait");

    /* partial read of 5 bytes must not pop anything */
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_FIFO); app_i2c_end(&A, false, 0);
    app_i2c_read_begin(&A);
    for (int i = 0; i < 6; i++) (void)app_i2c_read_byte(&A);     /* 5 sent + 1 preloaded */
    app_i2c_end(&A, true, 1);
    CHECK(read_reg(UAKB_REG_COUNT) == 3, "partial read keeps the event");

    /* 16-byte burst pops exactly two events */
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_FIFO); app_i2c_end(&A, false, 0);
    app_i2c_read_begin(&A);
    uint8_t buf[17];
    for (int i = 0; i < 17; i++) buf[i] = app_i2c_read_byte(&A);
    app_i2c_end(&A, true, 1);
    CHECK(buf[1] == UAKB_KEYCODE(1, 0) && buf[9] == UAKB_KEYCODE(1, 1), "burst returns oldest first");
    CHECK(read_reg(UAKB_REG_COUNT) == 1, "burst popped two");

    /* reading past the end returns zero events and pops only real ones */
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_FIFO); app_i2c_end(&A, false, 0);
    app_i2c_read_begin(&A);
    for (int i = 0; i < 25; i++) buf[i % 17] = app_i2c_read_byte(&A);
    app_i2c_end(&A, true, 1);
    CHECK(read_reg(UAKB_REG_COUNT) == 0, "fifo empty");
    CHECK(!app_int_asserted(&A), "INT released when empty");

    /* overflow */
    for (unsigned i = 0; i < UAKB_FIFO_DEPTH + 3u; i++) tap(K(1, i % 12u));
    CHECK(read_reg(UAKB_REG_COUNT) == UAKB_FIFO_DEPTH, "fifo caps at depth");
    uint8_t st = read_reg(UAKB_REG_STATUS);
    CHECK(st & UAKB_STATUS_OVERFLOW, "overflow flagged");
    CHECK(!(read_reg(UAKB_REG_STATUS) & UAKB_STATUS_OVERFLOW), "overflow cleared after read");
    for (unsigned i = 0; i < UAKB_FIFO_DEPTH + 1u; i++) tap(K(1, i % 12u));
    (void)read_reg(UAKB_REG_VER_MINOR);        /* preloads STATUS, which is never sent */
    CHECK(read_reg(UAKB_REG_STATUS) & UAKB_STATUS_OVERFLOW, "overflow survives a preloaded, unsent STATUS byte");
    CHECK(!(read_reg(UAKB_REG_STATUS) & UAKB_STATUS_OVERFLOW), "and is cleared by the read that sent it");
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_COMMAND); app_i2c_write_byte(&A, UAKB_CMD_CLEAR_FIFO);
    app_i2c_end(&A, false, 0);

    /* clear command */
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_COMMAND); app_i2c_write_byte(&A, UAKB_CMD_CLEAR_FIFO);
    app_i2c_end(&A, false, 0);
    CHECK(read_reg(UAKB_REG_COUNT) == 0, "clear fifo command");

    /* INT disabled */
    write_reg(UAKB_REG_CONFIG, UAKB_CFG_DEFAULT & ~UAKB_CFG_INT_EN);
    tap(K(1, 0));
    CHECK(!app_int_asserted(&A), "INT stays released when disabled");
}

static void test_registers(void)
{
    reset();
    CHECK(read_reg(UAKB_REG_ID) == UAKB_ID, "ID");
    CHECK(read_reg(UAKB_REG_VER_MAJOR) == UAKB_VERSION_MAJOR, "version major");
    CHECK(read_reg(UAKB_REG_CONFIG) == UAKB_CFG_DEFAULT, "config default 0x%02x", read_reg(UAKB_REG_CONFIG));
    CHECK(read_reg(UAKB_REG_REPEAT_DELAY) == 50 && read_reg(UAKB_REG_REPEAT_RATE) == 5, "repeat defaults");
    /* auto-increment burst read 0x00..0x04 */
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, 0x00); app_i2c_end(&A, false, 0);
    app_i2c_read_begin(&A);
    uint8_t b[6];
    for (int i = 0; i < 6; i++) b[i] = app_i2c_read_byte(&A);
    app_i2c_end(&A, true, 1);
    CHECK(b[0] == UAKB_ID && b[1] == UAKB_VERSION_MAJOR && b[2] == UAKB_VERSION_MINOR && b[4] == 0, "burst register read");
    /* burst write CONFIG, DELAY, RATE */
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_CONFIG);
    app_i2c_write_byte(&A, 0xFF); app_i2c_write_byte(&A, 30); app_i2c_write_byte(&A, 0);
    app_i2c_end(&A, false, 0);
    CHECK(read_reg(UAKB_REG_CONFIG) == 0x1F, "config masks unknown bits");
    CHECK(read_reg(UAKB_REG_REPEAT_DELAY) == 30, "delay written");
    CHECK(read_reg(UAKB_REG_REPEAT_RATE) == 1, "rate 0 clamps to 1");
    write_reg(UAKB_REG_LED, 7);
    CHECK(read_reg(UAKB_REG_LED) == UAKB_LED_AUTO, "invalid LED mode ignored");
    write_reg(UAKB_REG_LED, UAKB_LED_ON);
    CHECK(read_reg(UAKB_REG_LED) == UAKB_LED_ON, "LED mode on");
    write_reg(UAKB_REG_ID, 0x00);
    CHECK(read_reg(UAKB_REG_ID) == UAKB_ID, "read-only register unchanged");

    /* destructive commands need confirmation */
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_COMMAND); app_i2c_write_byte(&A, UAKB_CMD_BOOTLOADER);
    app_i2c_end(&A, false, 0);
    CHECK(A.pending == PENDING_NONE, "bootloader without confirm ignored");
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_COMMAND); app_i2c_write_byte(&A, UAKB_CMD_BOOTLOADER);
    app_i2c_write_byte(&A, UAKB_CMD_CONFIRM); app_i2c_end(&A, false, 0);
    CHECK(A.pending == PENDING_BOOTLOADER, "bootloader with confirm");
    A.pending = PENDING_NONE;
    app_i2c_write_begin(&A); app_i2c_write_byte(&A, UAKB_REG_COMMAND); app_i2c_write_byte(&A, UAKB_CMD_RESET);
    app_i2c_write_byte(&A, UAKB_CMD_CONFIRM); app_i2c_end(&A, false, 0);
    CHECK(A.pending == PENDING_RESET, "reset with confirm");
}

static void test_utf8(void)
{
    uint8_t o[4];
    CHECK(utf8_encode(0x41, o) == 1 && o[0] == 0x41, "ascii");
    CHECK(utf8_encode(0x0439, o) == 2 && o[0] == 0xD0 && o[1] == 0xB9, "й");
    CHECK(utf8_encode(0x20B4, o) == 3 && o[0] == 0xE2 && o[1] == 0x82 && o[2] == 0xB4, "₴");
    CHECK(utf8_encode(0x1F600, o) == 4 && o[0] == 0xF0, "4-byte");
    CHECK(utf8_encode(0xD800, o) == 0, "surrogate rejected");
    CHECK(utf8_encode(0x110000, o) == 0, "out of range rejected");
}

static void test_busy(void)
{
    reset();
    CHECK(!app_busy(&A), "idle");
    run(K(1, 0), 2);
    CHECK(app_busy(&A), "busy while debouncing");
    run(K(1, 0), 10);
    CHECK(app_busy(&A), "busy while key held");
    run(0, 10);
    CHECK(!app_busy(&A), "idle after release");
}

/* The example events printed in docs/PROTOCOL.md, byte for byte. */
static void expect_bytes(const char *what, const uint8_t want[8])
{
    uint8_t ev[8];
    int got = read_event(ev);
    CHECK(got && memcmp(ev, want, 8) == 0,
          "doc example %s: got %02x %02x %02x %02x %02x %02x %02x %02x", what,
          ev[0], ev[1], ev[2], ev[3], ev[4], ev[5], ev[6], ev[7]);
}

static void test_doc_examples(void)
{
    reset();
    tap(K(1, 1));
    expect_bytes("ц", (const uint8_t[8]){ 0x01, 0x0D, 0x00, 0x01, 0xD1, 0x86, 0, 0 });
    A.lang_ua = 0;
    tap(K(3, 0) | K(3, 5));
    expect_bytes("Shift+B", (const uint8_t[8]){ 0x01, 0x29, 0x01, 0x00, 0x42, 0, 0, 0 });
    A.lang_ua = 1;
    tap(K(4, 3));
    expect_bytes("lang", (const uint8_t[8]){ 0x01, 0x33, 0x00, 0x00, 0x00, 0x20, 0, 0 });
    A.lang_ua = 1;
    run(K(0, 11), DEBOUNCE_MS + 500); run(0, 20);
    uint8_t ev[8];
    read_event(ev);
    expect_bytes("Bksp repeat", (const uint8_t[8]){ 0x03, 0x0B, 0x00, 0x01, 0x08, 0, 0, 0 });
    drain();
    tap(K(4, 1) | K(4, 9));
    expect_bytes("Ctrl+Left", (const uint8_t[8]){ 0x01, 0x39, 0x02, 0x01, 0x00, 0x03, 0, 0 });

    /* a read without a new pointer write starts again at the same register */
    write_reg(UAKB_REG_ID, 0x55);                 /* read-only: ignored, pointer moves to 0x01 */
    CHECK(A.ptr == UAKB_REG_VER_MAJOR, "write to read-only register still advances the pointer");
    app_i2c_write_begin(&A);
    app_i2c_write_byte(&A, UAKB_REG_ID);          /* pointer only */
    app_i2c_end(&A, false, 0);
    app_i2c_read_begin(&A);
    uint8_t a = app_i2c_read_byte(&A);
    app_i2c_end(&A, true, 0);
    app_i2c_read_begin(&A);
    uint8_t b = app_i2c_read_byte(&A);
    app_i2c_end(&A, true, 0);
    CHECK(a == UAKB_ID && b == UAKB_ID, "repeated reads start at the same register (%02x %02x)", a, b);
}

int main(void)
{
    test_doc_examples();
    test_utf8();
    test_layers();
    test_specials();
    test_lang_caps_light();
    test_apostrophe();
    test_modifiers_in_event();
    test_debounce();
    test_repeat_and_release();
    test_fifo_i2c_semantics();
    test_registers();
    test_busy();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
