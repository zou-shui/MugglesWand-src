#include "BLEKeySequence.h"
#include "Service/BLEHIDKeys.h"
#include "Service/DualPrint.h"

#include <ctype.h>
#include <string.h>

namespace
{

struct KeyNameDef
{
    const char *name;
    uint8_t keycode;
    uint8_t modifiers;
};

struct ModifierNameDef
{
    const char *name;
    uint8_t bit;
};

const KeyNameDef kKeyNames[] = {
    {"enter", KEY_RETURN, 0},
    {"return", KEY_RETURN, 0},
    {"esc", KEY_ESCAPE, 0},
    {"escape", KEY_ESCAPE, 0},
    {"tab", KEY_TAB, 0},
    {"backspace", KEY_BACKSPACE, 0},
    {"space", KEY_SPACE, 0},
    {"delete", KEY_DELETE, 0},
    {"del", KEY_DELETE, 0},
    {"insert", KEY_INSERT, 0},
    {"ins", KEY_INSERT, 0},
    {"home", KEY_HOME, 0},
    {"end", KEY_END, 0},
    {"pageup", KEY_PAGE_UP, 0},
    {"pgup", KEY_PAGE_UP, 0},
    {"pagedown", KEY_PAGE_DOWN, 0},
    {"pgdn", KEY_PAGE_DOWN, 0},
    {"left", KEY_LEFT, 0},
    {"right", KEY_RIGHT, 0},
    {"up", KEY_UP, 0},
    {"down", KEY_DOWN, 0},
    {"capslock", KEY_CAPS_LOCK, 0},
    {"numlock", KEY_NUM_LOCK, 0},
    {"scrolllock", KEY_SCROLL_LOCK, 0},
    {"printscreen", KEY_PRINT_SCREEN, 0},
    {"pause", KEY_PAUSE, 0},
    {"menu", KEY_APPLICATION, 0},
    {"f1", KEY_F1, 0},
    {"f2", KEY_F2, 0},
    {"f3", KEY_F3, 0},
    {"f4", KEY_F4, 0},
    {"f5", KEY_F5, 0},
    {"f6", KEY_F6, 0},
    {"f7", KEY_F7, 0},
    {"f8", KEY_F8, 0},
    {"f9", KEY_F9, 0},
    {"f10", KEY_F10, 0},
    {"f11", KEY_F11, 0},
    {"f12", KEY_F12, 0},
    {"comma", KEY_COMMA, 0},
    {"minus", KEY_MINUS, 0},
    {"equal", KEY_EQUAL, 0},
    {"leftbracket", KEY_LEFTBRACE, 0},
    {"rightbracket", KEY_RIGHTBRACE, 0},
    {"backslash", KEY_BACKSLASH, 0},
    {"semicolon", KEY_SEMICOLON, 0},
    {"apostrophe", KEY_APOSTROPHE, 0},
    {"grave", KEY_GRAVE, 0},
    {"dot", KEY_DOT, 0},
    {"slash", KEY_SLASH, 0},
    {"plus", KEY_EQUAL, KEY_MOD_LSHIFT},
    {"leftparen", KEY_9, KEY_MOD_LSHIFT},
    {"rightparen", KEY_0, KEY_MOD_LSHIFT},
};

const ModifierNameDef kModifierNames[] = {
    {"ctrl", KEY_MOD_LCTRL},
    {"control", KEY_MOD_LCTRL},
    {"leftctrl", KEY_MOD_LCTRL},
    {"lctrl", KEY_MOD_LCTRL},
    {"rightctrl", KEY_MOD_RCTRL},
    {"rctrl", KEY_MOD_RCTRL},
    {"shift", KEY_MOD_LSHIFT},
    {"leftshift", KEY_MOD_LSHIFT},
    {"lshift", KEY_MOD_LSHIFT},
    {"rightshift", KEY_MOD_RSHIFT},
    {"rshift", KEY_MOD_RSHIFT},
    {"alt", KEY_MOD_LALT},
    {"leftalt", KEY_MOD_LALT},
    {"lalt", KEY_MOD_LALT},
    {"rightalt", KEY_MOD_RALT},
    {"ralt", KEY_MOD_RALT},
    {"altgr", KEY_MOD_RALT},
    {"win", KEY_MOD_LGUI},
    {"windows", KEY_MOD_LGUI},
    {"gui", KEY_MOD_LGUI},
    {"super", KEY_MOD_LGUI},
    {"meta", KEY_MOD_LGUI},
    {"leftwin", KEY_MOD_LGUI},
    {"lwin", KEY_MOD_LGUI},
    {"rightwin", KEY_MOD_RGUI},
    {"rwin", KEY_MOD_RGUI},
};

bool equals_ci(const char *text, size_t length, const char *expected)
{
    size_t expected_length = strlen(expected);
    if (length != expected_length)
        return false;

    for (size_t i = 0; i < length; ++i)
    {
        unsigned char left = (unsigned char)text[i];
        unsigned char right = (unsigned char)expected[i];
        if (tolower(left) != tolower(right))
            return false;
    }
    return true;
}

bool lookup_modifier(const char *text, size_t length, uint8_t &bit)
{
    for (const ModifierNameDef &entry : kModifierNames)
    {
        if (equals_ci(text, length, entry.name))
        {
            bit = entry.bit;
            return true;
        }
    }
    return false;
}

bool lookup_printable(unsigned char c, uint8_t &keycode, uint8_t &modifiers)
{
    modifiers = 0;

    if (c >= 'a' && c <= 'z')
    {
        keycode = KEY_A + (c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'Z')
    {
        keycode = KEY_A + (c - 'A');
        modifiers = KEY_MOD_LSHIFT;
        return true;
    }
    if (c >= '1' && c <= '9')
    {
        keycode = KEY_1 + (c - '1');
        return true;
    }

    switch (c)
    {
    case '0':
        keycode = KEY_0;
        return true;
    case ' ':
        keycode = KEY_SPACE;
        return true;
    case ',':
        keycode = KEY_COMMA;
        return true;
    case '.':
        keycode = KEY_DOT;
        return true;
    case '/':
        keycode = KEY_SLASH;
        return true;
    case ';':
        keycode = KEY_SEMICOLON;
        return true;
    case '\'':
        keycode = KEY_APOSTROPHE;
        return true;
    case '`':
        keycode = KEY_GRAVE;
        return true;
    case '-':
        keycode = KEY_MINUS;
        return true;
    case '=':
        keycode = KEY_EQUAL;
        return true;
    case '[':
        keycode = KEY_LEFTBRACE;
        return true;
    case ']':
        keycode = KEY_RIGHTBRACE;
        return true;
    case '\\':
        keycode = KEY_BACKSLASH;
        return true;
    case '!':
        keycode = KEY_1;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '@':
        keycode = KEY_2;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '#':
        keycode = KEY_3;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '$':
        keycode = KEY_4;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '%':
        keycode = KEY_5;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '^':
        keycode = KEY_6;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '&':
        keycode = KEY_7;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '*':
        keycode = KEY_8;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '(':
        keycode = KEY_9;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case ')':
        keycode = KEY_0;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '_':
        keycode = KEY_MINUS;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '+':
        keycode = KEY_EQUAL;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '{':
        keycode = KEY_LEFTBRACE;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '}':
        keycode = KEY_RIGHTBRACE;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '|':
        keycode = KEY_BACKSLASH;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case ':':
        keycode = KEY_SEMICOLON;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '"':
        keycode = KEY_APOSTROPHE;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '<':
        keycode = KEY_COMMA;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '>':
        keycode = KEY_DOT;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '?':
        keycode = KEY_SLASH;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    case '~':
        keycode = KEY_GRAVE;
        modifiers = KEY_MOD_LSHIFT;
        return true;
    default:
        return false;
    }
}

bool lookup_key(const char *text, size_t length, uint8_t &keycode, uint8_t &modifiers)
{
    for (const KeyNameDef &entry : kKeyNames)
    {
        if (equals_ci(text, length, entry.name))
        {
            keycode = entry.keycode;
            modifiers = entry.modifiers;
            return true;
        }
    }

    if (length == 1)
        return lookup_printable((unsigned char)text[0], keycode, modifiers);

    return false;
}

bool parse_decimal(const char *text, size_t length, uint16_t &value)
{
    if (length == 0 || length > 5)
        return false;

    uint32_t parsed = 0;
    for (size_t i = 0; i < length; ++i)
    {
        if (!isdigit((unsigned char)text[i]))
            return false;
        parsed = parsed * 10 + (uint32_t)(text[i] - '0');
    }

    if (parsed > BLE_KEY_TIMING_MAX_MS)
        return false;

    value = (uint16_t)parsed;
    return true;
}

bool parse_timing(const char *timing, size_t length,
                  uint16_t &hold_ms, uint16_t &delay_ms)
{
    hold_ms = BLE_KEY_DEFAULT_HOLD_MS;
    delay_ms = BLE_KEY_DEFAULT_DELAY_MS;
    if (length == 0)
        return true;

    const char *comma = (const char *)memchr(timing, ',', length);
    if (comma == nullptr)
        return false;
    if (memchr(comma + 1, ',', length - (size_t)(comma + 1 - timing)) != nullptr)
        return false;

    size_t hold_length = (size_t)(comma - timing);
    size_t delay_length = length - hold_length - 1;

    if (hold_length > 0 && !parse_decimal(timing, hold_length, hold_ms))
        return false;
    if (delay_length > 0 && !parse_decimal(comma + 1, delay_length, delay_ms))
        return false;
    return true;
}

bool emit_comma_action(BLEKeySequenceCallback callback, void *context)
{
    BLEKeyAction action = {
        (uint8_t)0,
        KEY_COMMA,
        BLE_KEY_DEFAULT_HOLD_MS,
        BLE_KEY_DEFAULT_DELAY_MS,
    };
    return callback == nullptr || callback(action, context);
}

bool invalid_sequence(const char *reason, const char *token = nullptr)
{
    if (token != nullptr)
        DualSerial.printf("[Keyboard] invalid sequence (%s): %s\n", reason, token);
    else
        DualSerial.printf("[Keyboard] invalid sequence: %s\n", reason);
    return false;
}

bool emit_token(const char *token, size_t length,
                BLEKeySequenceCallback callback, void *context)
{
    size_t timing_pos = length;
    for (size_t i = 0; i < length; ++i)
    {
        if (token[i] == '(')
        {
            timing_pos = i;
            break;
        }
    }

    size_t base_length = timing_pos;
    uint16_t hold_ms = BLE_KEY_DEFAULT_HOLD_MS;
    uint16_t delay_ms = BLE_KEY_DEFAULT_DELAY_MS;

    if (timing_pos < length)
    {
        if (length - timing_pos < 2 || token[length - 1] != ')')
            return invalid_sequence("unterminated timing", token);

        const char *timing = token + timing_pos + 1;
        size_t timing_length = length - timing_pos - 2;
        if (memchr(timing, '(', timing_length) != nullptr ||
            memchr(timing, ')', timing_length) != nullptr)
        {
            return invalid_sequence("malformed timing", token);
        }
        if (!parse_timing(timing, timing_length, hold_ms, delay_ms))
            return invalid_sequence("invalid timing value", token);
    }

    if (base_length == 0)
        return invalid_sequence("empty key", token);

    uint8_t modifiers = 0;
    uint8_t keycode = 0;

    if (token[0] == '[')
    {
        if (base_length < 3 || token[base_length - 1] != ']')
            return invalid_sequence("malformed key combination", token);

        size_t segment_start = 1;
        bool found_non_modifier = false;
        for (size_t i = 1; i < base_length; ++i)
        {
            bool at_end = i == base_length - 1;
            if (token[i] != '+' && !at_end)
                continue;

            size_t segment_length = i - segment_start;
            if (segment_length == 0)
                return invalid_sequence("empty combination item", token);

            uint8_t modifier_bit = 0;
            if (lookup_modifier(token + segment_start, segment_length, modifier_bit))
            {
                modifiers |= modifier_bit;
            }
            else
            {
                uint8_t key_modifiers = 0;
                uint8_t parsed_key = 0;
                if (!lookup_key(token + segment_start, segment_length,
                                parsed_key, key_modifiers))
                {
                    return invalid_sequence("unknown key in combination", token);
                }
                if (found_non_modifier)
                    return invalid_sequence("multiple keys in combination", token);

                keycode = parsed_key;
                modifiers |= key_modifiers;
                found_non_modifier = true;
            }

            segment_start = i + 1;
        }

        if (segment_start != base_length)
            return invalid_sequence("malformed key combination", token);
    }
    else
    {
        uint8_t modifier_bit = 0;
        if (lookup_modifier(token, base_length, modifier_bit))
        {
            modifiers = modifier_bit;
        }
        else if (!lookup_key(token, base_length, keycode, modifiers))
        {
            return invalid_sequence("unknown key", token);
        }
    }

    BLEKeyAction action = {modifiers, keycode, hold_ms, delay_ms};
    return callback == nullptr || callback(action, context);
}

} // namespace

bool BLEKeySequence_parse(const char *sequence,
                          BLEKeySequenceCallback callback,
                          void *context)
{
    if (sequence == nullptr || sequence[0] == '\0')
        return invalid_sequence("sequence is empty");
    if (strlen(sequence) >= BLE_KEY_SEQUENCE_MAX)
        return invalid_sequence("sequence is too long");

    char token[BLE_KEY_SEQUENCE_MAX] = {};
    size_t token_length = 0;
    bool emitted_any = false;
    int bracket_depth = 0;
    int timing_depth = 0;

    for (size_t i = 0;; ++i)
    {
        char c = sequence[i];
        if (c == '\0')
        {
            if (bracket_depth != 0 || timing_depth != 0)
                return invalid_sequence("unclosed bracket");
            if (token_length == 0)
                return invalid_sequence("trailing separator");

            token[token_length] = '\0';
            if (!emit_token(token, token_length, callback, context))
                return false;
            return emitted_any || token_length > 0;
        }

        if (c == ',' && bracket_depth == 0 && timing_depth == 0)
        {
            size_t run_length = 1;
            while (sequence[i + run_length] == ',')
                ++run_length;

            if (token_length == 0 || (run_length % 2) == 0)
                return invalid_sequence("ambiguous comma sequence");

            token[token_length] = '\0';
            if (!emit_token(token, token_length, callback, context))
                return false;
            emitted_any = true;
            token_length = 0;

            // Between ordinary keys, 2*n+1 commas encode n literal comma keys.
            size_t comma_keys = run_length / 2;
            for (size_t j = 0; j < comma_keys; ++j)
            {
                if (!emit_comma_action(callback, context))
                    return false;
                emitted_any = true;
            }

            i += run_length - 1;
            continue;
        }

        if (c == '[')
        {
            if (bracket_depth != 0)
                return invalid_sequence("nested key combination");
            ++bracket_depth;
        }
        else if (c == ']')
        {
            if (bracket_depth == 0)
                return invalid_sequence("unexpected closing bracket");
            --bracket_depth;
        }
        else if (c == '(')
        {
            if (timing_depth != 0)
                return invalid_sequence("nested timing");
            ++timing_depth;
        }
        else if (c == ')')
        {
            if (timing_depth == 0)
                return invalid_sequence("unexpected closing parenthesis");
            --timing_depth;
        }

        if (token_length + 1 >= sizeof(token))
            return invalid_sequence("token is too long");
        token[token_length++] = c;
    }
}
