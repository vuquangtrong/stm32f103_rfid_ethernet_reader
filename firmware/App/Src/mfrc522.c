#include "mfrc522.h"
#include "main.h"

#define mfrc522_select()  RFID_NSS_GPIO_Port->BSRR = GPIO_BSRR_BR4
#define mfrc522_release() RFID_NSS_GPIO_Port->BSRR = GPIO_BSRR_BS4
#define mfrc522_reset()   RFID_RST_GPIO_Port->BSRR = GPIO_BSRR_BR11
#define mfrc522_set()     RFID_RST_GPIO_Port->BSRR = GPIO_BSRR_BS11

static void mfrc522_spi_init() {
    LL_SPI_Enable(SPI1);
    mfrc522_release();
    mfrc522_reset();
}

static uint8_t mfrc522_spi_rw(uint8_t data) {
    while (!(LL_SPI_IsActiveFlag_TXE(SPI1)))
        ;
    LL_SPI_TransmitData8(SPI1, data);
    while (!(LL_SPI_IsActiveFlag_RXNE(SPI1)))
        ;
    return LL_SPI_ReceiveData8(SPI1);
}

static void mfrc522_delay(uint32_t ms) {
    LL_mDelay(ms);
}

// Generic SPI read command
static uchar mfrc522_read_byte(uchar addr) {
    uchar val;
    mfrc522_select();
    mfrc522_spi_rw(((addr << 1) & 0x7E) | 0x80);
    val = mfrc522_spi_rw(0x00);
    mfrc522_release();
    return val;
}

// Generic SPI write command
static void mfrc522_write_byte(uchar addr, uchar val) {
    mfrc522_select();
    mfrc522_spi_rw((addr << 1) & 0x7E);
    mfrc522_spi_rw(val);
    mfrc522_release();
}

/*
 * Internal functions
 */

static void mfrc522_set_bit_mask(uchar reg, uchar mask) {
    uchar tmp = mfrc522_read_byte(reg);
    mfrc522_write_byte(reg, tmp | mask);
}

static void mfrc522_clear_bit_mask(uchar reg, uchar mask) {
    uchar tmp = mfrc522_read_byte(reg);
    mfrc522_write_byte(reg, tmp & (~mask));
}

static void mfrc522_antenna_on(void) {
    mfrc522_set_bit_mask(TxControlReg, 0x03);
}

static void mfrc522_antenna_off(void) {
    mfrc522_clear_bit_mask(TxControlReg, 0x03);
}

// Return 2-byte CRC
static void mfrc522_calc_crc(uchar *pIndata, uchar len, uchar *pOutData) {
    mfrc522_clear_bit_mask(DivIrqReg, 0x04);  // CRCIrq = 0
    mfrc522_set_bit_mask(FIFOLevelReg, 0x80); // Clear the FIFO pointer

    // Writing data to the FIFO
    for (int i = 0; i < len; i++) {
        mfrc522_write_byte(FIFODataReg, *(pIndata + i));
    }
    mfrc522_write_byte(CommandReg, PCD_CALCCRC);

    // Wait CRC calculation is complete
    uchar wait = 0xFF;
    uchar irq = 0x00;
    do {
        irq = mfrc522_read_byte(DivIrqReg);
        wait--;
    } while ((wait != 0) && !(irq & 0x04)); // CRCIrq = 1

    // Read CRC calculation result
    pOutData[0] = mfrc522_read_byte(CRCResultRegL);
    pOutData[1] = mfrc522_read_byte(CRCResultRegH);
}

