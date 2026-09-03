#include "stm32f10x.h"                  // Device header
#include "app_motor_usart.h"
#include "myenum.h"
#include "delay.h"
#include "bsp.h"
#include "bsp_motor_usart.h"
#include "app_motor_usart.h"
#include "bsp_timer.h"
#include "Motor.h"
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
u8 Joy_RxBuf[20];//摇杆接收数据缓冲区
u8 MPU_RxBuf[10];//陀螺仪接收数据缓冲区
u8 Joy_Lpos,Joy_Rpos;//摇杆坐标数据
u8 MPU_Data[10];//MPU接收数据滤波数组
u8 Con_RxBuf[10];//控制指令接收数组
u8 Eva_RxBuf[4];//避障停止指令数组
u8 RGB_RxBuf[4];//彩灯停止指令数组
u8 mode=1;//模式，1默认为蓝牙模式
extern u8 mode_flag;
u8 NRF_flag=0;//遥控模式判断循环退出标志位
u8 MPU_flag=0;//重力模式判断循环退出标志位

u8 RGB_flag=0;//RGB彩灯标志位
u8 RGB_mode=0;//RGB彩灯模式
int HSV_H=0;//HSV的H数值
u8 HSV_flag=0;//颜色转换时所用标志位
u8 LED_Count=0;//LED灯的个数

extern char Lx_Buf[10];//串口接收摇杆数据缓冲区
extern char Rx_Buf[10];//串口接收摇杆数据缓冲区
extern char Pitch_Roll_Buf[20];//串口重力感应

#define UPLOAD_DATA 2  //0:不接受数据 1:接收总的编码器数据 2:接收实时的编码器 3:接收电机当前速度 mm/s
					   //0: Do not receive data 1: Receive total encoder data 2: Receive real-time encoder 3: Receive current motor speed mm/s

#define MOTOR_TYPE 3   //1:520电机 2:310电机 3:测速码盘TT电机 4:TT直流减速电机 5:L型520电机
                       //1:520 motor 2:310 motor 3:speed code disc TT motor 4:TT DC reduction motor 5:L type 520 motor

uint8_t times = 0;
void Motor_Setting(void)
{
	//发送PID参数	Send PID parameters
	#if MOTOR_TYPE == 1
	send_motor_type(1);
	delay_ms(100);
	send_pulse_phase(40);
	delay_ms(100);
	send_pulse_line(11);
	delay_ms(100);
	send_wheel_diameter(67.00);
	delay_ms(100);
	send_motor_deadzone(1900);
	delay_ms(100);
	
	#elif MOTOR_TYPE == 2
	send_motor_type(2);
	delay_ms(100);
	send_pulse_phase(20);
	delay_ms(100);
	send_pulse_line(13);
	delay_ms(100);
	send_wheel_diameter(48.00);
	delay_ms(100);
	send_motor_deadzone(1600);
	delay_ms(100);
	
	#elif MOTOR_TYPE == 3
	send_motor_type(3);
	delay_ms(100);
	send_pulse_phase(90);
	delay_ms(100);
	send_pulse_line(500);//gmr500线编码
	delay_ms(100);
	send_wheel_diameter(60.00);
	delay_ms(100);
	send_motor_deadzone(1650);
	delay_ms(100);
	
	#elif MOTOR_TYPE == 4
	send_motor_type(4);
	delay_ms(100);
	send_pulse_phase(48);
	delay_ms(100);
	send_motor_deadzone(1000);
	delay_ms(100);
	
	#elif MOTOR_TYPE == 5
	send_motor_type(1);
	delay_ms(100);
	send_pulse_phase(40);
	delay_ms(100);
	send_pulse_line(11);
	delay_ms(100);
	send_wheel_diameter(67.00);
	delay_ms(100);
	send_motor_deadzone(1900);
	delay_ms(100);
	#endif
	
	//给电机模块发送需要上报的数据	Send the data that needs to be reported to the motor module
	#if UPLOAD_DATA == 1
	send_upload_data(true,false,false);delay_ms(10);
	#elif UPLOAD_DATA == 2
	send_upload_data(false,true,false);delay_ms(10);
	#elif UPLOAD_DATA == 3
	send_upload_data(false,false,true);delay_ms(10);
	#endif
}

void Motor_Init(void)
{
	bsp_init();
	TIM3_Init();
	Motor_Usart_init();
	Control_Pwm(0,0,0,0);
	delay_ms(100);
	//先关闭上报	Close the report first
	send_upload_data(false,false,false);delay_ms(10);
	Motor_Setting();
}

