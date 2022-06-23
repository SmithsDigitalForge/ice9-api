/*
 *  iceprog -- simple programming tool for FTDI-based Lattice iCE programmers
 *
 *  Copyright (C) 2015  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2018  Piotr Esden-Tempski <piotr@esden.net>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *  Relevant Documents:
 *  -------------------
 *  http://www.latticesemi.com/~/media/Documents/UserManuals/EI/icestickusermanual.pdf
 *  http://www.micron.com/~/media/documents/products/data-sheet/nor-flash/serial-nor/n25q/n25q_32mb_3v_65nm.pdf
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lattice_cmds.h"
#include "mpsse.h"
#include "ice9.h"

enum device_type {
	TYPE_NONE = 0,
	TYPE_ECP5 = 1,
	TYPE_NX = 2,
};

struct device_info {
	const char* 	 name;
	uint32_t    	 id;
	enum device_type type;
};

static struct device_info connected_device = {0};


static bool verbose = false;

// ---------------------------------------------------------
// Hardware specific CS, CReset, CDone functions
// ---------------------------------------------------------

static void set_cs_creset(int cs_b, int creset_b)
{
    uint8_t gpio = 0;
    uint8_t direction = 0x93;

    if (cs_b) {
        // ADBUS4 (GPIOL0)
        gpio |= 0x10;
    }

    if (creset_b) {
        // ADBUS7 (GPIOL3)
        gpio |= 0x80;
    }

    mpsse_set_gpio(gpio, direction);
}

static bool get_cdone(void)
{
    // ADBUS6 (GPIOL2)
    return (mpsse_readb_low() & 0x40) != 0;
}

// SRAM reset is the same as flash_chip_select()
// For ease of code reading we use this function instead
static void sram_reset()
{
    // Asserting chip select and reset lines
    set_cs_creset(1, 0);
}

// SRAM chip select assert
// When accessing FPGA SRAM the reset should be released
static void sram_chip_select()
{
    set_cs_creset(0, 1);
}


// SRAM chip select assert
// When accessing FPGA SRAM the reset should be released
static void sram_chip_deselect()
{
    set_cs_creset(1, 1);
}


static void print_idcode(uint32_t idcode){
    connected_device.id = idcode;
	
    /* ECP5 Parts */
    for(int i = 0; i < sizeof(ecp_devices)/sizeof(struct device_id_pair); i++){
        if(idcode == ecp_devices[i].device_id)
            {
                connected_device.name = ecp_devices[i].device_name;
                connected_device.type = TYPE_ECP5;
                printf("IDCODE: 0x%08x (%s)\n", idcode ,ecp_devices[i].device_name);
                return;
            }
    }

    /* NX Parts */
    for(int i = 0; i < sizeof(nx_devices)/sizeof(struct device_id_pair); i++){
        if(idcode == nx_devices[i].device_id)
            {
                connected_device.name = nx_devices[i].device_name;
                connected_device.type = TYPE_NX;
                printf("IDCODE: 0x%08x (%s)\n", idcode ,nx_devices[i].device_name);
                return;
            }
    }
    printf("IDCODE: 0x%08x does not match :(\n", idcode);
    exit(1);
}

void print_ecp5_status_register(uint32_t status){	
	printf("ECP5 Status Register: 0x%08x\n", status);

	if(verbose){
		printf("  Transparent Mode:   %s\n",  status & (1 << 0)  ? "Yes" : "No" );
		printf("  Config Target:      %s\n",  status & (7 << 1)  ? "eFuse" : "SRAM" );
		printf("  JTAG Active:        %s\n",  status & (1 << 4)  ? "Yes" : "No" );
		printf("  PWD Protection:     %s\n",  status & (1 << 5)  ? "Yes" : "No" );
		printf("  Decrypt Enable:     %s\n",  status & (1 << 7)  ? "Yes" : "No" );
		printf("  DONE:               %s\n",  status & (1 << 8)  ? "Yes" : "No" );
		printf("  ISC Enable:         %s\n",  status & (1 << 9)  ? "Yes" : "No" );
		printf("  Write Enable:       %s\n",  status & (1 << 10) ? "Writable" : "Not Writable");
		printf("  Read Enable:        %s\n",  status & (1 << 11) ? "Readable" : "Not Readable");
		printf("  Busy Flag:          %s\n",  status & (1 << 12) ? "Yes" : "No" );
		printf("  Fail Flag:          %s\n",  status & (1 << 13) ? "Yes" : "No" );
		printf("  Feature OTP:        %s\n",  status & (1 << 14) ? "Yes" : "No" );
		printf("  Decrypt Only:       %s\n",  status & (1 << 15) ? "Yes" : "No" );
		printf("  PWD Enable:         %s\n",  status & (1 << 16) ? "Yes" : "No" );
		printf("  Encrypt Preamble:   %s\n",  status & (1 << 20) ? "Yes" : "No" );
		printf("  Std Preamble:       %s\n",  status & (1 << 21) ? "Yes" : "No" );
		printf("  SPIm Fail 1:        %s\n",  status & (1 << 22) ? "Yes" : "No" );
		
		uint8_t bse_error = (status & (7 << 23)) >> 23;
		switch (bse_error){
			case 0b000: printf("  BSE Error Code:     No Error (0b000)\n"); break;
			case 0b001: printf("  BSE Error Code:     ID Error (0b001)\n"); break;
			case 0b010: printf("  BSE Error Code:     CMD Error - illegal command (0b010)\n"); break;
			case 0b011: printf("  BSE Error Code:     CRC Error (0b011)\n"); break;
			case 0b100: printf("  BSE Error Code:     PRMB Error - preamble error (0b100)\n"); break;
			case 0b101: printf("  BSE Error Code:     ABRT Error - configuration aborted by the user (0b101)\n"); break;
			case 0b110: printf("  BSE Error Code:     OVFL Error - data overflow error (0b110)\n"); break;
			case 0b111: printf("  BSE Error Code:     SDM Error - bitstream pass the size of SRAM array (0b111)\n"); break;
		}

		printf("  Execution Error:    %s\n",  status & (1 << 26) ? "Yes" : "No" );
		printf("  ID Error:           %s\n",  status & (1 << 27) ? "Yes" : "No" );
		printf("  Invalid Command:    %s\n",  status & (1 << 28) ? "Yes" : "No" );
		printf("  SED Error:          %s\n",  status & (1 << 29) ? "Yes" : "No" );
		printf("  Bypass Mode:        %s\n",  status & (1 << 30) ? "Yes" : "No" );
		printf("  Flow Through Mode:  %s\n",  status & (1 << 31) ? "Yes" : "No" );
	}
}


