//配置定时器2为PWM输出
//chn1->PA0
//chn2->PA1
//chn3->PA2
#include "stm32f10x.h"
#include "Serial_k230.h"
#include "Serial.h"
#include  "PWM.h"
#include  "math.h"
#define FILTER_N 5
static float x_buf[FILTER_N] = {0},
             y_buf[FILTER_N] = {0};
static int x_index = 0, y_index = 0;


float filter_pos(float new_pos, float *pos_buf, int *pos_index)
{
    pos_buf[*pos_index] = new_pos;
    *pos_index = (*pos_index + 1) % FILTER_N;
    float sum = 0;
    for(int i=0; i<FILTER_N; i++) sum += pos_buf[i];
    return sum / FILTER_N;
}

void Servo_init(void)
{
	PWM_init();
}

//0    500    0.5ms    
//180  2500   2.5ms
//分频后为50HZ, 10000
//输出为-90到90度
void Servo_SetAngle_Servo1(float angle)
{
	PWM_Set_Compare_chn1((angle+90)/180 *2000 + 500 );
}

void Servo_SetAngle_Servo2(float angle)
{
	PWM_Set_Compare_chn2((angle+90)/180 *2000 + 500 );
}
/*void Servo_SetPower_Laser(float Power)
{
    PWM_Set_Compare_chn3(Power/180 *2000 + 500);
}*/

void output_to_servo(float output_x, float output_y, int focal_length_pixels,float* Servo_dx, float* Servo_dy)
{
    float angle_x_rad, angle_y_rad;

    angle_x_rad = atan2f(output_x, focal_length_pixels);
    angle_y_rad = atan2f(output_y, focal_length_pixels);

    float angle_x_deg = angle_x_rad * 180.0f / 3.14159f;
    float angle_y_deg = angle_y_rad * 180.0f / 3.14159f;

    // 限幅
    if (angle_x_deg < -80.0f) angle_x_deg = -80.0f;
    if (angle_x_deg > 80.0f)  angle_x_deg = 80.0f;
    if (angle_y_deg < -60.0f) angle_y_deg = -60.0f;
    if (angle_y_deg > 60.0f)  angle_y_deg = 60.0f;

    *Servo_dx = angle_x_deg;
    *Servo_dy = angle_y_deg;
}

void Servo_process(Serial_k230_Data *data)
{
    float output_x = data->x;
    float output_y = data->y;

    // Apply filtering
    output_x = filter_pos(output_x, x_buf, &x_index);
    output_y = filter_pos(output_y, y_buf, &y_index);

    // Convert to servo angles
    float Servo_dx, Servo_dy;
    output_to_servo(output_x, output_y, 463, &Servo_dx, &Servo_dy);

    // Debug output
    Serial_Printf("Servo angles: dx=%.2f, dy=%.2f\n", Servo_dx, Servo_dy);
    // Set servo angles
    //Servo_SetAngle_Servo1(Servo_dx);
    //Servo_SetAngle_Servo2(Servo_dy);
    Servo_SetAngle_Servo1(-65);
    Servo_SetAngle_Servo2(-50);
    
}

