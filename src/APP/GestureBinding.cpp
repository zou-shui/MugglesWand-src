#include "GestureBinding.h"

#include "APP_BLE_HID.h"
#include "APP_EspNow.h"
#include "APP_Lumos.h"
#include "APP_POV.h"
#include "Model/gesture_inference.h"
#include "Service/DualPrint.h"

#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>
#include <new>
#include <string.h>

namespace
{

using BindingValidator = bool (*)(const char *parameter);
using BindingExecutor = bool (*)(const char *parameter);

struct AppHandler
{
    const char *name;
    BindingValidator validate;
    BindingExecutor execute;
    bool blank_parameter_only;
};

// Heap-backed fixed-count buffer used by loading and command snapshots.
// The logical gesture count comes from the inference module at runtime.
class BindingBuffer
{
public:
    explicit BindingBuffer(size_t count)
        : data_(new (std::nothrow) GestureBinding[count]()), count_(count)
    {
    }

    ~BindingBuffer()
    {
        delete[] data_;
    }

    BindingBuffer(const BindingBuffer &) = delete;
    BindingBuffer &operator=(const BindingBuffer &) = delete;

    GestureBinding *data() { return data_; }
    const GestureBinding *data() const { return data_; }
    size_t count() const { return count_; }
    explicit operator bool() const { return data_ != nullptr; }

private:
    GestureBinding *data_;
    size_t count_;
};

constexpr char kNvsNamespace[] = "wand_map";
constexpr char kNvsKey[] = "table";
constexpr uint8_t kRecordMagic[4] = {'M', 'W', 'G', '1'};
constexpr uint16_t kRecordVersion = 1;
constexpr size_t kRecordHeaderSize = 12;
constexpr size_t kRecordEntrySize =
    GESTURE_APP_NAME_SIZE + GESTURE_PARAMETER_SIZE;
constexpr size_t kRecordCrcSize = sizeof(uint32_t);

GestureBinding *s_bindings = nullptr;
size_t s_binding_count = 0;
SemaphoreHandle_t s_binding_mutex = nullptr;
SemaphoreHandle_t s_update_mutex = nullptr;

void copy_text(char *destination, size_t capacity, const char *source)
{
    if (destination == nullptr || capacity == 0)
        return;
    if (source == nullptr)
        source = "";

    size_t length = 0;
    while (source[length] != '\0' && length + 1 < capacity)
        ++length;

    memcpy(destination, source, length);
    destination[length] = '\0';
}

void copy_binding(GestureBinding &destination, const GestureBinding &source)
{
    memset(&destination, 0, sizeof(destination));
    copy_text(destination.app, sizeof(destination.app), source.app);
    copy_text(destination.parameter, sizeof(destination.parameter),
              source.parameter);
}

bool is_blank(const char *text)
{
    if (text == nullptr)
        return true;
    while (*text != '\0')
    {
        if (!isspace((unsigned char)*text))
            return false;
        ++text;
    }
    return true;
}

bool validate_blank(const char *parameter)
{
    return is_blank(parameter);
}

void set_default_binding(GestureBinding *bindings, size_t count,
                         size_t index, const char *app,
                         const char *parameter = "")
{
    if (bindings == nullptr || index >= count)
        return;

    copy_text(bindings[index].app, sizeof(bindings[index].app), app);
    copy_text(bindings[index].parameter,
              sizeof(bindings[index].parameter), parameter);
}

void load_defaults(GestureBinding *bindings, size_t count)
{
    if (bindings == nullptr)
        return;

    memset(bindings, 0, sizeof(GestureBinding) * count);
    set_default_binding(bindings, count, 0, "espnow", "0x02");
    set_default_binding(bindings, count, 1, "espnow", "0x03");
    set_default_binding(bindings, count, 2, "ble_keyboard", "up");
    set_default_binding(bindings, count, 3, "ble_keyboard", "down");
    set_default_binding(bindings, count, 4, "ble_mouse");
    set_default_binding(bindings, count, 5, "lumos");
    set_default_binding(bindings, count, 6, "ble_knob");
    set_default_binding(bindings, count, 7, "pov");
}

const AppHandler kAppHandlers[] = {
    {"espnow", APP_espnow_validate, APP_espnow_execute, false},
    {"lumos", validate_blank, APP_Lumos_execute, true},
    {"pov", validate_blank, APP_POV_execute, true},
    {"ble_keyboard", APP_ble_keyboard_validate, APP_ble_keyboard_execute, false},
    {"ble_mouse", validate_blank, APP_ble_mouse_execute, true},
    {"ble_knob", validate_blank, APP_ble_knob_execute, true},
};

const AppHandler *find_app_handler(const char *app)
{
    if (app == nullptr)
        return nullptr;

    for (const AppHandler &handler : kAppHandlers)
    {
        if (strcmp(handler.name, app) == 0)
            return &handler;
    }
    return nullptr;
}

void put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFF);
    destination[1] = (uint8_t)((value >> 8) & 0xFF);
}

