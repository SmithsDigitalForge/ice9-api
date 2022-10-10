#include "ice9.h"
#include "logger.h"
#include <time.h>
#include <libftdi1/ftdi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ICE9_VENDOR_ID 0x3524
#define ICE9_DATA_PRODUCT_ID 0x0002

#define MIN(a, b) ((a) < (b)) ? (a) : (b)

struct ice9_handle {
    struct ftdi_context *ftdi_ptr;
    int stream_bytes_to_read;
    int stream_bytes_read_so_far;
    uint8_t *stream_data_ptr;
    uint8_t *extra_data_buffer;
    uint8_t *extra_data_read_pointer;
    int extra_data_bytes;
};

#define BANK_SIZE (1024*1024)

static enum Ice9Error ecode;

#define lib_try(x) {ecode = (x); if (ecode != OK) {return ecode;}}

struct ice9_handle* ice9_new(void) {
    struct ice9_handle *p = (struct ice9_handle *)(malloc(sizeof(struct ice9_handle)));
    p->ftdi_ptr = ftdi_new();
    p->extra_data_buffer = (uint8_t*) malloc(BANK_SIZE);
    p->extra_data_read_pointer = p->extra_data_buffer;
    p->extra_data_bytes = 0;
    return p;
}

void ice9_free(struct ice9_handle *hnd) {
    ftdi_free(hnd->ftdi_ptr);
    free(hnd);
}

enum Ice9Error ice9_open(struct ice9_handle *hnd) {
    if (ftdi_read_data_set_chunksize(hnd->ftdi_ptr, 16384) != 0) {
        return Error;
    }
    switch (ftdi_set_interface(hnd->ftdi_ptr, INTERFACE_A)) {
        case 0: break;
        case -1: return UnknownInterface;
        case -2: return USBDeviceUnavailable;
        case -3: return DeviceAlreadyOpen;
        default:
            return Error;
    }
    LOG_INFO("newer ice9 open\n");
    switch (ftdi_usb_open(hnd->ftdi_ptr, ICE9_VENDOR_ID, ICE9_DATA_PRODUCT_ID)) {
        case 0: {usleep(1000); return OK;}
        case -3: return USBDeviceNotFound;
        case -4: return UnableToOpenDevice;
        case -5: return UnableToClaimDevice;
        case -6: return ResetFailed;
        case -7: return SetBaudrateFailed;
        case -8: return GetProductDescriptionFailed;
        case -9: return GetSerialNumberFailed;
        case -12: return GetDeviceListFromLibUSBFailed;
        case -13: return GetDeviceDescriptorFromLibUSBFailed;
        default:
            return Error;
    }
}

enum Ice9Error ice9_usb_reset(struct ice9_handle *hnd) {
    LOG_INFO("newer ice9 usb_reset\n");
    switch (ftdi_usb_reset(hnd->ftdi_ptr)) {
        case 0: {usleep(1000); return OK;}
        case -1: return FTDIResetFailed;
        case -2: return USBDeviceUnavailable;
        default:
            return Error;
    }
}

