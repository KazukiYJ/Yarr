#ifndef PERIPHIALI2C_H
#define PERIPHIALI2C_H
// #################################
// # Author: Timon Heim
// # Email: timon.heim at cern.ch
// # Project: Yarr
// # Description: I2C master interface
// # Comment: Base class for I2C devices
// ################################

#include <iostream>

#include "SpecController.h"

#define I2C_ADDR (0x4 << 14)

// 8 bit regs
#define I2C_PRE_LOW 0x00
#define I2C_PRE_HIGH 0x01
#define I2C_CTRL 0x02
#define I2C_TX 0x03
#define I2C_RX 0x03
#define I2C_CMD 0x04
#define I2C_STAT 0x04

#define I2C_CTRL_IRQ_EN 0x40
#define I2C_CTRL_I2C_EN 0x80
#define I2C_TX_RW 0x01
#define I2C_STAT_IRQ 0x01
#define I2C_STAT_TIP 0x02
#define I2C_STAT_ARB_LOST 0x20
#define I2C_STAT_BUSY 0x40
#define I2C_STAT_ACK 0x80
#define I2C_CMD_IRQ_ACK 0x01
#define I2C_CMD_ACK 0x08
#define I2C_CMD_WR 0x10
#define I2C_CMD_RD 0x20
#define I2C_CMD_STOP 0x40
#define I2C_CMD_START 0x80

#define I2C_TIMEOUT 1e9

class PeriphialI2C {
    public:
    protected:
        PeriphialI2C(SpecController *arg_spec);

        void init();
        int writeReg(uint32_t dev_addr, uint32_t reg_addr, uint32_t value);
        int readReg(uint32_t dev_addr, uint32_t reg_addr, uint32_t *value);

        int setAddr(uint32_t dev_addr, uint32_t reg_addr);
        int writeData(uint32_t value);
        int setupWrite(uint32_t dev_addr);
        int setupRead(uint32_t dev_addr);
        int readData(uint32_t *value);
        int sendNack();
    private:
        SpecController *spec;
        uint32_t last_dev_addr;
        uint32_t last_reg_addr;
        uint32_t last_rw;
        int checkTip();
};

#endif
