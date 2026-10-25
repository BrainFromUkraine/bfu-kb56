/* SPDX-FileCopyrightText: 2026 BFU Electronics LTD
 * SPDX-License-Identifier: Apache-2.0 */
/*
 * Layout tables: 5 rows x 12 columns, matching the PCB matrix (ROW0..4, COL0..11).
 * Row 4 columns 4, 6, 7, 8 are not populated; the space bar is at column 5.
 */
#include "keymap.h"

#define SP(x)   KA_SPECIAL(UAKB_SP_##x)
#define MD(x)   KA_MOD(UAKB_MOD_##x)
#define BS      0x08u
#define TAB     0x09u
#define ENT     0x0Au
#define ESC     0x1Bu
#define DEL     0x7Fu
#define SPC     0x20u
#define F(n)    KA_SPECIAL(UAKB_SP_F1 + (n) - 1u)

#define ROW4        { MD(FN), MD(CTRL), MD(ALT), SP(LANG), 0, SPC, 0, 0, 0, SP(LEFT), SP(DOWN), SP(RIGHT) }

static const kact_t L_UA[UAKB_ROWS][UAKB_COLS] = {
    { KA_APOS, U'1', U'2', U'3', U'4', U'5', U'6', U'7', U'8', U'9', U'0', BS },
    { U'й', U'ц', U'у', U'к', U'е', U'н', U'г', U'ш', U'щ', U'з', U'х', U'ї' },
    { U'ф', U'і', U'в', U'а', U'п', U'р', U'о', U'л', U'д', U'ж', U'є', ENT },
    { MD(SHIFT), U'я', U'ч', U'с', U'м', U'и', U'т', U'ь', U'б', U'ю', U'.', SP(UP) },
    ROW4,
};

static const kact_t L_UA_S[UAKB_ROWS][UAKB_COLS] = {
    { U'₴', U'!', U'"', U'№', U';', U'%', U':', U'?', U'*', U'(', U')', BS },
    { U'Й', U'Ц', U'У', U'К', U'Е', U'Н', U'Г', U'Ш', U'Щ', U'З', U'Х', U'Ї' },
    { U'Ф', U'І', U'В', U'А', U'П', U'Р', U'О', U'Л', U'Д', U'Ж', U'Є', ENT },
    { MD(SHIFT), U'Я', U'Ч', U'С', U'М', U'И', U'Т', U'Ь', U'Б', U'Ю', U',', SP(UP) },
    ROW4,
};

static const kact_t L_EN[UAKB_ROWS][UAKB_COLS] = {
    { U'`', U'1', U'2', U'3', U'4', U'5', U'6', U'7', U'8', U'9', U'0', BS },
    { U'q', U'w', U'e', U'r', U't', U'y', U'u', U'i', U'o', U'p', U'[', U']' },
    { U'a', U's', U'd', U'f', U'g', U'h', U'j', U'k', U'l', U';', U'\'', ENT },
    { MD(SHIFT), U'z', U'x', U'c', U'v', U'b', U'n', U'm', U',', U'.', U'/', SP(UP) },
    ROW4,
};

static const kact_t L_EN_S[UAKB_ROWS][UAKB_COLS] = {
    { U'~', U'!', U'@', U'#', U'$', U'%', U'^', U'&', U'*', U'(', U')', BS },
    { U'Q', U'W', U'E', U'R', U'T', U'Y', U'U', U'I', U'O', U'P', U'{', U'}' },
    { U'A', U'S', U'D', U'F', U'G', U'H', U'J', U'K', U'L', U':', U'"', ENT },
    { MD(SHIFT), U'Z', U'X', U'C', U'V', U'B', U'N', U'M', U'<', U'>', U'?', SP(UP) },
    ROW4,
};

/* Fn layer is the same in both languages */
static const kact_t L_FN[UAKB_ROWS][UAKB_COLS] = {
    { ESC, F(1), F(2), F(3), F(4), F(5), F(6), F(7), F(8), F(9), F(10), DEL },
    { TAB, U'-', U'_', U'=', U'+', U'~', U'ґ', U'[', U']', U'{', U'}', U'\\' },
    { U'!', U'@', U'#', U'$', U'%', U'^', U'&', U'*', U'(', U')', U'"', F(11) },
    { SP(CAPS), U'`', U'|', U'/', U'?', U':', U';', U'\'', U'<', U'>', U',', SP(PGUP) },
    { MD(FN), F(12), MD(ALT), SP(LIGHT), 0, SPC, 0, 0, 0, SP(HOME), SP(PGDN), SP(END) },
};

static const kact_t L_FN_S[UAKB_ROWS][UAKB_COLS] = {
    { ESC, F(1), F(2), F(3), F(4), F(5), F(6), F(7), F(8), F(9), F(10), DEL },
    { TAB, U'-', U'_', U'=', U'+', U'~', U'Ґ', U'[', U']', U'{', U'}', U'|' },
    { U'!', U'@', U'#', U'$', U'%', U'^', U'&', U'*', U'(', U')', U'"', F(11) },
    { SP(CAPS), U'`', U'|', U'/', U'?', U':', U';', U'\'', U'<', U'>', U',', SP(PGUP) },
    { MD(FN), F(12), MD(ALT), SP(LIGHT), 0, SPC, 0, 0, 0, SP(HOME), SP(PGDN), SP(END) },
};

static bool is_letter(kact_t a)
{
    if (!KA_IS_TEXT(a)) {
        return false;
    }
    if ((a >= U'a' && a <= U'z') || (a >= U'A' && a <= U'Z')) {
        return true;
    }
    /* Cyrillic block letters used by the layout */
    return (a >= 0x0400u && a <= 0x045Fu) || a == 0x0490u || a == 0x0491u;
}

bool keymap_present(uint8_t pos)
{
    if (pos >= KEY_COUNT) {
        return false;
    }
    return L_UA[pos / UAKB_COLS][pos % UAKB_COLS] != KA_NONE;
}

kact_t keymap_resolve(uint8_t pos, bool lang_ua, bool shift, bool fn, bool caps, bool apos_ascii)
{
    if (pos >= KEY_COUNT) {
        return KA_NONE;
    }
    const uint8_t r = pos / UAKB_COLS, c = pos % UAKB_COLS;
    const kact_t (*base)[UAKB_COLS];
    const kact_t (*shifted)[UAKB_COLS];

    if (fn) {
        base = L_FN;
        shifted = L_FN_S;
    } else if (lang_ua) {
        base = L_UA;
        shifted = L_UA_S;
    } else {
        base = L_EN;
        shifted = L_EN_S;
    }

    const kact_t lo = base[r][c], hi = shifted[r][c];
    bool use_shift = shift;
    if (caps && is_letter(lo) && is_letter(hi) && lo != hi) {
        use_shift = !use_shift;
    }
    kact_t a = use_shift ? hi : lo;
    if (a == KA_APOS) {
        a = apos_ascii ? (kact_t)'\'' : 0x02BCu;
    }
    return a;
}
