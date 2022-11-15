#include "ice9.h"
#include "logger.h"
#include <time.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ICE9_VENDOR_ID 0x3524
#define ICE9_DATA_PRODUCT_ID 0x0002

#define MIN(a, b) ((a) < (b)) ? (a) : (b)

struct ice9_handle {
    struct libusb_context *context;
    struct libusb_device_handle *device;
    uint8_t *read_buffer;
    int read_buffer_size;
    int read_buffer_head;
    int read_buffer_tail;
    int stream_bytes_to_read;
    int stream_bytes_read_so_far;
    uint8_t *stream_data_ptr;
    uint8_t *extra_data_buffer;
    uint8_t *extra_data_read_pointer;
    int extra_data_bytes;
    uint8_t *bulk_sync_read_buffer;
};

#define BANK_SIZE (1024*1024)
#define PACKET_SIZE 4096
#define RING_BUFFER_SIZE (1024*1024)

static enum Ice9Error ecode;

#define lib_try(x) {ecode = (x); if (ecode != OK) {return ecode;}}

// Helper functions for the ring buffer
int bytes_in_read_buffer(struct ice9_handle* hnd) {
    return ((hnd->read_buffer_head + RING_BUFFER_SIZE - hnd->read_buffer_tail) % RING_BUFFER_SIZE);
}

// The max fill is one byte less since we do not track 
// if head==tail means the buffer is full or if it is empty.
int free_space_in_read_buffer(struct ice9_handle* hnd) {
    return RING_BUFFER_SIZE - 1 - bytes_in_read_buffer(hnd);
}

// Read bytes from the read buffer to the destination buffer up to the specified
// count.  Will not underflow the buffer.  Returns the number of bytes actually
// transferred.  Callers responsibility to make sure dest buffer can hold count 
// bytes.
int drain_from_read_buffer(struct ice9_handle* hnd, uint8_t* dest, int count) {
    // Because of the modulo operations, we cache this calculation.
    int in_buffer = bytes_in_read_buffer(hnd);
    // This can always be done in at most 2 memcopy operations.  The first step 
    // is to update the count so it does not exceed the number of bytes we have
    // in the buffer.
    count = MIN(count, in_buffer);
    // The first transfer takes tail to min(tail + count, BUFSIZE), or
    // min(count, BUFSIZE-tail) bytes
    int first_transfer = MIN(count, RING_BUFFER_SIZE - hnd->read_buffer_tail);
    memcpy(dest, hnd->read_buffer + hnd->read_buffer_tail, first_transfer);
    dest += first_transfer;
    // Update the tail pointer
    hnd->read_buffer_tail = (hnd->read_buffer_tail + first_transfer) % RING_BUFFER_SIZE;
    // The second transfer is now up to the remaining bytes
    int second_transfer = count - first_transfer;
    memcpy(dest, hnd->read_buffer + hnd->read_buffer_tail, second_transfer);
    hnd->read_buffer_tail += second_transfer;
    return count;
}

// Write bytes to the read buffer up to the specified count.  Will not overflow the
// buffer.  Returns the number of bytes actually written.  
int enqueue_to_read_buffer(struct ice9_handle* hnd, const uint8_t*src, int count) {
    // Calculate the amount of free space and adjust the count
    int buffer_space = free_space_in_read_buffer(hnd);
    // adjust the bytes to enqueue to ensure the buffer does not overflow
    count = MIN(count, buffer_space);
    // This can always be done in at most 2 memcpy operations.  The first
    // transfer takes head to min(head + count, BUFSIZE) or 
    // min(count, BUFSIZE-head) bytes
    int first_transfer = MIN(count, RING_BUFFER_SIZE - hnd->read_buffer_head);
    memcpy(hnd->read_buffer + hnd->read_buffer_head, src, first_transfer);
    src += first_transfer;
    // Update the head pointer
    hnd->read_buffer_head = (hnd->read_buffer_head + first_transfer) % RING_BUFFER_SIZE;
    // The second transfer is now up to the remaining bytes
    int second_transfer = count - first_transfer;
    memcpy(hnd->read_buffer + hnd->read_buffer_head, src, second_transfer);
    hnd->read_buffer_head += second_transfer;
    return count;
}


struct ice9_handle* ice9_new(void) {
    struct ice9_handle *p = (struct ice9_handle *)(malloc(sizeof(struct ice9_handle)));
    if (libusb_init(&p->context) < 0) {
        return NULL;
    }
    p->read_buffer = (uint8_t*) malloc(RING_BUFFER_SIZE);
    p->read_buffer_size = RING_BUFFER_SIZE;
    p->read_buffer_head = 0;
    p->read_buffer_tail = 0;
    p->extra_data_buffer = (uint8_t*) malloc(BANK_SIZE);
    p->extra_data_read_pointer = p->extra_data_buffer;
    p->extra_data_bytes = 0;
    p->bulk_sync_read_buffer = (uint8_t*) malloc(PACKET_SIZE);
    return p;
}

