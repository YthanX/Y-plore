#include "stm32f10x.h"                  // Device header
#include "Serial.h"
#include "string.h"
typedef struct
{
	uint8_t length;     			// 数据包长度
	uint8_t header[2];  			// 数据帧头和尾，为0xFF, 0xFE
	uint16_t x;        				// 目标矩形中心点x
	uint16_t y;        				// 目标矩形中心点y
	uint16_t width;     			// 目标矩形宽度
	uint16_t height;    			// 目标矩形高度
	float depth;       				// 目标矩形深度
	float angle;       				// 目标矩形角度 
}Serial_k230_Data;

static char str[RX_BUF_SIZE]; // 用于存储接收到的字符串
uint8_t Serial_k230_Rxpacket[RX_BUF_SIZE]; // 接收数据包
uint8_t Serial_k230_RECEIVE_FLAG = 0; // 接收标志位

void Serial_k230_Init(void)
{
	Serial_init(Serial_k230_Rxpacket , RX_BUF_SIZE); // 初始化串口接收	
}

void Serial_k230_updateData(Serial_k230_Data *data)
{
	//Serial_DMA_transfer();
	
	if (Serial_k230_Rxpacket[0] == Header && Serial_k230_Rxpacket[RX_BUF_SIZE-1] == Footer)
        {
            // 复制中间内容到str，去掉包头包尾，长度
            memcpy(str, &Serial_k230_Rxpacket[2], RX_BUF_SIZE-3);
            str[RX_BUF_SIZE - 3 ] = '\0'; // 字符串结尾
            // 解析数据
			data->length = Serial_k230_Rxpacket[1]; // 获取数据包长度
            //显示搬运数据
            sscanf(str, "目标矩形中心点x:%d,y:%d,宽度:%d,高度:%d,深度:%f,角度:%f",
                &data->x, &data->y, &data->width, &data->height, &data->depth, &data->angle);
        }
	
}

void Serial_k230_toPC(Serial_k230_Data *data)
{
    Serial_Printf("length=%d,x=%d, y=%d, width=%d, height=%d, depth=%.2f, angle=%.2f\n",
        data->length,data->x, data->y, data->width, data->height, data->depth, data->angle);
}
//失败
void USART1_IRQHandler(void)
{
	if(USART_GetITStatus(USART1, USART_IT_IDLE) != RESET) //如果空闲中断发生
	{
		volatile uint32_t temp;
        temp = USART1->SR;
        temp = USART1->DR;
        Serial_k230_RECEIVE_FLAG = 1; // 有新数据包
	}
}

