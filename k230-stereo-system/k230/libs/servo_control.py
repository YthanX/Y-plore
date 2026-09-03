from machine import Pin, PWM
from machine import FPIOA
import time
import math

#配置引脚42为PWM0功能,引脚46为PWM2功能
#通道0：GPIO42,通道1：GPIO43,通道2：GPIO46,通道3：GPIO47
fpioa = FPIOA()
fpioa.set_function(42,FPIOA.PWM0)
fpioa.set_function(46,FPIOA.PWM2)
fpioa.set_function(47,FPIOA.PWM3)
#构建激光模块对象
Laser = PWM(3,2000, 0, enable=True)  # 在同一语句下创建和配置PWM,占空比0%
#构建PWM0对象，通道0，频率为50Hz，占空比为0，默认使能输出
S1 = PWM(0, 50, 0, enable=True) # 在同一语句下创建和配置PWM
S2 = PWM(2, 50, 0, enable=True) # 在同一语句下创建和配置PWM 舵机常用控制50Hz频率
#########################################################舵机云台含消抖尝试###################################################################################
'''尝试使用单片机常用方法来消除舵机抖动问题'''#延时法失败
# 

current_angle_x = 0
current_angle_y = 0     # 当前舵机角度
#
#定义pid参数
########################################等会记得导出来
Kp=3.0
Ki=0.03
Kd=0.25
#########################################
last_error_x=0#微分
integral_x=0#积分
last_error_y=0#微分
integral_y=0#积分

limit_integral=3000 # 积分限幅
Servo_dx=0
Servo_dy=0
Servo_data=[0,0]
#初始滤波常数#############################
alpha = 0.5  # 滤波系数，范围在0到1之间，0是完全由前一个值决定，1是完全由当前值决定
smooth_angle_alpha = 0.2  # 平滑角度的系数，0是完全由current决定，1是完全由target决定
# 用于滤波的变量    
filtered_x = None
filtered_y = None
#定义卡尔曼滤波器类,后续学习了在使用吧
class SimpleKalman:
    def __init__(self, Q=0.01, R=1):#  # Q是过程噪声，R是测量噪声都要外传
        self.Q = Q  # 过程噪声
        self.R = R  # 测量噪声
        self.x = None
        self.p = None
    def set(self, Q, R):
        """设置过程噪声和测量噪声"""
        self.Q = Q
        self.R = R
    def filter(self, z):
        if self.x is None:
            self.x = z
            self.p = 1.0
        else:
            # 预测
            self.p = self.p + self.Q
            # 卡尔曼增益
            K = self.p / (self.p + self.R)
            # 更新
            self.x = self.x + K * (z - self.x)
            self.p = (1 - K) * self.p
        return self.x
# 创建卡尔曼滤波器实例
kalman_x = SimpleKalman(Q=0.01, R=1)                # 你可以根据实际情况调整Q和R的值
kalman_y = SimpleKalman(Q=0.01, R=1)                # 你可以根据实际情况调整Q和R的值
#######################滤波函数########################
#1
def smooth_angle(target, current, smooth_angle_alpha=0.2):#这里有参数
    # alpha越小，越平滑
    return smooth_angle_alpha * target + (1 - smooth_angle_alpha) * current
#2
def low_pass_filter(pre_x,pre_y, alpha=0.5):
    global filtered_x, filtered_y
    if filtered_x is None or filtered_y is None:
        filtered_x = pre_x
        filtered_y = pre_y
    else:
        filtered_x = alpha * pre_x + (1 - alpha) * filtered_x
        filtered_y = alpha * pre_y + (1 - alpha) * filtered_y
    return filtered_x, filtered_y
#####################伺服电机angle_占空比duty转换####

def Servo(servo,angle=0):
    servo.duty((angle+90)/180*10+2.5)

# PID控制函数
# error是当前误差，last_error是上一个误差，integral是积分
def pid_control(error,last_error,integral,Kp,Ki,Kd):#last_error是上一个误差
    global limit_integral

    Proportional = Kp *error

    integral += error
    ###############################修改############################
     # 简单粗暴：积分限幅
    if integral > limit_integral:
        integral = limit_integral
    elif integral < -limit_integral:
        integral = -limit_integral
    ################################################################
    integral_sum =Ki * integral #修复：应该是 Ki * integral，不是 Ki * error

    derivative =Kd *(error -last_error)

    output=Proportional+integral_sum+derivative

    last_error=error

    return output,last_error,integral

def output_to_servo(output_x, output_y, focal_length_pixels=463):
    #                    # 摄像头内参（你需要根据实际摄像头调整）

    # 方法1：直接用焦距计算角度（推荐）
    angle_x_rad = math.atan2(output_x, focal_length_pixels)
    angle_y_rad = math.atan2(output_y, focal_length_pixels)

    # 转换为度数
    Servo_dx = int(angle_x_rad * 180 / 3.14159)
    Servo_dy = int(angle_y_rad * 180 / 3.14159)

    # 限制角度范围
    Servo_dx = max(-80, min(80, Servo_dx))
    Servo_dy = max(-60, min(60, Servo_dy))

    return Servo_dx, Servo_dy