// ISO14443 communication
static uchar mfrc522_talk_to_card(uchar command, uchar *sendData, uchar sendBytes, uchar *recvData, uint *recvBits) {
    uchar irqEn = 0x00;
    uchar waitIrq = 0x00;

    switch (command) {
    case PCD_AUTHENT: // Certification cards close
    {
        irqEn = 0x12;
        waitIrq = 0x10;
        break;
    }
    case PCD_TRANSCEIVE: // Transmit FIFO data
    {
        irqEn = 0x77;
        waitIrq = 0x30;
        break;
    }
    default:
        break;
    }

    // Prepare the command
    mfrc522_write_byte(CommIEnReg, irqEn | 0x80); // Interrupt request
    mfrc522_clear_bit_mask(CommIrqReg, 0x80);     // Clear all interrupt request bit
    mfrc522_set_bit_mask(FIFOLevelReg, 0x80);     // FlushBuffer=1, FIFO Initialization
    mfrc522_write_byte(CommandReg, PCD_IDLE);     // NO action; Cancel the current command

    // Writing data to the FIFO
    for (int i = 0; i < sendBytes; i++) {
        mfrc522_write_byte(FIFODataReg, sendData[i]);
    }

    // Execute the command
    mfrc522_write_byte(CommandReg, command);
    if (command == PCD_TRANSCEIVE) {
        mfrc522_set_bit_mask(BitFramingReg, 0x80); // StartSend=1, transmission of data starts
    }

    // Waiting to receive data to complete
    uint wait = 2000; // M1 card maximum waiting time is 25ms
    uchar irq;
    do {
        // CommIrqReg[7..0]
        // Set1 TxIRq RxIRq IdleIRq HiAlerIRq LoAlertIRq ErrIRq TimerIRq
        irq = mfrc522_read_byte(CommIrqReg);
        wait--;
    } while ((wait != 0) && !(irq & 0x01) && !(irq & waitIrq));

    // Stop execution
    mfrc522_clear_bit_mask(BitFramingReg, 0x80); // StartSend=0

    uchar ret = MI_ERR;
    // If not timeout
    if (wait > 0) {
        if (!(mfrc522_read_byte(ErrorReg) & 0x1B)) // BufferOvfl CollErr CRCErr ProtecolErr
        {
            ret = MI_OK;
            if (irq & irqEn & 0x01) {
                ret = MI_NOTAGERR;
            }

            if (command == PCD_TRANSCEIVE) {
                uchar len = mfrc522_read_byte(FIFOLevelReg);
                uchar lastBits = mfrc522_read_byte(ControlReg) & 0x07;
                if (lastBits) {
                    *recvBits = (len - 1) * 8 + lastBits;
                } else {
                    *recvBits = len * 8;
                }

                if (len == 0) {
                    len = 1;
                }
                if (len > MF_BLOCK_SIZE) {
                    len = MF_BLOCK_SIZE;
                }

                // Reading the received data in FIFO
                for (int i = 0; i < len; i++) {
                    recvData[i] = mfrc522_read_byte(FIFODataReg);
                }
            }
        } else {
            ret = MI_ERR;
        }
    }

    // mfrc522_set_bit_mask(ControlReg,0x80);           //timer stops
    mfrc522_write_byte(CommandReg, PCD_IDLE);

    return ret;
}

// Reset the MFRC522 using the soft reset function
static void mfrc522_soft_reset(void) {
    mfrc522_write_byte(CommandReg, PCD_RESETPHASE);
    mfrc522_delay(1000);
}

// Initialize the MFRC522
void mfrc522_init(void) {
    /* init bus */
    mfrc522_spi_init();
    mfrc522_delay(100);

    /* turn on mfrc522 */
    mfrc522_release();
    mfrc522_set();
    mfrc522_delay(100);

    /* reset mfrc522 */
    mfrc522_soft_reset();
    mfrc522_delay(100);

    /* antenna off */
    mfrc522_antenna_off();
    mfrc522_delay(100);

    /* configure registers */
    mfrc522_write_byte(ModeReg, 0x3D);       // CRC Initial value 0x6363
    mfrc522_write_byte(DemodReg, 0x5D);      // AddIQ = b01; FixIQ = b0; TPrescalEven = b1
    mfrc522_write_byte(RFCfgReg, 0x70);      // set Rx Gain at 48dB
    mfrc522_write_byte(TxASKReg, 0x40);      // force 100% ASK modulation
    mfrc522_write_byte(TModeReg, 0x8D);      // TAuto = b1; TGate = b00; TAutoRestart=b0; TPreScalerHi= 0xD
    mfrc522_write_byte(TPrescalerReg, 0x3D); // TPreScaler =  TPreScalerHi:TPreScalerLo = 0xD3D = 3389
                                             // TPrescalEven = 1 in DemodReg
                                             // ftimer = 13.56 MHz / (2*TPreScaler+2) = 13.56 MHz / (2*3389 + 2) = 2000 Hz
    mfrc522_write_byte(TReloadRegL, 0x30);   // 48 / 2000 = 24 ms
    mfrc522_write_byte(TReloadRegH, 0x00);
    mfrc522_delay(100);

    mfrc522_antenna_on();
    mfrc522_delay(100);
}

// Find cards, read the card type number
//   TagType - Return Card Type
//    0x4400 = Mifare_UltraLight
//    0x0400 = Mifare_One(S50)
//    0x0200 = Mifare_One(S70)
//    0x0800 = Mifare_Pro(X)
//    0x4403 = Mifare_DESFire
uchar mfrc522_request(uchar reqMode, uchar *TagType) {
    uchar status;
    uint recvBits;

    mfrc522_write_byte(BitFramingReg, 0x07); // TxLastBists = BitFramingReg[2..0]

    TagType[0] = reqMode;
    status = mfrc522_talk_to_card(PCD_TRANSCEIVE, TagType, 1, TagType, &recvBits);

    if ((status != MI_OK) || (recvBits != 0x10 /* 2 Bytes */)) {
        status = MI_ERR;
    }

    return status;
}

