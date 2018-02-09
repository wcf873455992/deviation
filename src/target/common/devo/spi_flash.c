/*
    This project is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Deviation is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Deviation.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include "common.h"

#ifndef SPIFLASH_TYPE
    #define SPIFLASH_TYPE SST25VFxxxB
#endif

#ifndef HAS_FLASH_DETECT
    #define HAS_FLASH_DETECT 0
#endif

#if HAS_FLASH_DETECT
    // Defaults for SST25VFxxxB, Devo 10 original memory
    static u8 SPIFLASH_SR_ENABLE    = 0x50;
    static u8 SPIFLASH_PROTECT_MASK = 0x3C;
    static u8 SPIFLASH_WRITE_SIZE   = 2;
    static u8 SPIFLASH_WRITE_CMD    = 0xAD;
    static u8 SPIFLASH_FAST_READ    = 0;
    static u8 SPIFLASH_USE_AAI      = 1;
#else
    #include "spi_flash.h"
#endif

#define CS_HI() gpio_set(GPIOB, GPIO2)
#define CS_LO() gpio_clear(GPIOB, GPIO2)

#if HAS_FLASH_DETECT
extern uint32_t Mass_Block_Count[2]; // see mass_mal.c
/*
 * Detect flash memory type and set variables controlling
 * access accordingly.
 */
void detect_memory_type()
{
    /* When we have an amount of 4096-byte sectors, fill out Structure for file
     * system
     */
    u32 spiflash_sectors = 0;
#if defined USE_DEVOFS && USE_DEVOFS
    u8 fat_offset = 3;    //boot_sector + fat_sector + root_sector
#else
    u8 fat_offset = 0;
#endif
    u8 mfg_id, memtype, capacity;
    u32 id;
    CS_LO();
    spi_xfer(SPI1, 0x9F);
    mfg_id  = (u8)spi_xfer(SPI1, 0);
    memtype = (u8)spi_xfer(SPI1, 0);
    capacity = (u8)spi_xfer(SPI1, 0);
    CS_HI();
    switch (mfg_id) {
    case 0xBF: // Microchip
        if (memtype == 0x25) {
            printf("Microchip SST25VFxxxB SPI Flash found\n");
            spiflash_sectors = 1 << ((capacity & 0x07) + 8);
        }
        break;
    case 0xEF: // Winbond
        if (memtype == 0x40) {
            printf("Winbond SPI Flash found\n");
            SPIFLASH_PROTECT_MASK = 0x1C;
            SPIFLASH_WRITE_SIZE   = 1;
            SPIFLASH_WRITE_CMD    = 0x02;
            SPIFLASH_FAST_READ    = 1;
            SPIFLASH_USE_AAI      = 0;
            spiflash_sectors      = 1 << ((capacity & 0x0f) + 4);
        }
        break;
    case 0x7F: // Extension code, older ISSI, maybe some others
        if (memtype == 0x9D && capacity == 0x46) {
            printf("ISSI IS25CQ032 SPI Flash found\n");
            // ISSI IS25CQ032
            SPIFLASH_SR_ENABLE    = 0x06;  //No EWSR, use standard WREN
            SPIFLASH_PROTECT_MASK = 0x3C;
            SPIFLASH_WRITE_SIZE   = 1;
            SPIFLASH_WRITE_CMD    = 0x02;
            SPIFLASH_FAST_READ    = 1;
            SPIFLASH_USE_AAI      = 0;
            spiflash_sectors      = 512;
        }
        break;
    case 0x9D: // ISSI
        if (memtype == 0x40 || memtype == 0x30) {
            printf("ISSI SPI Flash found\n");
            SPIFLASH_SR_ENABLE    = 0x06;  //No EWSR, use standard WREN
            SPIFLASH_PROTECT_MASK = 0x3C;
            SPIFLASH_WRITE_SIZE   = 1;
            SPIFLASH_WRITE_CMD    = 0x02;
            SPIFLASH_FAST_READ    = 1;
            SPIFLASH_USE_AAI      = 0;
            spiflash_sectors      = 1 << ((capacity & 0x0f) + 4);
        }
        break;
    case 0x01: // Cypress
        break;
    default:
        /* Check older READ ID command */
        printf("Unknown mfg %02X, type %02X, capacity %02X\n",
            mfg_id, memtype, capacity);
        id = SPIFlash_ReadID();
        if (id == 0xBF48BF48) {
            printf("Microchip SST25VFxxxA SPI Flash found\n");
            SPIFLASH_PROTECT_MASK = 0x0C;
            SPIFLASH_WRITE_SIZE   = 1;
            SPIFLASH_WRITE_CMD    = 0xAF;
            spiflash_sectors      = 16;
        }
        break;
    }

    printf("Flash params:\n\
        SPIFLASH_SR_ENABLE    = %02X\n\
        SPIFLASH_PROTECT_MASK = %02X\n\
        SPIFLASH_WRITE_SIZE   = %d\n\
        SPIFLASH_WRITE_CMD    = %02X\n\
        SPIFLASH_FAST_READ    = %d\n\
        SPIFLASH_USE_AAI      = %d\n",
        SPIFLASH_SR_ENABLE,
        SPIFLASH_PROTECT_MASK,
        SPIFLASH_WRITE_SIZE,
        SPIFLASH_WRITE_CMD,
        SPIFLASH_FAST_READ,
        SPIFLASH_USE_AAI);

    printf("%d free sectors\n", spiflash_sectors - SPIFLASH_SECTOR_OFFSET);
    Mass_Block_Count[0] = fat_offset + spiflash_sectors - SPIFLASH_SECTOR_OFFSET;
}
#endif

