#include "AllHeader.h"

Serial_k230_Data data; // 定义数据结构体变量,用于存储接收到的数据	Define a data structure variable to store the received data
KEY key1={RCC_APB2Periph_GPIOA, GPIOA, GPIO_Pin_11, GPIO_Mode_IPU};//定义按键1	Define button 1

int main(void)
{	
	uint16_t check_count=1;//防止卡死...
	Key_Init(key1);
	delay_init();
	OLED_Init();
	OLED_ShowString(1,1,"M1:");
	OLED_ShowString(1,10,"D:");
	OLED_ShowString(2,1,"M2:");
	OLED_ShowString(3,1,"M3:");
	OLED_ShowString(4,1,"M4:");
	Serial_k230_Init(); // 初始化K230串口接收
	Serial_Printf("pelase wait...\r\n");
	Servo_init(); // 初始化舵机
	Jdy23_USART3_Init(9600); // 初始化蓝牙串口

	Motor_Init(); // 初始化电机
	Control_Speed(0,0,0,0); // 初始化电机速度为0
	mode_flag=1;
	while(1)
	{
		
		Deal_data_real(); // 处理串口接收的数据
		OLED_ShowSignedNum(1,4,Encoder_Offset[0],4);
		OLED_ShowSignedNum(2,4,-Encoder_Offset[1],4);
		OLED_ShowSignedNum(3,4,-Encoder_Offset[2],4);
		OLED_ShowSignedNum(4,4,Encoder_Offset[3],4);
		if(Key_GetNum(key1)==0)//读取端口
				{
					check_count++;
				}
		OLED_ShowSignedNum(1,13,data.depth,3);
		OLED_ShowString(4,13,"8.23");
		Serial_k230_updateData(&data); // 更新数据
		Serial_k230_toPC(&data); // 将数据发送到PC端，调试用
		Servo_process(&data); // 处理已知data
		//Control_Speed(100,100,100,100);
		Motor_Bluetooth_Mode(); // 蓝牙遥控小车
	}
}
//还有一个time3,变量times用于调试，以下为test部分
// void Car_Move(void)
// {
// 	static uint8_t state = 0;
// 	switch(state)
// 	{
// 		case 0:
// 			forward(400);
// 		break;
// 		case 1:
// 			backward(400);
// 		break;
// 		case 2:
// 			Left_Turn(400);
// 		break;
// 		case 3:
// 			Move(R_Move,400);
// 		break;
// 		case 4:
// 			Motion_State(OFF);
// 		break;
// 	}
// 	state++;
// 	if(state>4)state=0;
// }