/*
    设备身份配置的持久化与命令解析。
    三个字段各自存放在 NVS 的独立键中：
      - 每条命令只改一个字段，不存在跨字段事务
      - NVS 对每个键自身有 CRC 和原子提交，无需再套自定义记录头
    启动时一次性读入 RAM 缓存，之后的读取不访问 NVS、不分配堆内存，
    避免 AP 低内存自愈任务在堆紧张时拿到不完整的配置。
*/
#include "DeviceConfig.h"
#include "Config.h"
#include "Service/DualPrint.h"

#include <Preferences.h>
#include <ctype.h>
#include <string.h>

namespace
{

    constexpr char kNamespace[] = "wand_cfg";
    constexpr char kKeySsid[] = "ap_ssid";
    constexpr char kKeyPass[] = "ap_pass";
    constexpr char kKeyBle[] = "ble_name";

    constexpr size_t kSsidMax = 32;
    constexpr size_t kPassMin = 8;
    constexpr size_t kPassMax = 63;
    constexpr size_t kBleNameMax = 29;

    portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

    char s_ssid[kSsidMax + 1];
    char s_pass[kPassMax + 1];
    char s_ble_name[kBleNameMax + 1];

    void copy_text(char *destination, size_t capacity, const char *source)
    {
        if (destination == nullptr || capacity == 0)
            return;

        if (source == nullptr)
            source = "";

        size_t length = strlen(source);
        if (length >= capacity)
            length = capacity - 1;

        memcpy(destination, source, length);
        destination[length] = '\0';
    }

    bool valid_ssid(const char *value)
    {
        size_t length = value == nullptr ? 0 : strlen(value);
        return length >= 1 && length <= kSsidMax;
    }

    bool valid_pass(const char *value)
    {
        size_t length = value == nullptr ? 0 : strlen(value);
        return length >= kPassMin && length <= kPassMax;
    }

    bool valid_ble_name(const char *value)
    {
        size_t length = value == nullptr ? 0 : strlen(value);
        return length >= 1 && length <= kBleNameMax;
    }

    // 键缺失或内容非法时使用默认值；每个字段独立回退，互不影响
    bool load_string(Preferences &preferences, const char *key,
                     const char *fallback, char *destination,
                     size_t destination_size, bool (*is_valid)(const char *))
    {
        String value = preferences.getString(key, fallback);
        if (!is_valid(value.c_str()))
        {
            DualSerial.printf("[Config] Invalid '%s' in NVS, using default\n", key);
            copy_text(destination, destination_size, fallback);
            return false;
        }

        copy_text(destination, destination_size, value.c_str());
        return true;
    }

    bool save_value(const char *key, const char *value, char *cache,
                    size_t cache_size)
    {
        const size_t length = strlen(value);

        Preferences preferences;
        if (!preferences.begin(kNamespace, false))
        {
            DualSerial.println("Error: failed to open NVS");
            return false;
        }

        const size_t written = preferences.putString(key, value);
        preferences.end();

        if (written != length)
        {
            DualSerial.println("Error: failed to save config");
            return false;
        }

        portENTER_CRITICAL(&s_lock);
        copy_text(cache, cache_size, value);
        portEXIT_CRITICAL(&s_lock);
        return true;
    }

    void print_config_usage()
    {
        DualSerial.println("Device config:");
        DualSerial.println("  config list");
        DualSerial.println("  config ap <ssid>");
        DualSerial.println("  config pass <password>");
        DualSerial.println("  config ble <name>");
        DualSerial.println("  config reset");
        DualSerial.println("Note: value is the rest of the line (spaces allowed).");
    }

    void print_config_values()
    {
        char ssid[kSsidMax + 1];
        char pass[kPassMax + 1];
        char ble_name[kBleNameMax + 1];

        device_config_get_ap(ssid, sizeof(ssid), pass, sizeof(pass));
        device_config_get_ble_name(ble_name, sizeof(ble_name));

        DualSerial.println("Device config:");
        DualSerial.printf("  AP SSID : \"%s\"\n", ssid);
        DualSerial.printf("  AP PASS : \"%s\"\n", pass);
        DualSerial.printf("  BLE name: \"%s\"\n", ble_name);
        DualSerial.println("Note: changes take effect after reboot.");
    }

    const char *skip_spaces(const char *cursor)
    {
        while (*cursor != '\0' && isspace((unsigned char)*cursor))
            ++cursor;
        return cursor;
    }

