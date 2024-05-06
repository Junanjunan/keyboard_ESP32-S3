void setup_mode_gpio();


typedef enum {
    MODE_USB,
    MODE_BLE,
    MODE_WIRELESS
} connection_mode_t;

connection_mode_t detect_current_mode();
