#include "ext_usb_disk_svc.h"

#include "esp_partition.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "ff.h"
#include "sdkconfig.h"
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>

/*
 * We warn if a secondary serial console is enabled. A secondasm_wl_handlery serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

/* TinyUSB descriptors
 ********************************************************************* */
#define EPNUM_MSC 1
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

enum {
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN = 0x80,

    EDPT_MSC_OUT = 0x01,
    EDPT_MSC_IN = 0x81,
};

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

static tusb_desc_device_t descriptor_config = {
    .bLength = sizeof(descriptor_config),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, // This is Espressif VID. This needs to be changed according to Users / Customers
    .idProduct = 0x4002,
    .bcdDevice = 0x100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01};

static uint8_t const msc_fs_configuration_desc[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64),
};

#if (TUD_OPT_HIGH_SPEED)
static const tusb_desc_device_qualifier_t device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0};

static uint8_t const msc_hs_configuration_desc[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 512),
};
#endif // TUD_OPT_HIGH_SPEED

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "TinyUSB",                  // 1: Manufacturer
    "TinyUSB Device",           // 2: Product
    "123456",                   // 3: Serials
    "Example MSC",              // 4. MSC
};
/*********************************************************************** TinyUSB descriptors*/

#define PROMPT_STR CONFIG_IDF_TARGET

// mount the partition and show all the files in CONFIG_FILE_BASE_PATH
static void _mount(void) {
    DUD_LOGI("Mount storage...");

    ESP_ERROR_CHECK(tinyusb_msc_storage_mount(CONFIG_FILE_BASE_PATH));

    // 指定卷标名称
    FRESULT ret = f_setlabel("DOIT_EYE");
    if (ret != FR_OK) {
        DUD_LOGI("USB setlabel Failed code:%d", ret);
    }

    // List all the files in this directory
    DUD_LOGI("ls command output:");
    struct dirent *d;
    DIR *dh = opendir(CONFIG_FILE_BASE_PATH);
    if (!dh) {
        if (errno == ENOENT) {
            // If the directory is not found
            DUD_LOGE("Directory doesn't exist %s", CONFIG_FILE_BASE_PATH);
        } else {
            // If the directory is not readable then throw error and exit
            DUD_LOGE("Unable to read directory %s", CONFIG_FILE_BASE_PATH);
        }
        return;
    }
    // While the next entry is not readable we will print directory files
    while ((d = readdir(dh)) != NULL) {
        printf("%s\n", d->d_name);
    }
    return;
}

// callback that is delivered when storage is mounted/unmounted by application.
static void storage_mount_changed_cb(tinyusb_msc_event_t *event) {
    DUD_LOGI("Storage mounted to application: %s", event->mount_changed_data.is_mounted ? "Yes" : "No");
}

static esp_err_t storage_init_spiflash(wl_handle_t *sm_wl_handle) {
    DUD_LOGI("Initializing wear levelling");

    const esp_partition_t *data_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, NULL);
    if (data_partition == NULL) {
        DUD_LOGE("Failed to find FATFS partition. Check the partition table.");
        return ESP_ERR_NOT_FOUND;
    }

    return wl_mount(data_partition, sm_wl_handle);
}

/**
 * @brief 设置无线句柄为无效值
 *
 * 该函数将全局无线句柄变量 sm_wl_handle 设置为无效值 WL_INVALID_HANDLE。
 * 通常用于初始化或重置无线句柄状态。
 *
 * @param void 无参数
 * @return void 无返回值
 */
static void set_wl_handle_invalid() {
    s_wl_handle = WL_INVALID_HANDLE;
}

/**
 * @brief 检查wl句柄是否有效
 *
 * 该函数用于检查全局变量sm_wl_handle是否不等于WL_INVALID_HANDLE，
 * 以判断当前无线句柄是否有效。
 *
 * @return bool 返回true表示句柄已被使能，返回false表示未被使能
 */
static bool is_wl_handle_valid() {
    return (s_wl_handle != WL_INVALID_HANDLE);
}

void dot_usb_disk_init(void) {
    if (is_wl_handle_valid()) {
        DUD_LOGI("Storage already initialized");
        return;
    }
    DUD_LOGI("Initializing storage...");

    ESP_ERROR_CHECK(storage_init_spiflash(&s_wl_handle));

    const tinyusb_msc_spiflash_config_t config_spi = {
        .wl_handle = s_wl_handle,
        .callback_mount_changed = storage_mount_changed_cb, /* First way to register the callback. This is while initializing the storage. */
        .mount_config.max_files = 5,
    };
    ESP_ERROR_CHECK(tinyusb_msc_storage_init_spiflash(&config_spi));
    ESP_ERROR_CHECK(tinyusb_msc_register_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED, storage_mount_changed_cb)); /* Other way to register the callback i.e. registering using separate API. If the callback had been already registered, it will be overwritten. */

    // mounted in the app by default
    _mount();

    DUD_LOGI("USB MSC initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &descriptor_config,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = msc_fs_configuration_desc,
        .hs_configuration_descriptor = msc_hs_configuration_desc,
        .qualifier_descriptor = &device_qualifier,
#else
        .configuration_descriptor = msc_fs_configuration_desc,
#endif // TUD_OPT_HIGH_SPEED
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    DUD_LOGI("USB MSC initialization DONE");
}

void dot_usb_disk_deinit(void) {
    if (!is_wl_handle_valid()) {
        DUD_LOGI("Storage NO INIT");
        return;
    }
    if (tinyusb_msc_storage_in_use_by_usb_host()) {
        DUD_LOGI("Storage is already exposed to USB host");
        // 尝试卸载存储
        ESP_ERROR_CHECK(tinyusb_msc_storage_unmount());
        DUD_LOGI("Storage unmounted");

        // 卸载 TinyUSB 驱动
        ESP_ERROR_CHECK(tinyusb_driver_uninstall());
        DUD_LOGI("TinyUSB driver uninstalled");
        tinyusb_msc_storage_deinit();
        wl_unmount(s_wl_handle);
        set_wl_handle_invalid();
    } else {
        DUD_LOGI("Storage is not exposed to USB host, no need to unmount");
    }
}