static void send_byte_command(uint8_t cmd) {
    uint8_t data[4] = {cmd};
    // First send the command
    mpsse_send_spi(data, 4);
}


static uint32_t read_word_reply() {
    uint8_t data[4] = {0, 0, 0, 0};

    // Then receive the ID code result
    mpsse_xfer_spi(data, 4);

    uint32_t idcode = 0;
    
    /* Format the IDCODE into a 32bit value */
    for(int i = 0; i< 4; i++)
        idcode = idcode << 8 | data[i];

    return idcode;
}

static void sram_prepare()
{
    sram_chip_select();
    send_byte_command(ISC_ENABLE);
    sram_chip_deselect();
    sram_chip_select();
    send_byte_command(ISC_ERASE);
    sram_chip_deselect();
    sram_chip_select();
    send_byte_command(LSC_RESET_CRC);
    sram_chip_deselect();
}


static void sram_read_status()
{
    sram_chip_select();
    send_byte_command(LSC_READ_STATUS);
    uint32_t idcode = read_word_reply();
    print_ecp5_status_register(idcode);
    sram_chip_deselect();
}

static void sram_bitstream_burst()
{
    sram_chip_select();
    send_byte_command(LSC_BITSTREAM_BURST);
}

static void sram_read_id()
{
    sram_chip_select();
    send_byte_command(READ_ID);
    uint32_t idcode = read_word_reply();
    print_idcode(idcode);
    sram_chip_deselect();
}

static void sram_refresh_fpga()
{
    sram_chip_select();
    send_byte_command(LSC_REFRESH);
    sram_chip_deselect();        
}


enum Ice9Error ice9_flash_fpga(const char *filename) {
    int ifnum = 0;
    const char *devstr = "i:0x3524:0x0001";
    bool slow_clock = false;
    FILE *f = NULL;
    long file_size = -1;


    f = fopen(filename, "rb");
    if (f == NULL) {
        return UnableToOpenBitFile;
    }
        
        
    // ---------------------------------------------------------
    // Reset
    // ---------------------------------------------------------

    fprintf(stderr, "init...\n");
    
    mpsse_init(ifnum, devstr, slow_clock);
    
    fprintf(stderr, "reset..\n");
    
    sram_reset();
    usleep(100);
        
    fprintf(stderr, "cdone: %s\n", get_cdone() ? "high" : "low");

    sram_refresh_fpga();
    sram_read_id();
    sram_read_status(); 
    sram_prepare();
    sram_read_status();
    fprintf(stderr, "Bye.\n");
    sram_chip_select();
    send_byte_command(LSC_BITSTREAM_BURST);
    while (1) {
        const uint32_t len = 16*1024;
        static unsigned char buffer[16*1024];
        int rc = fread(buffer, 1, len, f);
        if (rc <= 0) break;
        if (verbose)
            fprintf(stderr, "sending %d bytes.\n", rc);
        mpsse_send_spi(buffer, rc);
    }
    sram_chip_deselect();
    sram_read_status();
    sram_chip_select();
    send_byte_command(ISC_DISABLE);
    sram_chip_deselect();
    mpsse_close();
    return OK;
}

enum Ice9Error ice9_flash_fpga_mem(void *buf, int bufsize) {
    int ifnum = 0;
    const char *devstr = "i:0x3524:0x0001";
    bool slow_clock = false;
    // ---------------------------------------------------------
    // Reset
    // ---------------------------------------------------------

    fprintf(stderr, "init...\n");

    mpsse_init(ifnum, devstr, slow_clock);

    fprintf(stderr, "reset..\n");

    sram_reset();
    usleep(100);

    fprintf(stderr, "cdone: %s\n", get_cdone() ? "high" : "low");

    sram_refresh_fpga();
    sram_read_id();
    sram_read_status();
    sram_prepare();
    sram_read_status();
    fprintf(stderr, "Bye.\n");
    sram_chip_select();
    send_byte_command(LSC_BITSTREAM_BURST);
    while (bufsize) {
        const int len = (bufsize < 16*1024) ? bufsize : 16*1024;
        if (verbose)
            fprintf(stderr, "Sending %d bytes to Ice9\n", len);
        mpsse_send_spi(buf, len);
        buf += len;
        bufsize -= len;
    }
    sram_chip_deselect();
    sram_read_status();
    sram_chip_select();
    send_byte_command(ISC_DISABLE);
    sram_chip_deselect();
    mpsse_close();
    return OK;
}
