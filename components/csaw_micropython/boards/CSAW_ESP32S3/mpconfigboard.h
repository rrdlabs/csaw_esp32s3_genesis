// CsAW ESP32-S3 board configuration for the embedded MicroPython VM.
//
// MicroPython's ESP32 port uses MICROPY_ESP_IDF_ENTRY as the name of the
// function it exports (ports/esp32/main.c, v1.29.0).  Overriding it keeps
// app_main owned by CsAW: the interpreter is only started when explicitly
// demanded through csaw_python_vm_init().
#define CSAW_MICROPYTHON_ENTRY csaw_micropython_entry
#define MICROPY_ESP_IDF_ENTRY CSAW_MICROPYTHON_ENTRY

#define MICROPY_HW_BOARD_NAME "CsAW ESP32-S3 Genesis"
#define MICROPY_HW_MCU_NAME   "ESP32-S3"

// IDF 6.1 removed the legacy mbedtls/AES/SHA headers, the Nimble component and
// the old LEDC/PCNT drivers used by these optional MicroPython modules. CsAW's
// on-demand VM needs only the core interpreter, VFS, SD and the CsAW sandbox.
#define MICROPY_PY_SSL                      (0)
#define MICROPY_SSL_MBEDTLS                 (0)
#define MICROPY_PY_CRYPTOLIB                (0)
#define MICROPY_PY_HASHLIB                  (0)
#define MICROPY_PY_HASHLIB_SHA256           (0)
#define MICROPY_PY_HASHLIB_MD5              (0)
#define MICROPY_PY_HASHLIB_SHA1             (0)
#define MICROPY_PY_BLUETOOTH                (0)
#define MICROPY_PY_MACHINE_PWM              (0)
#define MICROPY_PY_ESP32_PCNT               (0)
// IDF 6.1 removed SOC_I2S_NUM; the I2S driver is owned by the CsAW audio HAL.
#define MICROPY_PY_MACHINE_I2S              (0)
// IDF 6.1 renamed ADC_ATTEN_DB_11 and moved driver/i2c_slave.h.  ADC and I2C
// target are owned by the CsAW native HAL and unused by the on-demand VM.
#define MICROPY_PY_MACHINE_ADC              (0)
#define MICROPY_PY_MACHINE_ADC_BLOCK        (0)
#define MICROPY_PY_MACHINE_I2C_TARGET       (0)
// machine_uart.c depends on the removed machine_timer.c helpers; CsAW owns the
// serial console and the VM is driven only through the on-demand C bridge.
#define MICROPY_PY_MACHINE_UART             (0)
// machine_sdcard.c was removed (IDF 6.1 moved driver/sdmmc_host.h); the CsAW
// native HAL owns the TF card and VFS mount.
#define MICROPY_PY_MACHINE_SDCARD           (0)

// Filesystem bridge: the CsAW native HAL mounts the SD card at /sd through IDF
// VFS before the VM is created.  Expose that mount to Python with VfsPosix so
// MicroPython never needs its own FatFs (which collides with IDF's fatfs) and
// never mounts flash.  Finalisers are required by the POSIX VFS file wrapper.
// MICROPY_VFS_FAT is set to 0 by esp32_common.cmake (IDF fatfs owns FAT).
#define MICROPY_VFS_POSIX                   (1)
#define MICROPY_VFS_POSIX_WRITABLE          (1)
#define MICROPY_ENABLE_FINALISER            (1)
// IDF 6.1's new I2C master driver needs API updates; the legacy driver/i2c.h
// is still shipped, so keep MicroPython on that path.  CsAW HAL owns I2C.
#define MICROPY_HW_ESP_NEW_I2C_DRIVER       (0)
// IDF 6.1 renamed esp_interface_t and the WPA3 auth constants.  Wi-Fi is owned
// by the CsAW native HAL, so the on-demand VM needs no MicroPython network.
#define MICROPY_PY_NETWORK                  (0)
#define MICROPY_PY_ESPNOW                   (0)

// IDF 6.1 compatibility: the old umbrella "usb" component no longer exists and
// TinyUSB is disabled for this board.  Keep MicroPython's USB device and USB
// Serial/JTAG paths off; the CsAW firmware owns the USB Serial/JTAG console.
#define MICROPY_HW_ENABLE_USBDEV           (0)
#define MICROPY_HW_ESP_USB_SERIAL_JTAG     (0)

// The MicroPython UART REPL pulls in soc/uart_periph.h and the legacy UART HAL,
// both removed in IDF 6.1.  CsAW never exposes a MicroPython serial REPL: the
// interpreter is driven only through the on-demand C bridge (python_exec /
// python_eval), so there is no reason to compile this path at all.
#define MICROPY_HW_ENABLE_UART_REPL        (0)

// 1-bit SDMMC TF card wiring used by the CsAW HAL (CLK=2, CMD=1, D0=3).
#define MICROPY_HW_SDMMC_DEFAULT_SLOT  (1)
#define MICROPY_HW_SDMMC_DEFAULT_WIDTH (1)
#define MICROPY_HW_SDMMC_CLK           (2)
#define MICROPY_HW_SDMMC_CMD           (1)
#define MICROPY_HW_SDMMC_D0            (3)