/*
 *
 */
void SPIFlash_Init()
{
    /* Enable SPI1 */
    rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_SPI1EN);
    /* Enable GPIOA */
    rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
    /* Enable GPIOB */
    rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);

    /* CS */
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, GPIO2);

    /* SCK, MOSI */
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO5 | GPIO7);
    /* MISO */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOAT, GPIO6);

    CS_HI();
    /* Includes enable */
    spi_init_master(SPI1,
                    SPI_CR1_BAUDRATE_FPCLK_DIV_4,
                    SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
                    SPI_CR1_CPHA_CLK_TRANSITION_1,
                    SPI_CR1_DFF_8BIT,
                    SPI_CR1_MSBFIRST);
    spi_enable_software_slave_management(SPI1);
    spi_set_nss_high(SPI1);

    spi_enable(SPI1);
#if HAS_FLASH_DETECT
    detect_memory_type();
#endif
}

static void SPIFlash_SetAddr(unsigned cmd, u32 address)
{
    CS_LO();
    spi_xfer(SPI1, cmd);
    spi_xfer(SPI1, (u8)(address >> 16));
    spi_xfer(SPI1, (u8)(address >>  8));
    spi_xfer(SPI1, (u8)(address));
}

/*
 *
 */
u32 SPIFlash_ReadID()
{
    u32 result;

    SPIFlash_SetAddr(0x90, 0);
    result  = (u8)spi_xfer(SPI1, 0);
    result <<= 8;
    result |= (u8)spi_xfer(SPI1, 0);
    result <<= 8;
    result |= (u8)spi_xfer(SPI1, 0);
    result <<= 8;
    result |= (u8)spi_xfer(SPI1, 0);

    CS_HI();
    return result;
}

void SPI_FlashBlockWriteEnable(unsigned enable)
{
    //printf("SPI_FlashBlockWriteEnable: %d\n", enable);
    CS_LO();
    spi_xfer(SPI1, SPIFLASH_SR_ENABLE);
    CS_HI();
    CS_LO();
    if (SPIFLASH_PROTECT_MASK) {
        spi_xfer(SPI1, 0x01);
        spi_xfer(SPI1, enable ? 0 : SPIFLASH_PROTECT_MASK);
    } else {
        //Not yet implemented case: SST26
    }
    CS_HI();
}

/*
 *
 */
void WriteFlashWriteEnable()
{
    CS_LO();
    spi_xfer(SPI1, 0x06);
    CS_HI();
}
/*
 *
 */
void WriteFlashWriteDisable()
{
    CS_LO();
    spi_xfer(SPI1, 0x04);
    CS_HI();
}
/*
 *
 */
void DisableHWRYBY()
{
    CS_LO();
    spi_xfer(SPI1, 0x80);
    CS_HI();
}
/*
 *
 */
void WaitForWriteComplete()
{
    unsigned sr;
    // We disable interrupts in SPI operation so we
    // need to periodically re-enable them.
    while(true) {
        int i;
        CS_LO();
        spi_xfer(SPI1, 0x05);
        for (i = 0; i < 100; ++i) {
            sr = spi_xfer(SPI1, 0x00);
            if (!(sr & 0x01)) break;
        }
        CS_HI();
        if (i < 100) break;
    }
}
/*
 *
 */
void SPIFlash_EraseSector(u32 sectorAddress)
{
    //printf("SPI erase sector, addr %06x\r\n", sectorAddress);
    WriteFlashWriteEnable();

    SPIFlash_SetAddr(0x20, sectorAddress);
    CS_HI();

    WaitForWriteComplete();
}
/*
 *
 */
void SPIFlash_BulkErase()
{
    printf("BulkErase...\n");

    WriteFlashWriteEnable();

    CS_LO();
    spi_xfer(SPI1, 0xC7);
    CS_HI();

    WaitForWriteComplete();
}