uint16_t get_u16(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8);
}

void put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFF);
    destination[1] = (uint8_t)((value >> 8) & 0xFF);
    destination[2] = (uint8_t)((value >> 16) & 0xFF);
    destination[3] = (uint8_t)((value >> 24) & 0xFF);
}

uint32_t get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

size_t record_size(size_t count)
{
    return kRecordHeaderSize + count * kRecordEntrySize + kRecordCrcSize;
}

void encode_record(const GestureBinding *bindings, size_t count,
                   uint8_t *record)
{
    size_t length = record_size(count);
    memset(record, 0, length);
    memcpy(record, kRecordMagic, sizeof(kRecordMagic));
    put_u16(record + 4, kRecordVersion);
    put_u16(record + 6, (uint16_t)count);
    record[8] = (uint8_t)GESTURE_APP_NAME_SIZE;
    record[9] = (uint8_t)GESTURE_PARAMETER_SIZE;

    uint8_t *entry = record + kRecordHeaderSize;
    for (size_t i = 0; i < count; ++i)
    {
        memcpy(entry, bindings[i].app, GESTURE_APP_NAME_SIZE);
        entry += GESTURE_APP_NAME_SIZE;
        memcpy(entry, bindings[i].parameter, GESTURE_PARAMETER_SIZE);
        entry += GESTURE_PARAMETER_SIZE;
    }

    uint32_t checksum = crc32(record, length - kRecordCrcSize);
    put_u32(record + length - kRecordCrcSize, checksum);
}

enum class LoadResult
{
    Valid,
    Missing,
    Invalid,
    IoError,
};

LoadResult decode_record(const uint8_t *record, size_t length, size_t count,
                         GestureBinding *bindings)
{
    if (count == 0 || length != record_size(count))
        return LoadResult::Invalid;
    if (memcmp(record, kRecordMagic, sizeof(kRecordMagic)) != 0)
        return LoadResult::Invalid;
    if (get_u16(record + 4) != kRecordVersion ||
        get_u16(record + 6) != count ||
        record[8] != GESTURE_APP_NAME_SIZE ||
        record[9] != GESTURE_PARAMETER_SIZE)
    {
        return LoadResult::Invalid;
    }

    uint32_t expected_crc = get_u32(record + length - kRecordCrcSize);
    if (crc32(record, length - kRecordCrcSize) != expected_crc)
        return LoadResult::Invalid;

    memset(bindings, 0, sizeof(GestureBinding) * count);
    const uint8_t *entry = record + kRecordHeaderSize;
    for (size_t i = 0; i < count; ++i)
    {
        const char *app = (const char *)entry;
        entry += GESTURE_APP_NAME_SIZE;
        const char *parameter = (const char *)entry;
        entry += GESTURE_PARAMETER_SIZE;

        if (memchr(app, '\0', GESTURE_APP_NAME_SIZE) == nullptr ||
            memchr(parameter, '\0', GESTURE_PARAMETER_SIZE) == nullptr)
        {
            return LoadResult::Invalid;
        }

        copy_text(bindings[i].app, sizeof(bindings[i].app), app);
        copy_text(bindings[i].parameter, sizeof(bindings[i].parameter),
                  parameter);
    }
    return LoadResult::Valid;
}

LoadResult load_from_nvs(GestureBinding *bindings, size_t count)
{
    Preferences preferences;
    if (!preferences.begin(kNvsNamespace, true))
        return LoadResult::IoError;

    size_t stored_length = preferences.getBytesLength(kNvsKey);
    size_t expected_length = record_size(count);
    if (stored_length == 0)
    {
        preferences.end();
        return LoadResult::Missing;
    }
    if (stored_length != expected_length)
    {
        preferences.end();
        return LoadResult::Invalid;
    }

    uint8_t *record = new (std::nothrow) uint8_t[expected_length]();
    if (record == nullptr)
    {
        preferences.end();
        return LoadResult::IoError;
    }

    size_t read_length = preferences.getBytes(
        kNvsKey, record, expected_length);
    preferences.end();

    LoadResult result = read_length == expected_length
                            ? decode_record(record, read_length, count, bindings)
                            : LoadResult::IoError;
    delete[] record;
    return result;
}