    bool consume_word(const char *&cursor, char *word, size_t capacity)
    {
        cursor = skip_spaces(cursor);
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

    // 子命令与值之间保留一个空白作为分隔，分隔符之后的整行原样作为值。
    // 因此值内部可以包含空格，不需要引号或转义。
    const char *rest_of_line(const char *cursor)
    {
        if (*cursor == '\0' || !isspace((unsigned char)*cursor))
            return nullptr;
        return cursor + 1;
    }

} // namespace

void device_config_init(void)
{
    char loaded_ssid[kSsidMax + 1];
    char loaded_pass[kPassMax + 1];
    char loaded_ble_name[kBleNameMax + 1];

    copy_text(loaded_ssid, sizeof(loaded_ssid), AP_SSID);
    copy_text(loaded_pass, sizeof(loaded_pass), AP_PASS);
    copy_text(loaded_ble_name, sizeof(loaded_ble_name), BLE_DEVICE_NAME);

    Preferences preferences;
    if (preferences.begin(kNamespace, true))
    {
        load_string(preferences, kKeySsid, AP_SSID, loaded_ssid,
                    sizeof(loaded_ssid), valid_ssid);
        load_string(preferences, kKeyPass, AP_PASS, loaded_pass,
                    sizeof(loaded_pass), valid_pass);
        load_string(preferences, kKeyBle, BLE_DEVICE_NAME, loaded_ble_name,
                    sizeof(loaded_ble_name), valid_ble_name);
        preferences.end();
    }
    else
    {
        // 首次启动时命名空间尚不存在，只读打开会返回 NOT_FOUND
        DualSerial.println("[Config] No saved config, using defaults");
    }

    portENTER_CRITICAL(&s_lock);
    copy_text(s_ssid, sizeof(s_ssid), loaded_ssid);
    copy_text(s_pass, sizeof(s_pass), loaded_pass);
    copy_text(s_ble_name, sizeof(s_ble_name), loaded_ble_name);
    portEXIT_CRITICAL(&s_lock);

    DualSerial.printf("[Config] AP SSID: \"%s\", BLE name: \"%s\"\n",
                      loaded_ssid, loaded_ble_name);
}

void device_config_get_ap(char *ssid, size_t ssid_size,
                          char *pass, size_t pass_size)
{
    portENTER_CRITICAL(&s_lock);
    copy_text(ssid, ssid_size, s_ssid);
    copy_text(pass, pass_size, s_pass);
    portEXIT_CRITICAL(&s_lock);
}

void device_config_get_ble_name(char *name, size_t size)
{
    portENTER_CRITICAL(&s_lock);
    copy_text(name, size, s_ble_name);
    portEXIT_CRITICAL(&s_lock);
}

bool device_config_set_ap_ssid(const char *value)
{
    if (!valid_ssid(value))
    {
        DualSerial.printf("Error: AP SSID must be 1-%u bytes\n",
                          (unsigned)kSsidMax);
        return false;
    }

    if (!save_value(kKeySsid, value, s_ssid, sizeof(s_ssid)))
        return false;

    DualSerial.printf("Saved: AP SSID = \"%s\" (reboot to apply)\n", value);
    return true;
}

bool device_config_set_ap_pass(const char *value)
{
    if (!valid_pass(value))
    {
        DualSerial.printf("Error: AP password must be %u-%u bytes\n",
                          (unsigned)kPassMin, (unsigned)kPassMax);
        return false;
    }

    if (!save_value(kKeyPass, value, s_pass, sizeof(s_pass)))
        return false;

    DualSerial.println("Saved: AP password (reboot to apply)");
    return true;
}

bool device_config_set_ble_name(const char *value)
{
    if (!valid_ble_name(value))
    {
        DualSerial.printf("Error: BLE name must be 1-%u bytes\n",
                          (unsigned)kBleNameMax);
        return false;
    }

    if (!save_value(kKeyBle, value, s_ble_name, sizeof(s_ble_name)))
        return false;

    DualSerial.printf("Saved: BLE name = \"%s\" (reboot to apply)\n", value);
    return true;
}

bool device_config_reset(void)
{
    Preferences preferences;
    if (!preferences.begin(kNamespace, false))
    {
        DualSerial.println("Error: failed to open NVS");
        return false;
    }

    const bool cleared = preferences.clear();
    preferences.end();

    if (!cleared)
    {
        DualSerial.println("Error: failed to reset config");
        return false;
    }

    portENTER_CRITICAL(&s_lock);
    copy_text(s_ssid, sizeof(s_ssid), AP_SSID);
    copy_text(s_pass, sizeof(s_pass), AP_PASS);
    copy_text(s_ble_name, sizeof(s_ble_name), BLE_DEVICE_NAME);
    portEXIT_CRITICAL(&s_lock);

    DualSerial.println("Device config reset to defaults (reboot to apply)");
    return true;
}

void device_config_console(const char *args)
{
    const char *cursor = args == nullptr ? "" : args;
    char subcommand[16] = {};

    if (!consume_word(cursor, subcommand, sizeof(subcommand)))
    {
        print_config_usage();
        return;
    }

    if (strcmp(subcommand, "list") == 0)
    {
        if (*skip_spaces(cursor) != '\0')
        {
            DualSerial.println("Usage: config list");
            return;
        }
        print_config_values();
    }
    else if (strcmp(subcommand, "reset") == 0)
    {
        if (*skip_spaces(cursor) != '\0')
        {
            DualSerial.println("Usage: config reset");
            return;
        }
        device_config_reset();
    }
    else if (strcmp(subcommand, "ap") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config ap <ssid>");
            return;
        }
        device_config_set_ap_ssid(value);
    }
    else if (strcmp(subcommand, "pass") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config pass <password>");
            return;
        }
        device_config_set_ap_pass(value);
    }
    else if (strcmp(subcommand, "ble") == 0)
    {
        const char *value = rest_of_line(cursor);
        if (value == nullptr || *value == '\0')
        {
            DualSerial.println("Usage: config ble <name>");
            return;
        }
        device_config_set_ble_name(value);
    }
    else
    {
        DualSerial.printf("Unknown config subcommand: %s\n", subcommand);
        print_config_usage();
    }
}