// Anti-collision detection, reading selected card serial number card
uchar mfrc522_anti_collision(uchar *serNum) {
    uchar status;
    uint recvBits;

    mfrc522_write_byte(BitFramingReg, 0x00); // TxLastBists = BitFramingReg[2..0]

    serNum[0] = PICC_ANTICOLL;
    serNum[1] = 0x20; // 32-bit serial number
    status = mfrc522_talk_to_card(PCD_TRANSCEIVE, serNum, 2, serNum, &recvBits);

    if (status == MI_OK) {
        uchar serNumCheck = 0;
        // Check card serial number
        for (int i = 0; i < 4; i++) {
            serNumCheck ^= serNum[i];
        }
        if (serNumCheck != serNum[4]) {
            status = MI_ERR;
        }
    }

    return status;
}

// Select the card, read the card storage capacity
uchar mfrc522_select_tag(uchar *serNum) {
    uchar buffer[9];
    uchar status;
    uint recvBits;

    // mfrc522_clear_bit_mask(Status2Reg, 0x08);			//MFCrypto1On=0

    buffer[0] = PICC_SElECTTAG;
    buffer[1] = 0x70;
    for (int i = 0; i < 5; i++) {
        buffer[i + 2] = *(serNum + i);
    }
    mfrc522_calc_crc(buffer, 7, &buffer[7]);
    status = mfrc522_talk_to_card(PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits);

    uchar size;
    if ((status == MI_OK) && (recvBits == 0x18 /* 3 Bytes */)) {
        size = buffer[0];
    } else {
        size = 0;
    }

    return size;
}

// Verify card password
//  authMode - Password Authentication Mode (0x60: Authentication A; 0x61: Authentication B)
//  BlockAddr - Block address
//  Sectorkey - Sector password
//  serNum - Card serial number, 4-byte
uchar mfrc522_auth(uchar authMode, uchar BlockAddr, uchar *Sectorkey, uchar *serNum) {
    uchar buff[12];
    uchar status;
    uint recvBits;

    // Verify the command block address + sector + password + card serial number
    buff[0] = authMode;
    buff[1] = BlockAddr;
    for (int i = 0; i < 6; i++) {
        buff[i + 2] = *(Sectorkey + i);
    }
    for (int i = 0; i < 4; i++) {
        buff[i + 8] = *(serNum + i);
    }
    status = mfrc522_talk_to_card(PCD_AUTHENT, buff, 12, buff, &recvBits);

    if ((status != MI_OK) || (!(mfrc522_read_byte(Status2Reg) & 0x08))) {
        status = MI_ERR;
    }

    return status;
}

// Read a block of data, maximum 16 bytes + 2-byte CRC
uchar mfrc522_read_block(uchar blockAddr, uchar *recvData) {
    uchar status;
    uint recvBits;

    recvData[0] = PICC_READ;
    recvData[1] = blockAddr;
    mfrc522_calc_crc(recvData, 2, &recvData[2]);
    status = mfrc522_talk_to_card(PCD_TRANSCEIVE, recvData, 4, recvData, &recvBits);

    if ((status != MI_OK) || (recvBits != 0x90 /* 18 Bytes */)) {
        status = MI_ERR;
    }

    return status;
}

// Write a block of data, maximum 16 bytes
uchar mfrc522_write_block(uchar blockAddr, uchar *writeData) {
    uchar buff[18];
    uchar status;
    uint recvBits;

    buff[0] = PICC_WRITE;
    buff[1] = blockAddr;
    mfrc522_calc_crc(buff, 2, &buff[2]);
    status = mfrc522_talk_to_card(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);

    if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) {
        status = MI_ERR;
    }

    if (status == MI_OK) {
        for (int i = 0; i < 16; i++) // Data to the FIFO write 16Byte
        {
            buff[i] = *(writeData + i);
        }
        mfrc522_calc_crc(buff, 16, &buff[16]);
        status = mfrc522_talk_to_card(PCD_TRANSCEIVE, buff, 18, buff, &recvBits);

        if ((status != MI_OK) || (recvBits != 4) || ((buff[0] & 0x0F) != 0x0A)) {
            status = MI_ERR;
        }
    }

    return status;
}

// Tell card go into hibernation
void mfrc522_halt(void) {
    uchar buff[4];
    uint recvBits;

    buff[0] = PICC_HALT;
    buff[1] = 0;
    mfrc522_calc_crc(buff, 2, &buff[2]);
    mfrc522_talk_to_card(PCD_TRANSCEIVE, buff, 4, buff, &recvBits);
}