const char* ice9_error_string(enum Ice9Error code) {
    switch (code) {
        case OK: return "OK";
        case Error: return "Error";
        case UnableToOpenBitFile: return "Unable to open program bitfile";
        case DownloadOfBitFileFailed: return "Download of bitfile to device failed";
        case USBDeviceNotFound: return "USB device not found";
        case UnableToOpenDevice: return "Unable to open device";
        case UnableToClaimDevice: return "Unable to claim device";
        case ResetFailed: return "Reset failed";
        case SetBaudrateFailed: return "Set baudrate failed";
        case GetProductDescriptionFailed: return "Get product description failed";
        case GetSerialNumberFailed: return "Get serial number failed";
        case GetDeviceListFromLibUSBFailed: return "Get device list from libusb failed";
        case GetDeviceDescriptorFromLibUSBFailed: return "Get device descriptor from libusb failed";
        case FTDIResetFailed: return "FTDI Reset failed";
        case USBDeviceUnavailable: return "USB Device Unavailable";
        case UnknownInterface: return "Unknown Interface";
        case DeviceAlreadyOpen: return "Device already open";
        case CannotEnableBitBangMode: return "Cannot enable bitbang mode";
        case LatencyValueOutOfRange: return "Latency value out of range";
        case UnableToSetLatencyTimer: return "Unable to set latency timer";
        case USBReleaseFailed: return "USB release failed";
        case FTDIContextInvalid: return "Invalid handle (ftdi context or ice9 handle)";
        case LibUSBIOError: return "LibUSB IO Error";
        case LibUSBInvalidParameter: return "LibUSB Invalid Parameter";
        case LibUSBAccessDenied: return "LibUSB Access Denied";
        case LibUSBNoDeviceFound: return "LibUSB No Device Found";
        case LibUSBEntityNotFound: return "LibUSB Entity Not Found";
        case LibUSBResourceBusy: return "LibUSB Resource Busy";
        case LibUSBTimeout: return "LibUSB Timeout";
        case LibUSBOverflow: return "LibUSB Overflow";
        case LibUSBPipeError: return "LibUSB Pipe Error";
        case LibUSBInterrupted: return "LibUSB Interrupted";
        case LibUSBInsufficientMemory: return "LibUSB Insufficient Memory";
        case LibUSBOperationNotSupported: return "LibUSB Operation Not Supported";
        case LibUSBOtherError: return "LibUSB Other Error";
        case PartialWrite: return "Partial write";
        case NoDataAvailable: return "No Data available for read";
        case PingMismatch: return "Ping mismatch";
        default:
            LOG_INFO("unknown ice9 error code %d\n");
            return "Unknown";
    }
}

enum Ice9Error ice9_fifo_mode(struct ice9_handle *hnd) {
    LOG_INFO("newer ice9 fifo_Mode\n");
    switch (ftdi_set_bitmode(hnd->ftdi_ptr, 0xFF, 0x00)) {
        case 0: break;
        case -1: return CannotEnableBitBangMode;
        case -2: return USBDeviceUnavailable;
        default: return Error;
    }
    switch (ftdi_set_latency_timer(hnd->ftdi_ptr, 1)) {
        case 0: break;
        case -1: return LatencyValueOutOfRange;
        case -2: return UnableToSetLatencyTimer;
        case -3: return USBDeviceUnavailable;
        default: return Error;
    }
    switch (ftdi_set_bitmode(hnd->ftdi_ptr, 0xFF, 0x40)) {
        case 0: {usleep(1000); return OK;}
        case -1: return CannotEnableBitBangMode;
        case -2: return USBDeviceUnavailable;
        default: return Error;
    }
}

enum Ice9Error ice9_close(struct ice9_handle *hnd) {
    LOG_INFO("closing ftdi_usb\n");
    ftdi_tciflush(hnd->ftdi_ptr);
    ftdi_tcoflush(hnd->ftdi_ptr);
    ftdi_usb_reset(hnd->ftdi_ptr);
    switch (ftdi_usb_close(hnd->ftdi_ptr)) {
        case 0: return OK;
        case -1: return USBReleaseFailed;
        case -3: return FTDIContextInvalid;
        default: return Error;
    }
}

/*
 * Transfer up to `available` bytes from the provided buffer to the destination buffer.
 * Returns the number of bytes actually transferred
 */
int transfer_bytes(struct ice9_handle *hnd, const uint8_t *ptr, int available) {
    /* First copy out as many bytes as possible out of the backing store */
    int to_copy_from_store = MIN(hnd->stream_bytes_to_read, available);
    memcpy(hnd->stream_data_ptr + hnd->stream_bytes_read_so_far, ptr, to_copy_from_store);
    hnd->stream_bytes_read_so_far += to_copy_from_store;
    hnd->stream_bytes_to_read -= to_copy_from_store;
    return to_copy_from_store;
}