bool save_to_nvs(const GestureBinding *bindings, size_t count)
{
    size_t length = record_size(count);
    uint8_t *record = new (std::nothrow) uint8_t[length]();
    if (record == nullptr)
        return false;

    encode_record(bindings, count, record);

    Preferences preferences;
    if (!preferences.begin(kNvsNamespace, false))
    {
        delete[] record;
        return false;
    }

    size_t written = preferences.putBytes(kNvsKey, record, length);
    preferences.end();
    delete[] record;
    return written == length;
}

void snapshot_bindings(GestureBinding *destination)
{
    if (s_binding_mutex != nullptr)
        xSemaphoreTake(s_binding_mutex, portMAX_DELAY);
    memcpy(destination, s_bindings, sizeof(GestureBinding) * s_binding_count);
    if (s_binding_mutex != nullptr)
        xSemaphoreGive(s_binding_mutex);
}

void replace_bindings(const GestureBinding *source)
{
    if (s_binding_mutex != nullptr)
        xSemaphoreTake(s_binding_mutex, portMAX_DELAY);
    memcpy(s_bindings, source, sizeof(GestureBinding) * s_binding_count);
    if (s_binding_mutex != nullptr)
        xSemaphoreGive(s_binding_mutex);
}

void print_map_usage()
{
    DualSerial.println("Gesture mapping:");
    DualSerial.println("  map list");
    DualSerial.println("  map bind <gesture> <app> [parameter]");
    DualSerial.println("  map clear <gesture>");
    DualSerial.println("  map reset");
    DualSerial.println("apps: espnow, lumos, pov, ble_keyboard, ble_mouse, ble_knob");
}

void print_map_list()
{
    if (s_binding_count == 0 || s_bindings == nullptr)
    {
        DualSerial.println("Gesture mappings are unavailable");
        return;
    }

    BindingBuffer bindings(s_binding_count);
    if (!bindings)
    {
        DualSerial.println("Error: not enough memory to list gesture mappings");
        return;
    }
    snapshot_bindings(bindings.data());

    DualSerial.println("Gesture bindings:");
    for (size_t i = 0; i < bindings.count(); ++i)
    {
        const GestureBinding &binding = bindings.data()[i];
        if (binding.app[0] == '\0')
        {
            DualSerial.printf("  [%u] <unbound>\n", (unsigned)i);
        }
        else if (binding.parameter[0] == '\0')
        {
            DualSerial.printf("  [%u] %s\n", (unsigned)i, binding.app);
        }
        else
        {
            DualSerial.printf("  [%u] %s \"%s\"\n",
                              (unsigned)i, binding.app, binding.parameter);
        }
    }
}

bool remaining_is_blank(const char *text)
{
    return is_blank(text);
}

bool consume_word(const char *&cursor, char *word, size_t capacity)
{
    while (*cursor != '\0' && isspace((unsigned char)*cursor))
        ++cursor;
    if (*cursor == '\0')
        return false;

    const char *start = cursor;
    while (*cursor != '\0' && !isspace((unsigned char)*cursor))
        ++cursor;

    size_t length = (size_t)(cursor - start);
    if (length == 0 || length >= capacity)
        return false;

    memcpy(word, start, length);
    word[length] = '\0';
    return true;
}

bool parse_gesture_id(const char *text, size_t count, uint8_t &gesture)
{
    if (text == nullptr || text[0] == '\0' || count == 0)
        return false;

    uint16_t value = 0;
    for (const char *p = text; *p != '\0'; ++p)
    {
        if (!isdigit((unsigned char)*p))
            return false;
        value = (uint16_t)(value * 10 + (uint16_t)(*p - '0'));
        if (value >= count)
            return false;
    }

    gesture = (uint8_t)value;
    return true;
}

bool commit_candidate(const GestureBinding *candidate)
{
    if (!save_to_nvs(candidate, s_binding_count))
    {
        DualSerial.println("Error: failed to save gesture mapping");
        return false;
    }
    replace_bindings(candidate);
    return true;
}