void Motor_Data_Printf(void)
{
	if(g_recv_flag == 1)
{
	g_recv_flag = 0;
	
	#if UPLOAD_DATA == 1
		Deal_data_real();
		Serial_Printf("M1:%d,M2:%d,M3:%d,M4:%d\r\n",Encoder_Now[0],Encoder_Now[1],Encoder_Now[2],Encoder_Now[3]);
	#elif UPLOAD_DATA == 2
		Deal_data_real();
		Serial_Printf("M1:%d,M2:%d,M3:%d,M4:%d\r\n",Encoder_Offset[0],Encoder_Offset[1],Encoder_Offset[2],Encoder_Offset[3]);/*Encoder_Offset[2]*//*Encoder_Offset[3]*/
	#elif UPLOAD_DATA == 3
		Deal_data_real();
		Serial_Printf("M1:%.2f,M2:%.2f,M3:%.2f,M4:%.2f\r\n",g_Speed[0],g_Speed[1],g_Speed[2],g_Speed[3]);
	#endif
}
}
/**************************************************
函数名称：forward(u16 speed)
函数功能：小车前进
入口参数：speed  0-500
返回参数：无
***************************************************/
void forward(u16 speed)
{
	// TIM_SetCompare1(TIM2,500-speed);//R_AIN2:右下:3
	// R_AIN2_ON;
	
	// TIM_SetCompare2(TIM2,speed);//R_BIN2:左下:4
	// R_BIN2_OFF;
	
	// TIM_SetCompare3(TIM2,speed);//L_AIN2:右上:2
	// L_AIN2_OFF;
	
	// TIM_SetCompare4(TIM2,500-speed);//L_BIN2:左上:1
  	// L_BIN2_ON;
	Control_Speed(speed,speed,speed,speed);
}


/**************************************************
函数名称：backward(u16 speed)
函数功能：小车后退
入口参数：speed  0-500
返回参数：无
***************************************************/
void backward(u16 speed)
{
// 	TIM_SetCompare1(TIM2,speed);//R_AIN2:右下
// 	R_AIN2_OFF;
	
// 	TIM_SetCompare2(TIM2,500-speed);//R_BIN2:左下
// 	R_BIN2_ON;
	
// 	TIM_SetCompare3(TIM2,500-speed);//L_AIN2:右上
// 	L_AIN2_ON;
	
// 	TIM_SetCompare4(TIM2,speed);//L_BIN2:左上
//   L_BIN2_OFF;	
	Control_Speed(-speed,-speed,-speed,-speed);
}


/**************************************************
函数名称：Left_Turn(u16 speed)
函数功能：小车左转
入口参数：speed  0-500
返回参数：无
***************************************************/
void Left_Turn(u16 speed)
{
// 	TIM_SetCompare1(TIM2,500-speed);//R_AIN2:右下
// 	R_AIN2_ON;
	
// 	TIM_SetCompare2(TIM2,500-speed);//R_BIN2:左下
// 	R_BIN2_ON;
	
// 	TIM_SetCompare3(TIM2,speed);//L_AIN2:右上
// 	L_AIN2_OFF;
	
// 	TIM_SetCompare4(TIM2,speed);//L_BIN2:左上
//   L_BIN2_OFF;	
	Control_Speed(speed,speed,-speed,-speed);
}


/**************************************************
函数名称：Right_Turn(u16 speed)
函数功能：小车右转
入口参数：speed  0-500
返回参数：无
***************************************************/
void Right_Turn(u16 speed)
{
	Control_Speed(-speed,-speed,speed,speed);
}

/************************************************************************
函数名称：Move(u16 Dir,u16 speed)
函数功能：小车平移
入口参数：Dir 平移方向(L_Move R_Move L_U_Move L_D_Move R_U_Move L_D_Move)
					方向 speed  0-500
返回参数：无
*********************************************************&&*************/
void Move(u16 Dir,u16 speed)
{
	if(Dir==0)//左移
	{
		Control_Speed(-speed,-speed,speed,speed);
	}
	else if(Dir==1)//右移
	{
		Control_Speed(speed,speed,-speed,-speed);
	}
	else if(Dir==2)//左上移动
	{
		Control_Speed(0,-speed,speed,0);
	}
	else if(Dir==3)//右上移动
	{
		Control_Speed(speed,0,0,-speed);
	}
	else if(Dir==4)//左下移动
	{
		Control_Speed(-speed,0,0,speed);
	}
	else if(Dir==5)//右下移动
	{
		Control_Speed(0,speed,-speed,0);
	}
}