int bank_bytes(struct ice9_handle *hnd, const uint8_t *ptr, int to_bank) {
    // We are going to append to_bank bytes worth of data to the extra data buffer.
    // We must be careful as we may overflow the extra data buffer.
    int bank_used = hnd->extra_data_read_pointer - hnd->extra_data_buffer;
    if ((to_bank + bank_used) >= BANK_SIZE) {
        return -1;
    }
    memcpy(hnd->extra_data_read_pointer + hnd->extra_data_bytes, ptr, to_bank);
    hnd->extra_data_bytes += to_bank;
    // In this case, we return 1 for OK, since that gets propagated to the callback.
    return 1;
}

int read_callback(uint8_t *buffer, int length, FTDIProgressInfo *progress, void *userdata) {
    struct ice9_handle *hnd = (struct ice9_handle *)(userdata);
    // First transfer bytes from the backing store (if available)
    int copy_from_store = transfer_bytes(hnd, hnd->extra_data_read_pointer, hnd->extra_data_bytes);
    int extra_data_bytes_was_nonzero = hnd->extra_data_bytes != 0;
    hnd->extra_data_read_pointer += copy_from_store;
    hnd->extra_data_bytes -= copy_from_store;
    if (extra_data_bytes_was_nonzero && (hnd->extra_data_bytes == 0)) {
        // Reset the extra bytes buffer
        hnd->extra_data_read_pointer = hnd->extra_data_buffer;
        memset(hnd->extra_data_buffer, 0, BANK_SIZE);
    }
    // Next, transfer bytes from the provided buffer (if possible)
    int copy_from_new_buffer = transfer_bytes(hnd, buffer, length);
    buffer += copy_from_new_buffer;
    length -= copy_from_new_buffer;
    // Now we need to decide if we can take more data, or if we have to bank what we have.
    // The first question is to determine if we need more data, or if the output buffer
    // is statisfied.  If the output is _not_ complete at this point, we need more data.
    // We should return 0 so that we get more data.
    if (hnd->stream_bytes_to_read != 0) {
        return 0;
    }
    // OK, the output is finished.  That means we need to bank whatever bytes we still
    // have, and tell ftdi to stop calling us.
    if (length == 0) {
        // Nothing to bank, and we are all done.
        return 1;
    }
    // We have data to bank.
    return bank_bytes(hnd, buffer, length);
}

// Custom version of this is needed because we do not want to reset the
// chip between stream reads..
int ftdi_readstream_ice9(struct ftdi_context *ftdi,
                     FTDIStreamCallback *callback, void *userdata,
                     int packetsPerTransfer, int numTransfers);


enum Ice9Error ice9_stream_read(struct ice9_handle *hnd, uint8_t *data, int num_bytes) {
    hnd->stream_bytes_read_so_far = 0;
    hnd->stream_bytes_to_read = num_bytes;
    hnd->stream_data_ptr = data;
    // HACKY-HACK.
    // Without this, libFTDI waits for the timeout to expire when the stream is terminated.
    hnd->ftdi_ptr->usb_read_timeout = 10;
    int ret = ftdi_readstream_ice9(hnd->ftdi_ptr, read_callback, hnd, 8, 16);
    hnd->ftdi_ptr->usb_read_timeout = 5000;
    if (ret > 0) {
        return OK;
    }
    return Error;
}


enum Ice9Error ice9_read(struct ice9_handle *hnd, uint8_t *data, int num_bytes) {
    int read_so_far = 0;
    while (read_so_far < num_bytes) {
        int ret = ftdi_read_data(hnd->ftdi_ptr, data, num_bytes - read_so_far);
        if (ret < 0) {
            return Error;
        }
        read_so_far += ret;
        data += ret;
    }
    return OK;
}

