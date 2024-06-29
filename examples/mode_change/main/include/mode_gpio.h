typedef enum {
    MODE_USB = 1,
    MODE_BLE = 2,
    MODE_WIRELESS = 3
} connection_mode_t;

void setup_mode_gpio(connection_mode_t *mode);