void ice9_free(struct ice9_handle *hnd) {
    libusb_exit(hnd->context);
    free(hnd);
}

enum Ice9Error ice9_open(struct ice9_handle *hnd) {
    hnd->device = libusb_open_device_with_vid_pid(hnd->context, ICE9_VENDOR_ID, ICE9_DATA_PRODUCT_ID);
    if (hnd->device == NULL) {
        return USBDeviceNotFound;
    }
    return OK;
}

enum Ice9Error ice9_usb_reset(struct ice9_handle *hnd) {
    LOG_INFO("Reset USB w/FTDI packets\n");
    // First, send a 0x40, 0, 0, 0
    if (libusb_control_transfer(hnd->device, 0x40, 0, 0, 0, NULL, 0, 1000) < 0)
    {
        LOG_ERROR("Unable to send reset to chip...\n");
        return ResetFailed;
    }
    usleep(1000);
    // Second, send a 0x40, 0, 1, 0 (2 of these)
    for (int i = 0; i < 2; i++)
    {
        if (libusb_control_transfer(hnd->device, 0x40, 0, 1, 0, NULL, 0, 1000) < 0)
        {
            LOG_ERROR("Unable to send 0x40 x 1 reset\n");
            return ResetFailed;
        }
        usleep(1000);
    }
    unsigned char dummy[4096];
    int transferred = 0;
    // Do a read from endpoint 1 for some reason.
    int ret = libusb_bulk_transfer(hnd->device, 0x81, dummy, 4096, &transferred, 1000);
    if (ret < 0)
    {
        LOG_ERROR("Unable to issue bulk read to endpoint 1 - libusb error code %d\n", ret);
        return ResetFailed;
    }
    LOG_INFO("Reset bytes received: %d  %x %x %x %x\n", transferred, dummy[0], dummy[1], dummy[2], dummy[3]);
    // Second, send a 0x40, 0, 1, 0 (4 of these)
    for (int i = 0; i < 4; i++)
    {
        if (libusb_control_transfer(hnd->device, 0x40, 0, 1, 0, NULL, 0, 1000) < 0)
        {
            LOG_ERROR("Unable to send 0x40 x 1 reset\n");
            return ResetFailed;
        }
    }
    return OK;
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
    // Next we send a 0x40, 0, 2
    if (libusb_control_transfer(hnd->device, 0x40, 0, 2, 0, NULL, 0, 1000) < 0) {
        LOG_ERROR("Unable to send 0x40 x 0 2\n");
        return CannotEnableBitBangMode;
    }

    // Then we send a 0x40, 11, 0x00ff
    if (libusb_control_transfer(hnd->device, 0x40, 11, 0x000ff, 0, NULL, 0, 1000) < 0) {
        LOG_ERROR("Unable to send 0x40,11 request\n");
        return CannotEnableBitBangMode;
    }
    
    // Finally, we send the mode change
    if (libusb_control_transfer(hnd->device, 0x40, 11, 0x40FF, 0, NULL, 0, 1000) < 0) {
        LOG_ERROR("Unable to send 0x40 11 0x40ff reset\n");
        return CannotEnableBitBangMode;
    }

    int transferred = 0;
    unsigned char dummy[4096];
    // Do a read from endpoint 1 for some reason.
    int ret = libusb_bulk_transfer(hnd->device, 0x81, dummy, 4096, &transferred, 1000);
    if (ret < 0)
    {
        LOG_ERROR("Unable to issue bulk read to endpoint 1 - libusb error code %d\n", ret);
        return ResetFailed;
    }
    LOG_ERROR("Mode set reset bytes received: %d  %x %x %x %x\n", transferred, dummy[0], dummy[1], dummy[2], dummy[3]);

    // Send a lot of zeros...
    unsigned char jnk[4096];
    memset(jnk, 0, 4096);
    int actual_length = 0;
    ret = libusb_bulk_transfer(hnd->device, 0x02, jnk, 4096, &actual_length, 1000);
    LOG_ERROR("Reset clear write packet %d %d\n", actual_length, ret);
    ice9_ping_bridge(hnd, 0x67);
    return OK;
}

