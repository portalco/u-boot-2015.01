/*
 * Copyright (C) 2016 Renesas America
 * Copyright (C) 2016 PortalCo
 *
 * This file is released under the terms of GPL v2 and any later version.
 * See the file COPYING in the root directory of the source tree for details.
 */

#include <common.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <i2c.h>
#include <rtc.h>
#include <asm/arch/rza1-regs.h>

#include <spi.h>
#include <spi_flash.h>

/* This function temporary disables the feature of OR-ing the results of
   commands together when using dual spi flash memory. When using single
   flash, it does nothing. */
void qspi_disable_auto_combine(void);

//#define DEBUG

/* Port Function Registers */
#define PORTn_base 0xFCFE3000
#define Pn(n)	(PORTn_base + 0x0000 + n * 4)	/* Port register R/W */
#define PSRn(n)	(PORTn_base + 0x0100 + n * 4)	/* Port set/reset register R/W */
#define PPRn(n)	(PORTn_base + 0x0200 + n * 4)	/* Port pin read register R */
#define PMn(n)	(PORTn_base + 0x0300 + n * 4)	/* Port mode register R/W */
#define PMCn(n)	(PORTn_base + 0x0400 + n * 4)	/* Port mode control register R/W */
#define PFCn(n)	(PORTn_base + 0x0500 + n * 4)	/* Port function control register R/W */
#define PFCEn(n)	(PORTn_base + 0x0600 + n * 4)	/* Port function control expansion register R/W */
#define PNOTn(n)	(PORTn_base + 0x0700 + n * 4)	/* Port NOT register W */
#define PMSRn(n)	(PORTn_base + 0x0800 + n * 4)	/* Port mode set/reset register R/W */
#define PMCSRn(n)	(PORTn_base + 0x0900 + n * 4)	/* Port mode control set/reset register R/W */
#define PFCAEn(n)	(PORTn_base + 0x0A00 + n * 4)	/* Port Function Control Additional Expansion register R/W */
#define PIBCn(n)	(PORTn_base + 0x4000 + n * 4)	/* Port input buffer control register R/W */
#define PBDCn(n)	(PORTn_base + 0x4100 + n * 4)	/* Port bi-direction control register R/W */
#define PIPCn(n)	(PORTn_base + 0x4200 + n * 4)	/* Port IP control register R/W */

enum pfc_pin_alt_mode {ALT1=1, ALT2, ALT3, ALT4, ALT5, ALT6, ALT7, ALT8};
enum pfc_pin_gpio_mode {GPIO_OUT=0, GPIO_IN=1};

const u32 alt_settings[9][3] = {
	/* PFCAEn, PFCEn, PFCn */
	{0,0,0},/* dummy */
	{0,0,0},/* 1st alternative function */
	{0,0,1},/* 2nd alternative function */
	{0,1,0},/* 3rd alternative function */
	{0,1,1},/* 4th alternative function */
	{1,0,0},/* 5th alternative function */
	{1,0,1},/* 6th alternative function */
	{1,1,0},/* 7th alternative function */
	{1,1,1},/* 8th alternative function */
};

/* Arguments:
   n = port(1-9)
   b = bit(0-15)
   d = direction('GPIO_IN','GPIO_OUT')
*/
void pfc_set_gpio(u8 n, u8 b, u8 d)
{
	*(u32 *)PMCSRn(n) = 1UL<<(b+16);	// Pin as GPIO
	//*(u32 *)PSRn(n) = 1UL<<(b+16) | (u32)d<< b;	// Set direction
	if (d == GPIO_OUT) {
		*(u16 *)PMn(n) &= ~(((u16)1) << b);
		*(u16 *)PIBCn(n) &= ~(((u16)1) << b);
	} else {
		*(u16 *)PMn(n) |= 1 << b;
		*(u16 *)PIBCn(n) |= 1 << b;
	}
}

/* Arguments:
    n = port number (P1-P9)
    b = bit number (0-15)
    alt = Alternative mode ('ALT1'-'ALT7')
    inbuf =  Input buffer (0=disabled, 1=enabled)
    bi = Bidirectional mode (0=disabled, 1=enabled)
*/
void pfc_set_pin_function(u16 n, u16 b, u16 alt, u16 inbuf, u16 bi)
{
	u16 value;

	/* Set PFCAEn */
	value = *(u16 *)PFCAEn(n);
	value &= ~(1UL<<b); // clear
	value |= (alt_settings[alt][0] & 1UL) << b; // set(maybe)
	*(u16 *)PFCAEn(n) = value;

	/* Set PFCEn */
	value = *(u16 *)PFCEn(n);
	value &= ~(1UL<<b); // clear
	value |= (alt_settings[alt][1] & 1UL) << b; // set(maybe)
	*(u16 *)PFCEn(n) = value;

	/* Set PFCn */
	value = *(u16 *)PFCn(n);
	value &= ~(1UL<<b); // clear
	value |= (alt_settings[alt][2] & 1UL) << b; // set(maybe)
	*(u16 *)PFCn(n) = value;

	/* Set Pn */
	/* Not used for alternative mode */
	/* NOTE: PIP must be set to '0' for the follow peripherals and Pn must be set instead
		<> Multi-function timer pulse unit
		<> LVDS output interface
		<> Serial sound interface
	   For those, use this to set Pn: *(u32 *)PSRn(n) = 1UL<<(b+16) | direction<<(b);
	*/

	/* Set PIBCn */
	value = *(u16 *)PIBCn(n);
	value &= ~(1UL<<b); // clear
	value |= inbuf << b; // set(maybe)
	*(u16 *)PIBCn(n) = value;

	/* Set PBDCn */
	value = *(u16 *)PBDCn(n);
	value &= ~(1UL<<b); // clear
	value |= bi << b; // set(maybe)
	*(u16 *)PBDCn(n) = value;

	/* Alternative mode '1' (not GPIO '0') */
	*(u32 *)PMCSRn(n) |= 1UL<<(b+16) | 1UL<<(b);

	/* Set PIPCn so pin used for function '1'(not GPIO '0') */
	*(u16 *)PIPCn(n) |= 1UL <<b;
}