bool bindings_are_available()
{
    if (s_binding_count > 0 && s_bindings != nullptr)
        return true;

    DualSerial.println("Error: gesture mappings are unavailable");
    return false;
}

void handle_map_bind(const char *cursor)
{
    if (!bindings_are_available())
        return;

    char gesture_text[8] = {};
    char app_text[GESTURE_APP_NAME_SIZE] = {};
    uint8_t gesture = 0;

    if (!consume_word(cursor, gesture_text, sizeof(gesture_text)) ||
        !parse_gesture_id(gesture_text, s_binding_count, gesture))
    {
        DualSerial.printf("Error: gesture ID must be 0~%u\n",
                          (unsigned)(s_binding_count - 1));
        return;
    }

    if (!consume_word(cursor, app_text, sizeof(app_text)))
    {
        DualSerial.println("Error: map bind requires an app name");
        print_map_usage();
        return;
    }

    const AppHandler *handler = find_app_handler(app_text);
    if (handler == nullptr)
    {
        DualSerial.printf("Error: unknown app '%s'\n", app_text);
        print_map_usage();
        return;
    }

    const char *payload = "";
    if (*cursor != '\0')
    {
        if (!isspace((unsigned char)*cursor))
        {
            DualSerial.println("Error: invalid separator before parameter");
            return;
        }

        // Consume exactly one command separator. Any additional leading space
        // remains part of the parameter, which allows a leading space key.
        ++cursor;
        payload = cursor;
    }

    size_t payload_length = strlen(payload);
    while (payload_length > 0 &&
           (payload[payload_length - 1] == '\r' ||
            payload[payload_length - 1] == '\n'))
    {
        --payload_length;
    }

    if (payload_length >= GESTURE_PARAMETER_SIZE)
    {
        DualSerial.printf("Error: parameter exceeds %u bytes\n",
                          (unsigned)(GESTURE_PARAMETER_SIZE - 1));
        return;
    }

    char parameter[GESTURE_PARAMETER_SIZE] = {};
    memcpy(parameter, payload, payload_length);

    if (handler->blank_parameter_only && is_blank(parameter))
        parameter[0] = '\0';

    if (!handler->validate(parameter))
    {
        DualSerial.printf("Error: invalid parameter for app '%s'\n",
                          handler->name);
        return;
    }

    BindingBuffer candidate(s_binding_count);
    if (!candidate)
    {
        DualSerial.println("Error: not enough memory to update mapping");
        return;
    }

    snapshot_bindings(candidate.data());
    memset(&candidate.data()[gesture], 0,
           sizeof(candidate.data()[gesture]));
    copy_text(candidate.data()[gesture].app,
              sizeof(candidate.data()[gesture].app), handler->name);
    copy_text(candidate.data()[gesture].parameter,
              sizeof(candidate.data()[gesture].parameter), parameter);

    if (!commit_candidate(candidate.data()))
        return;

    if (parameter[0] == '\0')
        DualSerial.printf("Saved: gesture %u -> %s\n", gesture, handler->name);
    else
        DualSerial.printf("Saved: gesture %u -> %s \"%s\"\n",
                          gesture, handler->name, parameter);
}

void handle_map_clear(const char *cursor)
{
    if (!bindings_are_available())
        return;

    char gesture_text[8] = {};
    uint8_t gesture = 0;
    if (!consume_word(cursor, gesture_text, sizeof(gesture_text)) ||
        !parse_gesture_id(gesture_text, s_binding_count, gesture) ||
        !remaining_is_blank(cursor))
    {
        DualSerial.println("Usage: map clear <gesture>");
        return;
    }

    BindingBuffer candidate(s_binding_count);
    if (!candidate)
    {
        DualSerial.println("Error: not enough memory to update mapping");
        return;
    }

    snapshot_bindings(candidate.data());
    memset(&candidate.data()[gesture], 0,
           sizeof(candidate.data()[gesture]));

    if (commit_candidate(candidate.data()))
        DualSerial.printf("Cleared: gesture %u\n", gesture);
}

void handle_map_reset(const char *cursor)
{
    if (!bindings_are_available())
        return;

    if (!remaining_is_blank(cursor))
    {
        DualSerial.println("Usage: map reset");
        return;
    }

    BindingBuffer candidate(s_binding_count);
    if (!candidate)
    {
        DualSerial.println("Error: not enough memory to update mapping");
        return;
    }

    load_defaults(candidate.data(), candidate.count());
    if (commit_candidate(candidate.data()))
        DualSerial.println("Gesture mappings reset to defaults");
}

} // namespace