enum Ice9Error ice9_write(struct ice9_handle *hnd, const uint8_t *data, int num_bytes) {
    int ret = ftdi_write_data(hnd->ftdi_ptr, data, num_bytes);
    if (ret == num_bytes) {
        return OK;
    }
    if (ret > 0) {
        return PartialWrite;
    }
    switch (ret) {
        case -666: return USBDeviceUnavailable;
        case -1: return LibUSBIOError;
        case -2: return LibUSBInvalidParameter;
        case -3: return LibUSBAccessDenied;
        case -4: return LibUSBNoDeviceFound;
        case -5: return LibUSBEntityNotFound;
        case -6: return LibUSBResourceBusy;
        case -7: return LibUSBTimeout;
        case -8: return LibUSBOverflow;
        case -9: return LibUSBPipeError;
        case -10: return LibUSBInterrupted;
        case -11: return LibUSBInsufficientMemory;
        case -12: return LibUSBOperationNotSupported;
        default:
            return Error;
    }
}

enum Ice9Error ice9_write_words(struct ice9_handle *hnd, uint16_t *data, uint16_t len) {
    return ice9_write(hnd, (uint8_t*)(data), len * 2);
}

enum Ice9Error ice9_write_word(struct ice9_handle *hnd, uint16_t data) {
    return ice9_write_words(hnd, &data, 1);
}

enum Ice9Error ice9_read_words(struct ice9_handle *hnd, uint16_t *data, uint16_t len) {
    return ice9_read(hnd, (uint8_t*)(data), len * 2);
}

enum Ice9Error ice9_write_data_to_address(struct ice9_handle *hnd, uint8_t address, uint16_t *data, uint16_t len) {
    uint16_t header[2];
    header[0] = 0x0300 | address;
    header[1] = len;
    lib_try(ice9_write_words(hnd, header, 2));
    return ice9_write_words(hnd, data, len);
}

enum Ice9Error ice9_write_word_to_address(struct ice9_handle *hnd, uint8_t address, uint16_t value) {
    return ice9_write_data_to_address(hnd, address, &value, 1);
}

enum Ice9Error ice9_write_int_to_address(struct ice9_handle *hnd, uint8_t address, uint32_t value) {
    uint16_t vals[2];
    vals[0] = (value >> 16) & 0xFFFF;
    vals[1] = value & 0xFFFF;
    return ice9_write_data_to_address(hnd, address, vals, 2);
}

enum Ice9Error ice9_read_int_from_address(struct ice9_handle *hnd, uint8_t address, uint32_t *data) {
    uint16_t reply[2];
    lib_try(ice9_read_data_from_address(hnd, address, reply, 2));
    data[0] = (reply[0] << 16) | reply[1];
    return OK;
}

enum Ice9Error ice9_read_data_from_address(struct ice9_handle *hnd, uint8_t address, uint16_t *data, uint16_t len) {
    uint16_t header[2];
    header[0] = 0x0200 | address;
    header[1] = len;
    lib_try(ice9_write_words(hnd, header, 2));
    return ice9_read_words(hnd, data, len);
}

enum Ice9Error ice9_send_ping(struct ice9_handle *hnd, uint8_t pingid) {
    return ice9_write_word(hnd, 0x0100 | pingid);
}

enum Ice9Error ice9_ping_bridge(struct ice9_handle *hnd, uint8_t pingid) {
    lib_try(ice9_send_ping(hnd, pingid));
    uint16_t pingret = 0;
    usleep(1000);
    lib_try(ice9_read_words(hnd, &pingret, 1));
    pingret = pingret & 0xFF;
    if (pingret != pingid) {
        LOG_INFO("ice9 ping mismatch - sent %x, recv %x\n", pingid, pingret);
        return PingMismatch;
    }
    return OK;
}

enum Ice9Error ice9_enable_streaming(struct ice9_handle *hnd, uint8_t address) {
    return ice9_write_word(hnd, 0x0500 | address);
}

enum Ice9Error ice9_disable_streaming(struct ice9_handle *hnd) {
    return ice9_write_word(hnd, 0xFFFF);
}