/**************************************************
函数名称：Motion_State(u16 mode)
函数功能：小车关闭及打开
入口参数：mode (ON OFF)
返回参数：无
***************************************************/
void Motion_State(u16 mode)
{
	if(mode==6)
	{
		Control_Speed(0,0,0,0);
	}
	else if(mode==7)
	{
		Control_Speed(400,400,400,400);
	}
}

/*返回参数：映射后的数值
***************************************************/
int Map(int val,int in_min,int in_max,int out_min,int out_max)
{
	return (int)(val-in_min)*(out_max-out_min)/(in_max-in_min)+out_min;
}

void Motor_Map_process(int *speed1,int *speed2,int *speed3,int *speed4)
{
	*speed1=Map(*speed1,-127,127,-399,399);
	*speed2=Map(*speed2,-127,127,-399,399);	
	*speed3=Map(*speed3,-127,127,-399,399);
	*speed4=Map(*speed4,-127,127,-399,399);

	if(*speed1<20 && *speed1>-20)*speed1=0;
	if(*speed2<20 && *speed2>-20)*speed2=0;
	if(*speed3<20 && *speed3>-20)*speed3=0;
	if(*speed4<20 && *speed4>-20)*speed4=0;

	if(*speed1>399)*speed1=399;
	if(*speed2>399)*speed2=399;	
	if(*speed3>399)*speed3=399;
	if(*speed4>399)*speed4=399;

	if(*speed1<-399)*speed1=-399;
	if(*speed2<-399)*speed2=-399;
	if(*speed3<-399)*speed3=-399;
	if(*speed4<-399)*speed4=-399;
}
/**************************************************
函数名称：Bluetooth_Mode(void)
函数功能：蓝牙遥控模式
入口参数：无
返回参数：无
***************************************************/
void Motor_Bluetooth_Mode(void)
{
	if(mode_flag==1)
	{
		APP_Joy_Mode();//APP摇杆模式
	}
	else if(mode_flag==2)APP_Gravity_Mode();//APP重力模式
	else if(mode_flag==3)return;
	else if(mode_flag==4)return;
	else if(mode_flag==5)return;//呼吸灯
	else if(mode_flag==6)return;//流水灯
	else if(mode_flag==7)return;//闪烁灯
	else Motion_State(ON);
}
/**************************************************
函数名称：Joy_Mode(void)
函数功能：蓝牙模式的摇杆遥控
入口参数：无
返回参数：无
***************************************************/
#include "Serial.h"//debug
void APP_Joy_Mode(void)
{
	int Joy_Lx=50, Joy_Ly = 50, Joy_Rx = 50, Joy_Ry = 50;
	int Map_Lx, Map_Ly, Map_Rx, Map_Ry;
	int speed1, speed2, speed3, speed4;
	
	if (Lx_Buf[0] == 'L')
	{
		Joy_Lx = (Lx_Buf[2] - '0') * 10 + (Lx_Buf[3] - '0');
		Joy_Ly = (Lx_Buf[5] - '0') * 10 + (Lx_Buf[6] - '0');
	}
	if (Rx_Buf[0] == 'R')
	{
		Joy_Rx = (Rx_Buf[2] - '0') * 10 + (Rx_Buf[3] - '0');
		Joy_Ry = (Rx_Buf[5] - '0') * 10 + (Rx_Buf[6] - '0');
	}
	
	Map_Lx = Map(Joy_Lx, 10, 90, -127, 127);
	Map_Ly = Map(Joy_Ly, 10, 90, -127, 127);
	Map_Rx = Map(Joy_Rx, 10, 90, -127, 127);
	Map_Ry = Map(Joy_Ry, 10, 90, -127, 127);

	
	speed1 = -Map_Ly + Map_Lx - Map_Ry + Map_Rx;
	speed2 = -Map_Ly - Map_Lx - Map_Ry - Map_Rx;
	speed3 = -Map_Ly + Map_Lx - Map_Ry - Map_Rx;
	speed4 = -Map_Ly - Map_Lx - Map_Ry + Map_Rx;


	Motor_Map_process(&speed1,&speed2,&speed3,&speed4);

	Control_Speed(speed1,speed2,speed3,speed4);

	delay_ms(10);
	Serial_Printf("spd:%d,%d,%d,%d\r\n",speed1,speed2,speed3,speed4);//debug
	Serial_Printf("Lx:%d,Ly:%d,Rx:%d,Ry:%d\r\n",Joy_Lx,Joy_Ly,Joy_Rx,Joy_Ry);//debug
//	printf(Rx_Buf);
//	printf(Rx_Buf);
//	printf("\n");
}