void APP_binding_init(void)
{
    int model_class_count = gesture_inference_num_classes();
    if (model_class_count <= 0 || model_class_count > 255)
    {
        DualSerial.printf("[Map] Invalid model class count: %d\n",
                          model_class_count);
        return;
    }
    s_binding_count = (size_t)model_class_count;

    s_bindings = new (std::nothrow) GestureBinding[s_binding_count]();
    if (s_bindings == nullptr)
    {
        s_binding_count = 0;
        DualSerial.println("[Map] Failed to allocate gesture mappings");
        return;
    }

    if (s_binding_mutex == nullptr)
        s_binding_mutex = xSemaphoreCreateMutex();
    if (s_update_mutex == nullptr)
        s_update_mutex = xSemaphoreCreateMutex();

    BindingBuffer loaded(s_binding_count);
    LoadResult result = loaded ? load_from_nvs(loaded.data(), s_binding_count)
                               : LoadResult::IoError;

    if (result == LoadResult::Valid)
    {
        replace_bindings(loaded.data());
        DualSerial.printf("[Map] Loaded gesture mappings (%u classes)\n",
                          (unsigned)s_binding_count);
        return;
    }

    load_defaults(s_bindings, s_binding_count);

    if (result == LoadResult::Missing)
    {
        if (save_to_nvs(s_bindings, s_binding_count))
            DualSerial.printf("[Map] Initialized defaults (%u classes)\n",
                              (unsigned)s_binding_count);
        else
            DualSerial.println("[Map] Warning: failed to save default mappings");
    }
    else
    {
        DualSerial.println("[Map] Warning: mapping record invalid, using defaults");
    }
}

void APP_binding_execute(int32_t gesture)
{
    if (gesture < 0 || (size_t)gesture >= s_binding_count ||
        s_bindings == nullptr)
    {
        DualSerial.printf("[Map] Ignoring gesture %ld\n", (long)gesture);
        return;
    }

    GestureBinding binding;
    if (s_binding_mutex != nullptr)
        xSemaphoreTake(s_binding_mutex, portMAX_DELAY);
    copy_binding(binding, s_bindings[gesture]);
    if (s_binding_mutex != nullptr)
        xSemaphoreGive(s_binding_mutex);

    if (binding.app[0] == '\0')
    {
        DualSerial.printf("[Map] Gesture %ld is unbound\n", (long)gesture);
        return;
    }

    const AppHandler *handler = find_app_handler(binding.app);
    if (handler == nullptr)
    {
        DualSerial.printf("[Map] App '%s' is not supported by this firmware\n",
                          binding.app);
        return;
    }

    if (!handler->validate(binding.parameter))
    {
        DualSerial.printf("[Map] Stored binding for gesture %ld is invalid\n",
                          (long)gesture);
        return;
    }

    handler->execute(binding.parameter);
}

void APP_binding_console(const char *args)
{
    const char *cursor = args == nullptr ? "" : args;

    char subcommand[16] = {};
    if (!consume_word(cursor, subcommand, sizeof(subcommand)))
    {
        print_map_usage();
        return;
    }

    if (strcmp(subcommand, "list") == 0)
    {
        if (!remaining_is_blank(cursor))
        {
            DualSerial.println("Usage: map list");
            return;
        }
        print_map_list();
    }
    else if (strcmp(subcommand, "bind") == 0)
    {
        if (s_update_mutex != nullptr)
            xSemaphoreTake(s_update_mutex, portMAX_DELAY);
        handle_map_bind(cursor);
        if (s_update_mutex != nullptr)
            xSemaphoreGive(s_update_mutex);
    }
    else if (strcmp(subcommand, "clear") == 0)
    {
        if (s_update_mutex != nullptr)
            xSemaphoreTake(s_update_mutex, portMAX_DELAY);
        handle_map_clear(cursor);
        if (s_update_mutex != nullptr)
            xSemaphoreGive(s_update_mutex);
    }
    else if (strcmp(subcommand, "reset") == 0)
    {
        if (s_update_mutex != nullptr)
            xSemaphoreTake(s_update_mutex, portMAX_DELAY);
        handle_map_reset(cursor);
        if (s_update_mutex != nullptr)
            xSemaphoreGive(s_update_mutex);
    }
    else
    {
        DualSerial.printf("Unknown map subcommand: %s\n", subcommand);
        print_map_usage();
    }
}