/* Arguments:
   n = port(1-9)
   b = bit(0-15)
   v = value (0-1)
*/
void pfc_gpio_set(u8 n, u8 b, u8 v)
{
        if (v)
                *(u32 *)Pn(n) |= 1UL<<b;
        else
                *(u32 *)Pn(n) &= ~(1UL<<b);
}

/* Arguments:
   n = port(1-9)
   b = bit(0-15)
*/
int pfc_gpio_get(u8 n, u8 b)
{
        return ((*(u32 *)PPRn(n)) & (1UL<<b)) ? 1 : 0;
}


int spi_flash_cmd(struct spi_slave *spi, u8 cmd, void *response, size_t len);
struct spi_flash *spi_flash_probe(unsigned int bus, unsigned int cs,
		unsigned int max_hz, unsigned int spi_mode);
int spi_flash_cmd_write(struct spi_slave *spi, const u8 *cmd, size_t cmd_len,
		const void *data, size_t data_len);

DECLARE_GLOBAL_DATA_PTR;

int checkboard(void)
{
	puts("BOARD: Balboa IoT Gateway (based on R7S721021)\n");
	return 0;
}

int board_init(void)
{
	gd->bd->bi_boot_params = (CONFIG_SYS_SDRAM_BASE + 0x100);
	return 0;
}