void SPIFlash_WriteBytes(u32 writeAddress, u32 length, const u8 * buffer)
{
    u32 i = 0;
    if(!length) return; // just in case...

    if (SPIFLASH_USE_AAI)
        DisableHWRYBY();

    //printf("SPI write page_prog, addr %06x, length %d\r\n", writeAddress, length);
    WriteFlashWriteEnable();

    if (SPIFLASH_USE_AAI) {
        if (SPIFLASH_WRITE_SIZE == 2 && writeAddress & 0x01) {
            //printf("SPI write slow start(%08x, %d)\n", writeAddress, length);
            SPIFlash_WriteByte(writeAddress,buffer[0]);
            buffer++;
            writeAddress++;
            length--;
            if (length == 0)
                return;
            WriteFlashWriteEnable();
        }
        SPIFlash_SetAddr(SPIFLASH_WRITE_CMD, writeAddress);
        spi_xfer(SPI1, (u8)~buffer[i++]);
        if (SPIFLASH_WRITE_SIZE == 2) {
            spi_xfer(SPI1, i < length ? ~buffer[i++] : 0xff);
        }
    } else {
        SPIFlash_SetAddr(0x02, writeAddress);
    }
    while(i < length) {
        if (SPIFLASH_USE_AAI) {
            CS_HI();
            WaitForWriteComplete();
            CS_LO();
            spi_xfer(SPI1, SPIFLASH_WRITE_CMD);
            if (SPIFLASH_WRITE_SIZE == 2) {
                //Writing 0xff will have no effect even if there is already data at this address
                spi_xfer(SPI1, i < length ? ~buffer[i++] : 0xff);
            }
        }
        spi_xfer(SPI1, (u8)~buffer[i++]);
    }
    CS_HI();
    WaitForWriteComplete();

    WriteFlashWriteDisable();
}

void SPIFlash_WriteByte(u32 writeAddress, const unsigned byte) {
   if (SPIFLASH_USE_AAI)
       DisableHWRYBY();
   WriteFlashWriteEnable();
   SPIFlash_SetAddr(0x02, writeAddress);
   spi_xfer(SPI1, (u8)(~byte));
   CS_HI();
   WaitForWriteComplete();
   WriteFlashWriteDisable();

}



/*
 *
 */
void SPIFlash_ReadBytes(u32 readAddress, u32 length, u8 * buffer)
{
    u32 i;
    if (SPIFLASH_FAST_READ) {
        SPIFlash_SetAddr(0x0b, readAddress);
        spi_xfer(SPI1, 0); // Dummy read
    } else {
        SPIFlash_SetAddr(0x03, readAddress);
    }

    for(i=0;i<length;i++)
    {
        buffer[i] = ~spi_xfer(SPI1, 0);
    }

    CS_HI();
}

int SPIFlash_ReadBytesStopCR(u32 readAddress, u32 length, u8 * buffer)
{
    u32 i;
    if (SPIFLASH_FAST_READ) {
        SPIFlash_SetAddr(0x0b, readAddress);
        spi_xfer(SPI1, 0); // Dummy read
    } else {
        SPIFlash_SetAddr(0x03, readAddress);
    }

    for(i=0;i<length;i++)
    {
        buffer[i] = ~spi_xfer(SPI1, 0);
        if (buffer[i] == '\n') {
            i++;
            break;
        }
    }

    CS_HI();
    return i;
}

void debug_spi_flash()
{
    u8 data[512];
    u8 check[512];
    unsigned i;
    memset(check, 0, sizeof(check));
    int start = 0x1000*SPIFLASH_SECTOR_OFFSET;

    for (i = 0; i < sizeof(data); i++) {
        data[i] = rand32();
    }
    WriteFlashWriteEnable();
    SPIFlash_EraseSector(start);
    SPIFlash_ReadBytes(start, 101, check);
    for (i = 0; i < 101; i++) {
        if (check[i] != 0) {
          printf("Failed to erase at %d (%02x)\n", i, check[i]);
          break;
        }
    }
    SPIFlash_WriteBytes(start, 101, data);
    SPIFlash_ReadBytes(start, 101, check);
    printf("--------1\n");
    for (i = 0; i < 101; i++) {
        if (data[i] != check[i])  {
            printf("%04x: %02x != %02x\n", i, data[i], check[i]);
        }
    }
    printf("--------2\n");
    SPIFlash_WriteBytes(start +101 , 100, data + 101);
    SPIFlash_ReadBytes(start  +101 , 100, check + 101);
    for (i = 100; i < 201; i++) {
        if (data[i] != check[i])  {
            printf("%04x: %02x != %02x\n", i, data[i], check[i]);
        }
    }
    SPIFlash_WriteBytes(start + 223, 33, data+223);
    SPIFlash_WriteBytes(start + 201, 22, data+201);
    SPIFlash_ReadBytes(start + 200,  56, check + 200);
    printf("--------3\n");
    for (i = 200; i < 256; i++) {
        if (data[i] != check[i])  {
            printf("%04x: %02x != %02x\n", i, data[i], check[i]);
        }
    }
    while(1) { if(PWR_CheckPowerSwitch()) PWR_Shutdown(); }
}