enum Ice9Error ice9_close(struct ice9_handle *hnd) {
    libusb_close(hnd->device);
    return OK;
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

int read_callback(uint8_t *buffer, int length, void *userdata) {
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
/*
int ftdi_readstream_ice9(struct ftdi_context *ftdi,
                     FTDIStreamCallback *callback, void *userdata,
                     int packetsPerTransfer, int numTransfers);
*/



enum Ice9Error ice9_stream_read(struct ice9_handle *hnd, uint8_t *data, int num_bytes) {
    // First, try and supply as many bytes from the cached buffer as possible
    int from_cache = drain_from_read_buffer(hnd, data, num_bytes);
    data += from_cache;
    num_bytes -= from_cache; // Safe, as drain never returns more than the requested number of bytes.
    // Do we need more data?  Try to siphon from the device
    while (num_bytes > 0) {
        uint8_t buffer[16384];
        int bytes_read = 0;
        // Yes... So request a buffer.  Because of the way USB works, we do not seem to request
        // the number of bytes we actually want.  Instead, we ask for data, and simply supply a 
        // buffer that is large enough to hold the maximum number of bytes that might come back.
        // For this case, we want libusb to issue a lot of requests, so indicate a large buffer.
        // Making the buffer larger than 16K causes it to fail.
        int ret = libusb_bulk_transfer(hnd->device, 0x81, buffer, 16384, &bytes_read, 1000);
        if (ret < 0) {
            LOG_ERROR("libusb transfer error: %s\n", libusb_error_name(ret));
            return Error;
        }
        // Strip the status bytes from the read buffer.
        uint8_t *src = buffer;
        uint8_t buffer_stripped[16384];
        uint8_t *dest = buffer_stripped;
        int valid_read = 0;
        while (bytes_read > 0) { // Note, we assume packets are well formed here
            int to_copy = MIN(510, bytes_read - 2);
            memcpy(dest, src + 2, to_copy);
            bytes_read -= to_copy + 2;
            valid_read += to_copy;
            dest += to_copy;
            src += to_copy + 2;
        }
        // Transfer bytes (as many as possible) to the caller's buffer
        src = buffer_stripped;
        if ((valid_read > 0) && (num_bytes > 0)) {
            int pass_through = MIN(num_bytes, valid_read);
            memcpy(data, src, pass_through);
            src += pass_through;
            data += pass_through;
            num_bytes -= pass_through;
            valid_read -= pass_through;
        }
        // Stash any left over bytes
        if ((num_bytes == 0) && (valid_read != 0)) {
            enqueue_to_read_buffer(hnd, src, valid_read);
            valid_read = 0;
        }
    }
    return OK;
}

enum Ice9Error ice9_read(struct ice9_handle *hnd, uint8_t *data, int num_bytes) {
    // First, try and supply as many bytes from the cached buffer as possible
    int from_cache = drain_from_read_buffer(hnd, data, num_bytes);
    data += from_cache;
    num_bytes -= from_cache; // Safe, as drain never returns more than the requested number of bytes.
    // Do we need more data?  Try to siphon from the device
    while (num_bytes > 0) {
        uint8_t buffer[512];
        int bytes_read = 0;
        // Yes... So request a buffer.  Because of the way USB works, we do not seem to request
        // the number of bytes we actually want.  Instead, we ask for data, and simply supply a 
        // buffer that is large enough to hold the maximum number of bytes that might come back.
        // For this read, we assume the reads are small, so we ask for the data 1 packet at a time
        int ret = libusb_bulk_transfer(hnd->device, 0x81, buffer, 512, &bytes_read, 1000);
        if (ret < 0) {
            LOG_ERROR("libusb transfer error: %s\n", libusb_error_name(ret));
            return Error;
        }
        // Transfer as many bytes to the output as we can.  Discard the first two as they are
        // garbage.
        if (bytes_read > 2) {
            int pass_through = MIN(num_bytes, bytes_read - 2);
            int read_bytes_leftover = bytes_read - 2 - pass_through;
            memcpy(data, buffer + 2, pass_through);
            data += pass_through;
            num_bytes -= pass_through;
            // Check for the case that we have satisfied the read request, but there are leftover
            // bytes
            if ((num_bytes == 0) && (read_bytes_leftover > 0)) {
                enqueue_to_read_buffer(hnd, buffer + pass_through, read_bytes_leftover);
            }
        }
    }
    return OK;
}

enum Ice9Error ice9_write(struct ice9_handle *hnd, const uint8_t *data, int num_bytes) {
    int actual_length = 0;
    if (libusb_bulk_transfer(hnd->device, 0x02, (unsigned char *) data, num_bytes, &actual_length, 1000) < 0) {
        return LibUSBIOError;
    }
    if (actual_length != num_bytes) {
        return PartialWrite;
    }
    return OK;
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
