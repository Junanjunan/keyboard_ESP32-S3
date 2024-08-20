JTAG debugging

    ESP32-S3-devkitc

      ESP32-S3-WROOM-1

        N8R2
          It comes up in usbipd list for USB JTAG/serial debug unit by default When I connect usb otg connector

        N16R8
          It does not come up in usbipd list for USB JTAG by default
          It comes up only in boot mode.
            - When I press RST (reset) button, for a while in boot mode,
              I can see USB JTAG/serial debug unit in usbipd list
            - So, when I press RST and BOOT button at the same time,
              (Exactly, not at the same time, keep pressing BOOT, then press RST)
              it goes 'waiting for download' mode like this:
                ESP-ROM:esp32s3-20210327
                Build:Mar 27 2021
                rst:0x1 (POWERON),boot:0x0 (DOWNLOAD(USB/UART0))
                waiting for download
          -> I have to find why this differences exist and
            prepare way to debug with real keyboard by usb otg
            -> There is one more strange thing.
              After making JTAG debugging unit mode and flash with the usb otg connector,
              it is accepted as JTAG debugging unit continuously.
              Why...?

