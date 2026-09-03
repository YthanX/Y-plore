#ifndef __SERIAL_K230__H
#define __SERIAL_K230__H

typedef struct
{
    uint8_t length;      // 数据包长度
    uint8_t header[2];  // 数据帧头和尾，为0xFF, 0xFE
    uint16_t x;         // 目标矩形中心点x
    uint16_t y;         // 目标矩形中心点y
    uint16_t width;     // 目标矩形宽度
    uint16_t height;    // 目标矩形高度
    float depth;        // 目标矩形深度
    float angle;        // 目标矩形角度 
} Serial_k230_Data;

extern uint8_t Serial_k230_RECEIVE_FLAG;
void Serial_k230_Init(void);
void Serial_k230_toPC(Serial_k230_Data *data);
void Serial_k230_updateData(Serial_k230_Data *data);
#endif