################激光控制函数##############################
def laser_control(depth):
    """控制激光强度"""
    min_depth = 10  # 最小深度
    max_depth = 100  # 最大深度
    min_duty = 10  # 最小占空比
    max_duty = 100  # 最大占空比

    if depth < min_depth:
        depth = min_depth
    elif depth > max_depth:
        depth = max_depth

    # 根据深度计算占空比
    duty = min_duty + (max_duty - min_duty) * (depth - min_depth) / (max_depth - min_depth)
    duty = max(min_duty, min(max_duty, duty))  # 限制范围
    Laser.duty(duty)
############################################################################################

def sevro_process (max_res, paint_size,focal_length_pixels,depth=0):
    global last_error_x, integral_x, last_error_y, integral_y, Servo_dx, Servo_dy
    global current_angle_x, current_angle_y
    try:

        filtered_x,filtered_y=low_pass_filter(max_res[0], max_res[1], alpha)
        ###############注意这里并没有兼容各种屏幕，而且我修改了pipiline的逻辑，已经修改分辨率
        error_x=paint_size[0]//2-filtered_x
        error_y=paint_size[1]//2-filtered_y

        output_x,last_error_x,integral_x=pid_control(error_x,last_error_x,integral_x,Kp,Ki,Kd)
        output_y,last_error_y,integral_y=pid_control(error_y,last_error_y,integral_y,Kp,Ki,Kd)
        ###########调试，可以先留着
        print("PID Output X:", output_x, "Y:", output_y,"integral_x:", integral_x, "integral_y:", integral_y)
##########################这里应该使用depth优化一下##################################################################

        #优化1，使用深度信息来调整舵机角度

        #优化2，使用~调整激光强度
        if depth > 0 and max_res:             
            laser_control(depth)  
        else:
            Laser.duty(0)  
        # 计算舵机角度

         # 0计算“偏移角度”
        Servo_dx,Servo_dy=output_to_servo(output_x,output_y, focal_length_pixels)  # 使用实际焦距

        # 1. 计算目标角度
        target_angle_x = current_angle_x + Servo_dx
        target_angle_y = current_angle_y + Servo_dy

       # 2. 用一阶低通滤波让 current_angle_x/y 慢慢靠近 target
        current_angle_x = smooth_angle(target_angle_x, current_angle_x, smooth_angle_alpha)
        current_angle_y = smooth_angle(target_angle_y, current_angle_y, smooth_angle_alpha)

        # 限制范围
        current_angle_x = max(-80, min(80, current_angle_x))
        current_angle_y = max(-60, min(60, current_angle_y))

        Servo(S1,current_angle_x)
        Servo(S2,-current_angle_y)#改变一下方向

        time.sleep_ms(2)  # 减少阻塞时间，避免过长的延迟

        return Servo_dx,Servo_dy
    
    except Exception as e:
        print("Error in servo process:", e)
        return 0, 0
def set_pid(kp, ki, kd, set_alpha=0.5,set_smooth_angle_alpha=0.2,servo_limit=3000,kalman_Q=0.1, kalman_R=1):
    """设置PID参数"""
    global Kp, Ki, Kd,alpha,limit_integral,kalman_x, kalman_y, smooth_angle_alpha
    if not (0 <= kp <= 10 and 0 <= ki <= 1 and 0 <= kd <= 1):
        raise ValueError("PID parameters out of range: Kp should be [0, 10], Ki should be [0, 1], Kd should be [0, 1]")
    if not (0 <= set_smooth_angle_alpha <= 1):
        raise ValueError("Smooth angle alpha should be in the range [0, 1]")
    if not (0 <= alpha <= 1):
        raise ValueError("Alpha should be in the range [0, 1]")
    alpha = set_alpha
    smooth_angle_alpha = set_smooth_angle_alpha
    limit_integral = servo_limit  # 设置积分限幅
    Kp = kp
    Ki = ki
    Kd = kd
    kalman_x.set(kalman_Q, kalman_R)  # 设置卡尔曼滤波器的过程噪声和测量噪声
    kalman_y.set(kalman_R, kalman_R)  # 设置卡尔曼滤波器的过程噪声和测量噪声
    print(f"PID parameters set: Kp={Kp}, Ki={Ki}, Kd={Kd}, Alpha={alpha}, Integral Limit={limit_integral}")

def reset_servos():
    """程序结束时复位舵机到中位"""
    global Servo_dx, Servo_dy,current_angle_x, current_angle_y
    current_angle_x = 0
    current_angle_y = 0
    Servo(S1, -10)  # X轴舵机归零
    Servo(S2, 10)  # Y轴舵机归零
    Servo_dx =-10
    Servo_dy =10