void APP_Gravity_Mode(void)
{
	int i,j=0,Pitch_flag=0;
	int APP_Pitch=0,APP_Roll=0;
	int Pitch_symbel=1,Roll_symbel=1;//偏航角符号
	char Pitch_Buf[10],Roll_Buf[10];
	int Map_pitch, Map_roll;//映射后的偏航角
	int speed1, speed2, speed3, speed4;
	static int Smoothing_Pitch_Buf[5];//中值滤波数组
	static int Smoothing_Roll_Buf[5];//中值滤波数组
	static int Smoothing_Count=0;//中值滤波采样个数
	int Pitch_temp,Roll_temp;//选择排序变量
	
	//提取Roll
	for(i=1;i<20;i++)
	{
		if(Pitch_Roll_Buf[i]=='.')break;
		Roll_Buf[i-1]=Pitch_Roll_Buf[i];
	}
	//提取Pitch
	for(i=0;i<20;i++)
	{
		if(Pitch_Roll_Buf[i]==',')
		{
			Pitch_flag=1;
			i++;
		}
		if(Pitch_flag==1)
		{
			if(Pitch_Roll_Buf[i]=='.')
			{
				j=0;
				break;
			}
			Pitch_Buf[j]=Pitch_Roll_Buf[i];
			j++;
		}
	}
	//将Roll字符串转换为整形数据
	j=0;
	for(i=10;i>=0;i--)
	{
		if(Roll_Buf[i]>='0'&&Roll_Buf[i]<='9')
		{
			APP_Roll+=(Roll_Buf[i]-'0')*pow(10,j);
			j++;
		}
		if(Roll_Buf[0]=='-')
		{
			Roll_symbel=-1;
		}
	}
	//将Pitch字符串转换为整形数据
	j=0;
	for(i=10;i>=0;i--)
	{
		if(Pitch_Buf[i]>='0'&&Pitch_Buf[i]<='9')
		{
			APP_Pitch+=(Pitch_Buf[i]-'0')*pow(10,j);
			j++;
		}
		if(Pitch_Buf[0]=='-')
		{
			Pitch_symbel=-1;
		}
	}
	//得到整形偏航角数据
	APP_Pitch=Pitch_symbel*APP_Pitch;
	APP_Roll=Roll_symbel*APP_Roll;
	//采样五次
	Smoothing_Pitch_Buf[Smoothing_Count]=APP_Pitch;
	Smoothing_Roll_Buf[Smoothing_Count]=APP_Roll;
	Smoothing_Count++;
	//选择排序
	if(Smoothing_Count==5)
	{
		Smoothing_Count=0;
		
		for(j = 0; j < 5 - 1; j++) 
		{
        for(i = 0; i < 5 - j; i++) 
				{
            if(Smoothing_Pitch_Buf[i] > Smoothing_Pitch_Buf[i + 1]) 
						{
                Pitch_temp = Smoothing_Pitch_Buf[i];
                Smoothing_Pitch_Buf[i] = Smoothing_Pitch_Buf[i + 1];
                Smoothing_Pitch_Buf[i + 1] = Pitch_temp;
            }
						if(Smoothing_Roll_Buf[i] > Smoothing_Roll_Buf[i + 1]) 
						{
                Roll_temp = Smoothing_Roll_Buf[i];
                Smoothing_Roll_Buf[i] = Smoothing_Roll_Buf[i + 1];
                Smoothing_Roll_Buf[i + 1] = Roll_temp;
            }			
        }
    }
		//中值滤波
		APP_Pitch=Smoothing_Pitch_Buf[2];
		APP_Roll=Smoothing_Roll_Buf[2];
		
		Map_pitch = Map(APP_Pitch, -90, 90, -127, 127);
		Map_roll = Map(APP_Roll, -90, 90, -127, 127);
					
		speed1 =  -Map_pitch + Map_roll;
		speed2 =  -Map_pitch - Map_roll;
		speed3 =  -Map_pitch + Map_roll;
		speed4 =  -Map_pitch - Map_roll;
		
		Motor_Map_process(&speed1,&speed2,&speed3,&speed4);
				
		Control_Speed(speed1,speed2,speed3,speed4);

		memset(Smoothing_Pitch_Buf,0,sizeof(Smoothing_Pitch_Buf));
		memset(Smoothing_Roll_Buf,0,sizeof(Smoothing_Roll_Buf));
		delay_ms(1);	
	}
	
	memset(Roll_Buf,0,10);
	memset(Pitch_Buf,0,10);
	
	delay_ms(1);	
}