int board_early_init_f(void)
{
	/* This function runs early in the boot process, before u-boot is relocated
	   to RAM (hence the '_f' in the function name stands for 'still running from
	   flash'). A temporary stack has been set up for us which is why we can
	   have this as C code. */

	int i;

	rtc_reset();	/* to start rtc */

	/* =========== Pin Setup =========== */
	/* Specific for the RZ A1L on the Balboa IoT Gateway board. Adjust for your board as needed. */

	/* Serial Console */
	pfc_set_pin_function(7, 1, ALT4, 0, 0);	/* P7_1 = SC_TxD2 */
	pfc_set_pin_function(1, 7, ALT3, 0, 0);	/* P1_7 = SC_RxD2 */

	/* QSPI ch0 (booted in 1-bit, need to change to 4-bit) */
	pfc_set_pin_function(4, 4, ALT2, 0, 0);	/* P4_4 = QSPI_SCK */
	pfc_set_pin_function(4, 5, ALT2, 0, 0);	/* P4_5 = QSPI_CS */
	pfc_set_pin_function(4, 6, ALT2, 0, 1);	/* P4_6 = QSPI_IO0_SI (bi dir) */
	pfc_set_pin_function(4, 7, ALT2, 0, 1);	/* P4_7 = QSPI_IO1_SO (bi dir) */
	pfc_set_pin_function(4, 2, ALT2, 0, 1);	/* P4_2 = QSPI_IO2 (bi dir) */
	pfc_set_pin_function(4, 3, ALT2, 0, 1);	/* P4_3 = QSPI_IO3 (bi dir) */

	/* SPI for 128MB flash */
	pfc_set_pin_function(6, 12, ALT3, 0, 0); /* P6_12 = SPI_SCK1 */
	pfc_set_pin_function(6, 13, ALT3, 0, 0); /* P6_13 = SPI_CS1 */
	pfc_set_pin_function(6, 14, ALT3, 0, 0); /* P6_14 = SPI_MOSI1 */
	pfc_set_pin_function(6, 15, ALT3, 0, 0); /* P6_15 = SPI_MISO1 */

	/* Ethernet */
	/* Note: TXER and IRQ signals are not used */
	pfc_set_pin_function(8, 14, ALT2, 0, 0);	/* P8_14 = ET_COL */
	pfc_set_pin_function(9, 0, ALT2, 0, 0);		/* P9_0 = ET_MDC */
	pfc_set_pin_function(9, 1, ALT2, 0, 1);		/* P9_1 = ET_MDIO (bi dir) */
	pfc_set_pin_function(9, 2, ALT2, 0, 0);		/* P9_2 = ET_RXCLK */
	pfc_set_pin_function(9, 3, ALT2, 0, 0);		/* P9_3 = ET_RXER */
	pfc_set_pin_function(9, 4, ALT2, 0, 0);		/* P9_4 = ET_RXDV */
	pfc_set_pin_function(8, 4, ALT2, 0, 0);		/* P8_4 = ET_TXCLK */
	pfc_set_pin_function(8, 6, ALT2, 0, 0);		/* P8_6 = ET_TXEN */
	pfc_set_pin_function(8, 15, ALT2, 0, 0);	/* P8_15 = ET_CRS */
	pfc_set_pin_function(8, 0, ALT2, 0, 0);		/* P8_0 = ET_TXD0 */
	pfc_set_pin_function(8, 1, ALT2, 0, 0);		/* P8_1 = ET_TXD1 */
	pfc_set_pin_function(8, 2, ALT2, 0, 0);		/* P8_2 = ET_TXD2 */
	pfc_set_pin_function(8, 3, ALT2, 0, 0);		/* P8_3 = ET_TXD3 */
	pfc_set_pin_function(8, 7, ALT2, 0, 0);		/* P8_7 = ET_RXD0 */
	pfc_set_pin_function(8, 8, ALT2, 0, 0);		/* P8_8 = ET_RXD1 */
	pfc_set_pin_function(8, 9, ALT2, 0, 0);		/* P8_9 = ET_RXD2 */
	pfc_set_pin_function(8, 10, ALT2, 0, 0);	/* P8_10 = ET_RXD3 */

	/* SDRAM */
	for(i=0;i<=15;i++)
		pfc_set_pin_function(5, i, ALT1, 0, 1);	/* P5_0~15 = D0-D15 (bi dir) */
	pfc_set_pin_function(2, 1, ALT1, 0, 0);	/* P2_1 = RAS */
	pfc_set_pin_function(2, 2, ALT1, 0, 0);	/* P2_2 = CAS */
	pfc_set_pin_function(2, 6, ALT1, 0, 0);	/* P2_6 = WE */
	pfc_set_pin_function(2, 4, ALT1, 0, 0);	/* P2_4 = DQMLL */
	pfc_set_pin_function(2, 5, ALT1, 0, 0);	/* P2_5 = DQMLU */
	for(i=0;i<=12;i++)
		pfc_set_pin_function(3, i, ALT1, 0, 0);	/* P3_0~12: SDRAM_A0-A12 */
	pfc_set_pin_function(3, 13, ALT1, 0, 0);	/* P3_13 = SDRAM_BA0 */
	pfc_set_pin_function(3, 14, ALT1, 0, 0);	/* P3_14 = SDRAM_BA1 */
	pfc_set_pin_function(7, 8, ALT1, 0, 0);	/* P7_8 = CS2 */
	pfc_set_pin_function(2, 0, ALT1, 0, 0);	/* P2_0 = CS3 */	
	pfc_set_pin_function(2, 3, ALT1, 0, 0);	/* P2_3 = CKE */

	/* LEDs */
	pfc_set_gpio(6, 0, GPIO_OUT); /* P6_0 = LED2 */
	pfc_set_gpio(6, 1, GPIO_OUT); /* P6_1 = LED3 */

	/* Pushbutton */
	pfc_set_gpio(8, 11, GPIO_IN); /* P8_11 = PUSH */

	/* RS485 */
	pfc_set_pin_function(7, 5, ALT7, 0, 0);	/* P7_5 = RS485_RX */
	pfc_set_pin_function(7, 6, ALT7, 0, 0);	/* P7_6 = RS485_TX */

	/* For the new board revision (RS485 moved from SCI1 to SCIF1): */
	pfc_set_pin_function(1, 9, ALT3, 0, 0);	/* P1_9 = RS485_RX */
	pfc_set_pin_function(3, 15, ALT5, 0, 0);	/* P3_15 = RS485_TX */

	/* BTWIFI control pins */
	pfc_set_gpio(7, 3, GPIO_OUT); /* P7_3 = BTWIFI_RSTN */
	pfc_gpio_set(7, 3, 1); /* BTWIFI_RSTN inactive */
	pfc_set_gpio(7, 2, GPIO_IN); /* P7_2 = BTWIFI_HOST_WAKEUP */

	/* USB control pins */
//	pfc_set_gpio(1, 9, GPIO_OUT); /* P1_9 = USB_DEVICE_LS_ON */
//	pfc_set_gpio(6, 10, GPIO_OUT); /* P6_10 = USB_DEVICE_LS_ON */
	pfc_gpio_set(1, 9, 1); /* LS Off */
	pfc_set_gpio(1, 10, GPIO_OUT); /* P1_10 = USB_DEVICE_FS_ON */
	pfc_gpio_set(1, 10, 1); /* FS Off */
	pfc_set_gpio(1, 11, GPIO_OUT); /* P1_11 = ~USB_HOST_ON */
	pfc_set_gpio(1, 12, GPIO_OUT); /* P1_12 = USB_HOST_ON */
	pfc_gpio_set(1, 11, 0); /* Host On */
	pfc_gpio_set(1, 12, 1); /* Host On */

	/* RIIC Ch 1 */
	pfc_set_pin_function(1, 2, ALT1, 0, 1); /* P1_2 = RIIC1SCL (bi dir) */
	pfc_set_pin_function(1, 3, ALT1, 0, 1); /* P1_3 = RIIC1SDA (bi dir) */


	/**********************************************/
	/* Configure NOR Flash Chip Select (CS0, CS1) */
	/**********************************************/
	#define CS0WCR_D	0x00000b40
	#define CS0BCR_D	0x10000C00
	#define CS1WCR_D	0x00000b40
	#define CS1BCR_D	0x10000C00
	*(u32 *)CS0WCR = CS0WCR_D;
	*(u32 *)CS0BCR = CS0BCR_D;
	*(u32 *)CS1WCR = CS1WCR_D;
	*(u32 *)CS1BCR = CS1BCR_D;

	/**********************************************/
	/* Configure SDRAM (CS2, CS3)                 */
	/**********************************************/
	#define CS2BCR_D	0x00004C00
	#define CS2WCR_D	0x00000480 /* CAS Latency = 2 */
	#define CS3BCR_D	0x00004C00
	#define CS3WCR_D	0x00004492
	#define SDCR_D		0x00120812
	#define RTCOR_D		0xA55A0080
	#define RTCSR_D		0xA55A0008
	*(u32 *)CS2BCR = CS2BCR_D;
	*(u32 *)CS2WCR = CS2WCR_D;
	*(u32 *)CS3BCR = CS3BCR_D;
	*(u32 *)CS3WCR = CS3WCR_D;
	*(u32 *)SDCR = SDCR_D;
	*(u32 *)RTCOR = RTCOR_D;
	*(u32 *)RTCSR = RTCSR_D;

	/* wait */
	#define REPEAT_D 0x000033F1
	for (i=0;i<REPEAT_D;i++) {
		asm("nop");
	}

	/* The final step is to set the SDRAM Mode Register by written to a
	   specific address (the data value is ignored) */
	/* Check the hardware manual if your settings differ */
	/* See p.238/2493 of r01uh0437ej0200_rz_a1l.pdf, table
	   "Access Address in SDRAM Mode Register Write" */
	#define SDRAM_MODE_CS2 0x3FFFD040 /* 16 bit, CAS latency = 2, burst */
	#define SDRAM_MODE_CS3 0x3FFFE040
	*(u32 *)SDRAM_MODE_CS2 = 0;
	*(u32 *)SDRAM_MODE_CS3 = 0;

	return 0;
}

