#include <stdint.h>

#ifndef __ICEONE_H__
#define __ICEONE_H__

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

enum Ice9Error {
    OK,
    Error,
    UnableToOpenBitFile,
    DownloadOfBitFileFailed,
    USBDeviceNotFound,
    UnableToOpenDevice,
    UnableToClaimDevice,
    ResetFailed,
    SetBaudrateFailed,
    GetProductDescriptionFailed,
    GetSerialNumberFailed,
    GetDeviceListFromLibUSBFailed,
    GetDeviceDescriptorFromLibUSBFailed,
    FTDIResetFailed,
    USBDeviceUnavailable,
    UnknownInterface,
    DeviceAlreadyOpen,
    CannotEnableBitBangMode,
    LatencyValueOutOfRange,
    UnableToSetLatencyTimer,
    USBReleaseFailed,
    FTDIContextInvalid,
    LibUSBIOError,
    LibUSBInvalidParameter,
    LibUSBAccessDenied,
    LibUSBNoDeviceFound,
    LibUSBEntityNotFound,
    LibUSBResourceBusy,
    LibUSBTimeout,
    LibUSBOverflow,
    LibUSBPipeError,
    LibUSBInterrupted,
    LibUSBInsufficientMemory,
    LibUSBOperationNotSupported,
    LibUSBOtherError,
    PartialWrite,
    NoDataAvailable,
    StreamReadComplete,
    PingMismatch,
};

/*
 * Flash the given bitfile to the FPGA.  If NULL, then devstr will take on
 * the default parameters for an IceOne Programming port.
 */
EXTERN_C enum Ice9Error ice9_flash_fpga(const char *filename);

EXTERN_C enum Ice9Error ice9_flash_fpga_mem(void *buf, int bufsize);

EXTERN_C struct ice9_handle * ice9_new();

EXTERN_C void ice9_free(struct ice9_handle *hnd);

EXTERN_C void ice9_set_info_logger(void (*log_info)(const char *format, ...));

EXTERN_C void ice9_set_error_logger(void (*log_error)(const char *file, int line, const char *format, ...));

EXTERN_C enum Ice9Error ice9_open(struct ice9_handle *hnd);

EXTERN_C enum Ice9Error ice9_usb_reset(struct ice9_handle *hnd);

EXTERN_C const char* ice9_error_string(enum Ice9Error code);

EXTERN_C enum Ice9Error ice9_fifo_mode(struct ice9_handle *hnd);

EXTERN_C enum Ice9Error ice9_close(struct ice9_handle *hnd);

EXTERN_C enum Ice9Error ice9_write(struct ice9_handle *hnd, const uint8_t *data, int num_bytes);

EXTERN_C enum Ice9Error ice9_read(struct ice9_handle *hnd, uint8_t *data, int num_bytes);

EXTERN_C enum Ice9Error ice9_stream_read(struct ice9_handle *hnd, uint8_t *data, int num_bytes);

EXTERN_C enum Ice9Error ice9_write_words(struct ice9_handle *hnd, uint16_t *data, uint16_t len);

EXTERN_C enum Ice9Error ice9_write_word(struct ice9_handle *hnd, uint16_t data);

EXTERN_C enum Ice9Error ice9_read_words(struct ice9_handle *hnd, uint16_t *data, uint16_t len);

EXTERN_C enum Ice9Error ice9_write_data_to_address(struct ice9_handle *hnd, uint8_t address, uint16_t *data, uint16_t len);

EXTERN_C enum Ice9Error ice9_write_word_to_address(struct ice9_handle *hnd, uint8_t address, uint16_t value);

EXTERN_C enum Ice9Error ice9_write_int_to_address(struct ice9_handle *hnd, uint8_t address, uint32_t value);

EXTERN_C enum Ice9Error ice9_read_data_from_address(struct ice9_handle *hnd, uint8_t address, uint16_t *data, uint16_t len);

EXTERN_C enum Ice9Error ice9_read_int_from_address(struct ice9_handle *hnd, uint8_t address, uint32_t *data);

EXTERN_C enum Ice9Error ice9_send_ping(struct ice9_handle *hnd, uint8_t pingid);

EXTERN_C enum Ice9Error ice9_ping_bridge(struct ice9_handle *hnd, uint8_t pingid);

EXTERN_C enum Ice9Error ice9_enable_streaming(struct ice9_handle *hnd, uint8_t address);

EXTERN_C enum Ice9Error ice9_disable_streaming(struct ice9_handle *hnd);

#endif
