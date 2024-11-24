typedef struct {
    uint32_t report_id;    // Report identifier
    union {
        struct {
            uint8_t modifier;     // Modifier keys
            uint8_t reserved;     // Reserved byte
            uint8_t keycode[15];  // Keycode
        } keyboard_full_key_report;  // Keyboard full key report
        struct {
            uint8_t modifier;   // Modifier keys
            uint8_t reserved;   // Reserved byte
            uint8_t keycode[6]; // Keycodes
        } keyboard_report;  // Keyboard report
        struct {
            uint16_t keycode;    // Keycode
        } consumer_report;
    };
} hid_nkey_report_t;