int board_late_init(void)
{
	u8 mac[6];

	/* Read Mac Address and set*/
	i2c_init(CONFIG_SYS_I2C_SPEED, 0);
	i2c_set_bus_num(CONFIG_SYS_I2C_MODULE);

	/* Read MAC address */
	i2c_read(CONFIG_SYS_I2C_EEPROM_ADDR,
	         CONFIG_SH_ETHER_EEPROM_ADDR,
	         CONFIG_SYS_I2C_EEPROM_ADDR_LEN,
	         mac, 6);

	if (is_valid_ether_addr(mac))
		eth_setenv_enetaddr("ethaddr", mac);

	printf( "\n\n");
	printf( "\t   ------------------------\n");
	printf( "\t   Balboa IoT gateway board\n");
	printf( "\t   ------------------------\n");
	printf( "\n\n");
	printf(	"\t      SPI Flash Memory Map\n"
		"\t------------------------------\n"
		"\t         Start      Size\n");
	printf(	"\tu-boot:  0x%08X 0x%08X\n", 0,CONFIG_ENV_OFFSET);
	printf(	"\t   env:  0x%08X 0x%08X\n", CONFIG_ENV_OFFSET, CONFIG_ENV_SIZE);
	printf(	"\t    DT:  0x%08X 0x%08X\n", CONFIG_ENV_OFFSET+CONFIG_ENV_SIZE,CONFIG_ENV_SECT_SIZE);
	printf(	"\tKernel:  0x%08X 0x%08X\n",0x200000, 0x600000);
	printf(	"\trootfs:  0x%08X 0x%08X\n",0x800000, 0x2000000-0x800000);
	printf( "\n\n");

	/* Update commands */
	setenv("update_server", "192.168.0.11");
	setenv("update_uboot", "tftpboot 0x20000000 ${update_server}:u-boot.bin ; "
	                       "sf probe; sf erase 0x000000 0x80000 ; "
                               "sf write 0x20000000 0x000000 ${filesize}");

	/* Boot uImage in external SDRAM */
	/* Rootfs is a squashfs image in memory mapped QSPI */
	/* => run s_boot */
	setenv("s1", "sf probe 0; sf read 0F800000 C0000 8000"); // Read out DT blob
	setenv("s2", "sf read 0F000000 200000 600000"); //Copy Kernel to SDRAM
	setenv("s3", "bootm start 0x0F000000 - 0x0F800000 ; bootm loados ;"\
			"fdt memory 0x0C000000 0x04000000"); // Change memory address in DTB
	setenv("s4", "qspi single"); // Change XIP interface to single QSPI
	setenv("sargs", "console=ttySC2,115200 ignore_loglevel root=/dev/mtdblock0"); // bootargs
	setenv("s_boot", "run s1 s2 s3 s4; set bootargs ${sargs}; fdt chosen; bootm go"); // run the commands

	/* Boot XIP using external SDRAM RAM */
	/* Rootfs is a AXFS image in memory mapped QSPI */
	/* => run xsa_boot */
	/* Read out DT blob */
	setenv("xsa1", "sf probe 0; sf read 0F800000 C0000 8000");
	/* Change memory address in DTB */
	setenv("xsa2", "fdt addr 0F800000 ; fdt memory 0x0C000000 0x04000000"); /* 64MB SDRAM RAM */
	/* Change XIP interface to single QSPI */
	setenv("xsa3", "qspi single ; date 010100012016");
	setenv("xsaargs", "console=ttySC2,115200 ignore_loglevel root=/dev/null rootflags=physaddr=0x18800000"); // bootargs
	setenv("xsa_boot", "run xsa1 xsa2 xsa3; set bootargs ${xsaargs}; fdt chosen; bootx 18200000 0F800000"); // run the commands

	/* Boot XIP using internal 3MB SRAM */
	/* Rootfs is a AXFS image in memory mapped QSPI */
	/* => run xsr_boot */
	/* Read out DT blob */
	setenv("xsr1", "sf probe 0; sf read 20100000 C0000 8000");
	/* Change memory address in DTB */
	setenv("xsr2", "fdt addr 20100000 ; fdt memory 0x20000000 0x00300000"); /* 3MB internal SRAM */
	/* Change XIP interface to single QSPI */
	setenv("xsr3", "qspi single");
	setenv("xsrargs", "console=ttySC2,115200 ignore_loglevel root=/dev/null rootflags=physaddr=0x18800000 rz_irq_trim"); // bootargs
	setenv("xsr_boot", "run xsr1 xsr2 xsr3; set bootargs ${xsrargs}; fdt chosen; bootx 18200000 20100000"); // run the commands


	return 0;
}

