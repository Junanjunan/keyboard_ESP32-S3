typedef enum {
    MODE_USB,
    MODE_BLE,
    MODE_WIRELESS
} connection_mode_t;

void setup_mode_gpio(connection_mode_t *mode);