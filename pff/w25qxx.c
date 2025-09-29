#include "diskio.h"
#include <stdint.h>
#include <stdio.h>

#include "hal_sd.h"
#include "hal_timing.h"

#define flash_select sd_select
#define flash_deselect sd_deselect
#define flash_spi_byte sd_spi_byte
#define flash_led_on sd_led_on
#define flash_led_off sd_led_off


// W25Qxx命令定义
#define W25Q_JEDEC_ID 0x9F    // 读取厂商/设备ID
#define W25Q_READ 0x03        // 普通读取
#define W25Q_PAGE_PROGRAM 0x02 // 页编程
#define W25Q_SECTOR_ERASE 0x20 // 扇区擦除（4KB）
#define W25Q_WRITE_ENABLE 0x06 // 写使能
#define W25Q_READ_STATUS1 0x05 // 读状态寄存器1
#define W25Q_STATUS_BUSY 0x01 // 忙标志位

// Flash类型标识（类似SD卡的CardType）
static uint8_t FlashType;
#define FT_W25QXX 0x01        // 支持W25Q系列

// SPI通信宏定义
#define CS_H() flash_deselect()
#define CS_L() flash_select()
#define xmit_flash(d) flash_spi_byte(d)
#define rcvr_flash() flash_spi_byte(0xFF)
#define DLY_US(n) timing_delay_us(n)

/* 等待Flash操作完成（忙标志清除） */
static void flash_wait_busy(void) {
    CS_L();
    xmit_flash(W25Q_READ_STATUS1);
    while (rcvr_flash() & W25Q_STATUS_BUSY); // 循环等待忙标志位为0
    CS_H();
}

/* 发送写使能命令 */
static void flash_write_enable(void) {
    CS_L();
    xmit_flash(W25Q_WRITE_ENABLE);
    CS_H();
    DLY_US(10); // 确保命令生效
}

/*-----------------------------------------------------------------------*/
/* 初始化Flash设备                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(void) {
    uint8_t id[3];
    FlashType = 0;

    // 初始化SPI总线（通常在HAL层实现，此处仅做设备复位）
    CS_H();
    for (int i = 0; i < 10; i++) rcvr_flash(); // 发送dummy时钟

    // 读取JEDEC ID（制造商ID+设备ID）
    CS_L();
    xmit_flash(W25Q_JEDEC_ID);
    id[0] = rcvr_flash(); // 制造商ID（如Winbond为0xEF）
    id[1] = rcvr_flash(); // 设备ID高8位
    id[2] = rcvr_flash(); // 设备ID低8位
    CS_H();
    printf("read flash id: %X, %X, %X", id[0], id[1], id[2]);

    // 验证W25Q系列ID（以W25Q128为例，ID为0xEF 0x40 0x18）
    if (id[0] == 0xEF && (id[1] != 0)) {
        FlashType = FT_W25QXX;
        return 0; // 初始化成功
    }
    return STA_NOINIT; // 设备不存在或不支持
}

/*-----------------------------------------------------------------------*/
/* 读取扇区部分数据（LBA模式）                                           */
/*-----------------------------------------------------------------------*/
DRESULT disk_readp(
    BYTE *buff,      // 接收缓冲区（NULL表示转发数据）
    DWORD sector,    // 扇区号（LBA，假设1扇区=512字节）
    UINT offset,     // 扇区内偏移（0~511）
    UINT count       // 读取字节数（offset+count ≤512）
) {
    if (!(FlashType & FT_W25QXX)) return RES_NOTRDY;

    // 计算物理地址（假设扇区大小512字节，Flash地址从0开始）
    DWORD addr = sector * 512 + offset;

    CS_L();
    // 发送读命令+24位地址
    xmit_flash(W25Q_READ);
    xmit_flash((addr >> 16) & 0xFF); // 地址高8位
    xmit_flash((addr >> 8) & 0xFF);  // 地址中8位
    xmit_flash(addr & 0xFF);         // 地址低8位
    // 读取数据
    if (buff) {
        for (UINT i = 0; i < count; i++) {
            *buff++ = rcvr_flash();
        }
    } else {
        // 若buff为NULL，可转发数据（如用于流处理）
        for (UINT i = 0; i < count; i++) {
            // FORWARD(rcvr_flash()); // 根据项目需求实现
        }
    }
    CS_H();
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* 写入扇区部分数据（LBA模式）                                           */
/*-----------------------------------------------------------------------*/
#if PF_USE_WRITE
DRESULT disk_writep(
    const BYTE *buff, // 待写入数据（NULL表示初始化/结束写入）
    DWORD sc          // 字节数（非NULL时）或扇区号（NULL且sc≠0时）
) {
    static UINT write_pos; // 记录当前写入位置（页内偏移）
    static DWORD current_sector;

    if (!(FlashType & FT_W25QXX)) return RES_NOTRDY;

    if (buff) {
        // 阶段1：写入数据（需确保已初始化写入）
        UINT len = (UINT)sc;
        DWORD addr = current_sector * 512 + write_pos;

        // W25Qxx页大小为256字节，需检查是否跨页
        UINT page_remain = 256 - (addr % 256);
        if (len > page_remain) len = page_remain;

        // 写使能
        flash_write_enable();
        // 发送页编程命令+地址
        CS_L();
        xmit_flash(W25Q_PAGE_PROGRAM);
        xmit_flash((addr >> 16) & 0xFF);
        xmit_flash((addr >> 8) & 0xFF);
        xmit_flash(addr & 0xFF);
        // 写入数据
        for (UINT i = 0; i < len; i++) {
            xmit_flash(*buff++);
        }
        CS_H();
        flash_wait_busy(); // 等待编程完成

        write_pos += len;
        return (write_pos < 512) ? RES_OK : RES_OK; // 若未写完扇区，返回继续
    } else {
        if (sc != 0) {
            // 阶段0：初始化扇区写入（擦除扇区）
            current_sector = sc;
            write_pos = 0;
            DWORD sector_addr = current_sector * 512;

            // 擦除对应4KB扇区（W25Qxx最小擦除单位为4KB）
            flash_write_enable();
            CS_L();
            xmit_flash(W25Q_SECTOR_ERASE);
            xmit_flash((sector_addr >> 16) & 0xFF);
            xmit_flash((sector_addr >> 8) & 0xFF);
            xmit_flash(sector_addr & 0xFF);
            CS_H();
            flash_wait_busy(); // 等待擦除完成（耗时较长，约100ms）
            return RES_OK;
        } else {
            // 阶段2：结束写入（无需额外操作，Flash无块校验）
            return RES_OK;
        }
    }
}
#endif