int dram_init(void)
{
#if (1 !=  CONFIG_NR_DRAM_BANKS)
# error CONFIG_NR_DRAM_BANKS must set 1 in this board.
#endif
	gd->bd->bi_dram[0].start = CONFIG_SYS_SDRAM_BASE;
	gd->bd->bi_dram[0].size = CONFIG_SYS_SDRAM_SIZE;
	gd->ram_size = CONFIG_SYS_SDRAM_SIZE * CONFIG_NR_DRAM_BANKS;

	return 0;
}

void reset_cpu(ulong addr)
{
	/* Based on rza1_restart () from linux board file */
#define OFFSET_WTCSR 0
#define OFFSET_WTCNT 2
#define OFFSET_WRCSR 4
	void *base = (void *)0xFCFE0000;

	/* Dummy read (must read WRCSR:WOVF at least once before clearing) */
	*(volatile uint8_t *)(base + OFFSET_WRCSR) = *(uint8_t *)(base + OFFSET_WRCSR);

	*(volatile uint16_t *)(base + OFFSET_WRCSR) = 0xA500;  /* Clear WOVF */
	*(volatile uint16_t *)(base + OFFSET_WRCSR) = 0x5A5F;  /* Reset Enable */
	*(volatile uint16_t *)(base + OFFSET_WTCNT) = 0x5A00;  /* Counter to 00 */
	*(volatile uint16_t *)(base + OFFSET_WTCSR) = 0xA578;  /* Start timer */

	while(1); /* Wait for WDT overflow */
}

void led_set_state(unsigned short value)
{
}

/* XIP Kernel boot */
int do_bootx(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	ulong machid = MACH_TYPE_IOT_GATEWAY;
	void (*kernel_entry)(int zero, int arch, uint params);
	ulong r2;
	ulong img_addr;
	char *endp;

	/* need at least two arguments */
	if (argc < 2)
		goto usage;

	img_addr = simple_strtoul(argv[1], &endp, 16);
	kernel_entry = (void (*)(int, int, uint))img_addr;

#ifdef CONFIG_USB_DEVICE
	udc_disconnect();
#endif
	cleanup_before_linux();

	r2 = simple_strtoul(argv[2], NULL, 16);

	/* The kernel expects the following when booting:
	 *  r0 - 0
	 *  r1 - machine type
	 *  r2 - boot data (atags/dt) pointer
	 *
	 * For more info, refer to:
	 *  https://www.kernel.org/doc/Documentation/arm/Booting
	 */

	printf("Booting Linux...\n");

	kernel_entry(0, machid, r2);

	return 0;

usage:
	return CMD_RET_USAGE;
}
static char bootx_help_text[] =
	"x_addr dt_addr\n    - boot XIP kernel in Flash\n"
	"\t x_addr: Address of XIP kernel in Flash\n"
	"\tdt_addr: Address of Device Tree blob image";
U_BOOT_CMD(
	bootx,	CONFIG_SYS_MAXARGS,	1,	do_bootx,
	"boot XIP kernel in Flash", bootx_help_text
);


#define CMNCR_0	0x3FEFA000	/* Common control register */
#define DRCR_0	0x3FEFA00C	/* Data Read Control Register */
#define DRCMR_0	0x3FEFA010	/* Data Read Command Setting Register */
#define DREAR_0 0x3FEFA014	/* Data read extended address setting register */
#define DRENR_0 0x3FEFA01C	/* Data read enable setting register */
#define DROPR_0 0x3FEFA018	/* Data read option setting register */
#define DMDMCR_0 0x3FEFA058	/* SPI Mode Dummy Cycle Setting Register */
#define DRDRENR_0 0x3FEFA05C	/* Data Read DDR Enable Register */


struct read_mode {
	u8 cmd;
	char name[50];
};
#define READ_MODES 9
const struct read_mode modes[READ_MODES] = {
	{0x03, "Read Mode (3-byte Addr) (RZ/A1 reset value)"},
	{0x0C, "Fast Read Mode (4-byte Addr)"},
	{0x6C, "Quad Read Mode (4-byte Addr)"},
	{0xEC, "Quad I/O Read Mode (4-byte Addr)"},
	{0xEE, "Quad I/O DDR Read Mode (4-byte Addr)"},
	{0x0B, "Fast Read Mode (3-byte Addr)"},
	{0x6B, "Quad Read Mode (3-byte Addr)"},
	{0xEB, "Quad I/O Read Mode (3-byte Addr)"},
	{0xED, "Quad I/O DDR Read Mode (3-byte Addr)"},
};

/* If you are using a SPI Flash device that does not have 4-byte address
   commands (Flash size <= 16MB), then change the #if 0 to #if 1 */
#if 0
 #define ADDRESS_BYTE_SIZE 3	/* Addresses are 3-bytes (A0-A23) */
 #define FAST_READ 0x0B		/* Fast Read Mode (1-bit cmd, 1-bit addr, 1-bit data, 3-bytes of address) */
 #define QUAD_READ 0x6B		/* Quad Read Mode (1-bit cmd, 1-bit addr, 4-bit data, 3-bytes of address) */
 #define QUAD_IO_READ 0xEB	/* Quad I/O Read Mode (1-bit cmd, 4-bit addr, 4-bit data, 3-bytes of address) */
 #define QUAD_IO_DDR_READ 0xED	/* Quad I/O DDR Read Mode (1-bit cmd, 1-bit addr, 4-bit data, 3-bytes of address) */
#else
 #define ADDRESS_BYTE_SIZE 4	/* Addresses are 4-bytes (A0-A31) */
 #define FAST_READ 0x0C		/* Fast Read Mode (1-bit cmd, 1-bit addr, 1-bit data, 4-bytes of address) */
 #define QUAD_READ 0x6C		/* Quad Read Mode (1-bit cmd, 1-bit addr, 4-bit data, 4-bytes of address) */
 #define QUAD_IO_READ 0xEC	/* Quad I/O Read Mode (1-bit cmd, 4-bit addr, 4-bit data, 4-bytes of address) */
 #define QUAD_IO_DDR_READ 0xEE	/* Quad I/O DDR Read Mode (1-bit cmd, 1-bit addr, 4-bit data, 4-bytes of address) */
#endif

/* These should be filled out for each device */
u32 g_FAST_RD_DMY;		/* Fast Read Mode */
u32 g_QUAD_RD_DMY;		/* Quad Read Mode  */
u32 g_QUAD_IO_RD_DMY;		/* Quad I/O Read Mode  */
u32 g_QUAD_IO_DDR_RD_DMY;	/* Quad I/O DDR Read Mode  */
u32 g_QUAD_IO_RD_OPT;		/* Addtional option or 'mode' settings */

/**********************/
/* Spansion S25FL512S */
/**********************/
int enable_quad_spansion(struct spi_flash *sf, u8 quad_addr, u8 quad_data )
{
	/* NOTE: Macronix and Windbond are similar to Spansion */
	/* NOTE: Once quad comamnds are enabled, you don't need to disable
		 them to use the non-quad mode commands, so we just always
		 leave them on. */
	int ret = 0;
	u8 data[5];
	u8 cmd;
	u8 spi_cnt = 1;
	u8 st_reg[2];
	u8 cfg_reg[2];

	/* Read ID Register (for cases where not all parts are the same) */
	//ret |= spi_flash_cmd(sf->spi, 0x9F, &data[0], 5);

	if (sf->spi->cs)
		spi_cnt = 2; /* Dual SPI Flash */

	/* Read Status register (RDSR1 05h) */
	qspi_disable_auto_combine();
	ret |= spi_flash_cmd(sf->spi, 0x05, st_reg, 1*spi_cnt);

	/* Read Configuration register (RDCR 35h) */
	qspi_disable_auto_combine();
	ret |= spi_flash_cmd(sf->spi, 0x35, cfg_reg, 1*spi_cnt);

#ifdef DEBUG
	printf("Initial Values:\n");
	for(cmd = 0; cmd <= spi_cnt; cmd++) {
		printf("   SPI Flash %d:\n", cmd+1);
		printf("\tStatus register = %02X\n", st_reg[cmd]);
		printf("\tConfiguration register = %02X\n", cfg_reg[cmd]);
	}
#endif

	/* Skip SPI Flash configure if already correct */
	/* Note that if dual SPI flash, both have to be set */
	if ( (cfg_reg[0] != 0x02 ) ||
	     ((spi_cnt == 2) && (cfg_reg[1] != 0x02 ))) {

		data[0] = 0x00;	/* status reg: Don't Care */
		data[1] = 0x02; /* confg reg: Set QUAD, LC=00b */

		/* Send Write Enable (WREN 06h) */
		ret |= spi_flash_cmd(sf->spi, 0x06, NULL, 0);

		/* Send Write Registers (WRR 01h) */
		cmd = 0x01;
		ret |= spi_flash_cmd_write(sf->spi, &cmd, 1, data, 2);

		/* Wait till WIP clears */
		do
			spi_flash_cmd(sf->spi, 0x05, &data[0], 1);
		while( data[0] & 0x01 );

	}

#ifdef DEBUG
	/* Read Status register (RDSR1 05h) */
	qspi_disable_auto_combine();
	ret |= spi_flash_cmd(sf->spi, 0x05, st_reg, 1*spi_cnt);

	/* Read Configuration register (RDCR 35h) */
	qspi_disable_auto_combine();
	ret |= spi_flash_cmd(sf->spi, 0x35, cfg_reg, 1*spi_cnt);

	printf("New Values:\n");
	for(cmd = 0; cmd <= spi_cnt; cmd++) {
		printf("   SPI Flash %d:\n", cmd+1);
		printf("\tStatus register = %02X\n", st_reg[cmd]);
		printf("\tConfiguration register = %02X\n", cfg_reg[cmd]);
	}
#endif

	/* Finally, fill out the global settings for
	   Number of Dummy cycles between Address and Data */

	/* Spansion S25FL512S */
	/* According to the Spansion spec (Table 8.5), dummy cycles
	   are needed when LC=00 (chip default) for FAST READ,
	   QUAD READ, and QUAD I/O READ commands */
	g_FAST_RD_DMY = 8;		/* Fast Read Mode: 8 cycles */
	g_QUAD_RD_DMY = 8;		/* Quad Read Mode  */
	g_QUAD_IO_RD_DMY = 4;		/* Quad I/O Read Mode: 4 cycles */
	g_QUAD_IO_DDR_RD_DMY = 6;	/* Quad I/O DDR Read Mode  (NOT SUPPORTED) */

	/* When sending a QUAD I/O READ command, and extra MODE field
	   is needed.
	     [[ Single Data Rate, Quad I/O Read, Latency Code=00b ]]
		<> command = 1-bit, 8 clocks
		<> Addr(32bit) = 4-bit, 8 clocks,
		<> Mode = 4-bit, 2 clocks
		<> Dummy = 4-bit, 4 clocks
		<> Data = 4-bit, 2 clocks x {length}
	    See "Figure 10.37 Quad I/O Read Command Sequence" in Spansion spec
	*/
	/* Use Option data regsiters to output '0' as the
	   'Mode' field by sending OPD3 (at 4-bit) between address
	   and dummy */
	g_QUAD_IO_RD_OPT = 1;

	return ret;
}

/* This function is called when "sf probe" is issued, meaning that
   the user wants to access the deivce in normal single wire SPI mode.
   Since some SPI devices have to have special setups to enable QSPI mode
   or DDR QSPI mode, this function is used to reset those settings
   back to normal single wire FAST_READ mode. */
int qspi_reset_device(struct spi_flash *sf)
{
	int ret = 0;

	if(( !strcmp(sf->name, "S25FL512S_256K") ) ||
	   ( !strcmp(sf->name, "S25FL256S_64K") )) {
		/* Don't really need to do anything */
	}
	else {
		printf("\tWARNING: SPI Flash needs to be added to function %s()\n",__func__);
		return 1;
	}
	return ret;
}

/* QUAD SPI MODE */
int do_qspi(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct spi_flash *my_spi_flash;
	int ret = 0;
	int i;
	u8 cmd;
	u8 dual_chip;
	u8 quad_data;
	u8 quad_addr;
	u8 ddr;
	u32 dmdmcr, drenr, cmncr, drcmr, dropr, drdrenr;

	/* need at least 1 argument (single/dual) */
	if (argc < 2)
		goto usage;

	if ( strcmp(argv[1], "single") == 0)
		dual_chip = 0;
	else if ( strcmp(argv[1], "dual") == 0)
		dual_chip = 1;
	else
		goto usage;

	if ( argc <= 2 )
		quad_addr = 1;
	else if ( strcmp(argv[2], "a1") == 0)
		quad_addr = 0;
	else if ( strcmp(argv[2], "a4") == 0)
		quad_addr = 1;
	else
		goto usage;

	if ( argc <= 3 )
		quad_data = 1;
	else if ( strcmp(argv[3], "d1") == 0)
		quad_data = 0;
	else if ( strcmp(argv[3], "d4") == 0)
		quad_data = 1;
	else
		goto usage;

	if ( argc <= 4 )
		ddr = 0;
	else if ( strcmp(argv[4], "sdr") == 0)
		ddr = 0;
	else if ( strcmp(argv[4], "ddr") == 0)
		ddr = 1;
	else
		goto usage;

	/* checks */
	if( quad_addr && !quad_data )
		return CMD_RET_USAGE;
	if( ddr && !quad_addr )
		return CMD_RET_USAGE;

	/* Read initial register values */
	dmdmcr = *(volatile u32 *)DMDMCR_0;
	drenr = *(volatile u32 *)DRENR_0;
	cmncr = *(volatile u32 *)CMNCR_0;
	drcmr = *(volatile u32 *)DRCMR_0;
	dropr = *(volatile u32 *)DROPR_0;
	drdrenr = *(volatile u32 *)DRDRENR_0;

	printf("Current Mode: ");
	cmd = (drcmr >> 16) & 0xFF;
	for( i=0; i < READ_MODES; i++) {
		if( modes[i].cmd == cmd )
			printf("%s\n",modes[i].name);
	}

	/* bus=0, cs=0, speed=1000000 */
	if( dual_chip )
		my_spi_flash = spi_flash_probe(0, 1, 1000000, SPI_MODE_3);
	else
		my_spi_flash = spi_flash_probe(0, 0, 1000000, SPI_MODE_3);

	if (!my_spi_flash) {
		printf("Failed to initialize SPI flash.");
		return 1;
	}

	/* For Quad Mode operation, extra setup is needed in the SPI
	   Flash devices */
	if(( !strcmp(my_spi_flash->name, "S25FL512S_256K") ) ||
	   ( !strcmp(my_spi_flash->name, "S25FL256S_64K") ))
		ret = enable_quad_spansion(my_spi_flash, quad_addr, quad_data);
	else
	{
		printf("ERROR: SPI Flash support needs to be added to function %s()\n",__func__);
		spi_flash_free(my_spi_flash);	/* Done with SPI Flash */
		return 1;
	}

	/* Done with SPI Flash */
	spi_flash_free(my_spi_flash);

	if ( ret )
	{
		printf("Failed to set SPI Flash Configuration register.\n");
		return 1;
	}

	/***************************/
	/* Set up RZ SPI Registers */
	/***************************/
	/* Enable data swap (SFDE) */
	/* Keeps the endian order of bytes the same on the internal bus
	   regardless of how you fetched them over SPI */
	cmncr |= 0x01000000UL;

	if( dual_chip ) {
		/* Switch to dual memory */
		cmncr |= 0x00000001UL;
	}
	else {
		/* Switch to single memory */
		cmncr &= ~0x00000001UL;
	}

	/* 1-bit address, 4-bit data */
	if( quad_data && !quad_addr ) {
		/* Set read cmd to Quad Read */
		drcmr = (u32)QUAD_READ << 16;

		/* width: 1-bit cmd, 1-bit addr, 4-bit data */
#if (ADDRESS_BYTE_SIZE == 4)
		/* address: 32 bits */
		drenr = 0x00024f00UL;
#else /* ADDRESS_BYTE_SIZE == 3 */
		/* address: 24 bits */
		drenr = 0x00024700UL;
#endif
		/* Add extra Dummy cycles between address and data */
		dmdmcr = 0x00020000 | (g_QUAD_RD_DMY-1); /* 4 bit width, x cycles */
		drenr |= 0x00008000; /* Set Dummy Cycle Enable (DME) */
	}

	/* 1-bit address, 1-bit data */
	if( !quad_data && !quad_addr ) {
		/* Set read cmd to FAST Read */
		drcmr = (u32)FAST_READ << 16;

		/* width: 1-bit cmd, 1-bit addr, 1-bit data */
#if (ADDRESS_BYTE_SIZE == 4)
		/* address: 32 bits */
		drenr = 0x00004f00;
#else /* ADDRESS_BYTE_SIZE == 3 */
		/* address: 24 bits */
		drenr = 0x00004700;
#endif
		/* Add extra Dummy cycles between address and data */
		dmdmcr = 0x00000000 | (g_FAST_RD_DMY-1); /* 1 bit width, x cycles */
		drenr |= 0x00008000; /* Set Dummy Cycle Enable (DME) */
	}

	/* 4-bit address, 4-bit data */
	if( quad_addr ) {
		/* Set read cmd to Quad I/O */
		drcmr = (u32)QUAD_IO_READ << 16;

		/* width: 1-bit cmd, 4-bit addr, 4-bit data */
#if (ADDRESS_BYTE_SIZE == 4)
		/* address: 32 bits */
		drenr = 0x02024f00;
#else /* ADDRESS_BYTE_SIZE == 3 */
		/* address: 24 bits */
		drenr = 0x02024700;
#endif

		/* Use Option data regsiters to output 0x00 to write the
		   'mode' byte by sending OPD3 (at 4-bit) between address
		   and dummy */
		if ( g_QUAD_IO_RD_OPT ) {
			dropr = 0x00000000;
			drenr |= 0x00200080;	// send OPD3(8-bit) at 4-bit width (2 cycles total)
		}

		/* Add extra Dummy cycles between address and data */
		dmdmcr = 0x00020000 | (g_QUAD_IO_RD_DMY-1); /* 4 bit size, x cycles */

		drenr |= 0x00008000; /* Set Dummy Cycle Enable (DME) */
	}

	if ( ddr ) {
		printf( "WARNING: DDR mode doesn't actually work yet on the RSKRZA1 board.\n"
			"   The Spansion SPI flash has an extra phase in the command stream\n"
			"   that we can't account for.\n");

		/* Set read cmd to Read DDR Quad I/O */
		drcmr = (u32)QUAD_IO_DDR_READ << 16;

		/* Address, option and data all 4-bit DDR */
		drdrenr = 0x00000111;

		/* According to the Spansion spec (Table 8.5), dummy cycles
		   are needed when LC=00b for READ DDR QUAD I/O commnds */
		/* Add extra Dummy cycles between address and data */
		dmdmcr = 0x00020000 | (g_QUAD_IO_DDR_RD_DMY-1); /* 4 bit size, x cycles */
		drenr |= 0x00008000; /* Set Dummy Cycle Enable (DME) */
	}
	else {
		drdrenr = 0;
	}

	/* Set new register values */
	*(volatile u32 *)DMDMCR_0 = dmdmcr;
	*(volatile u32 *)DRENR_0 = drenr;
	*(volatile u32 *)CMNCR_0 = cmncr;
	*(volatile u32 *)DRCMR_0 = drcmr;
	*(volatile u32 *)DROPR_0 = dropr;
	*(volatile u32 *)DRDRENR_0 = drdrenr;

	/* Allow 32MB of SPI addressing (POR default is only 16MB) */
	*(volatile u32 *)DREAR_0 = 0x00000001;

	/* Turn Read Burst on, Burst Length=2 uints (also set cache flush) */
	/* Keep SSL low (SSLE=1) in case the next transfer is continugous with
	   our last...saves on address cycle. */
	*(u32 *)DRCR_0 = 0x00010301;
	asm("nop");
	*(volatile u32 *)DRCR_0;	/* Read must be done after cache flush */

	/* Do some dummy reads (our of order) to help clean things up */
	*(volatile u32 *)0x18000010;
	*(volatile int *)0x18000000;

	printf("New Mode: ");
	cmd = (*(volatile long *)DRCMR_0 >> 16) & 0xFF;
	for( i=0; i < READ_MODES; i++) {
		if( modes[i].cmd == cmd )
			printf("%s\n",modes[i].name);
	}

	return 0;
usage:
	return CMD_RET_USAGE;
}
static char qspi_help_text[] =
	"Set the XIP Mode for QSPI\n"
	"Usage: qspi [single|dual] [a1|(a4)] [d1|(d4)] [(sdr)|ddr]\n"
	"  (xx) means defualt value if not specified\n"
	"  'a4' requries 'd4' to be set\n"
	"  'ddr' requries 'd4' and 'a4' to be set\n";
U_BOOT_CMD(
	qspi,	CONFIG_SYS_MAXARGS,	1,	do_qspi,
	"Change QSPI XIP Mode", qspi_help_text
);

