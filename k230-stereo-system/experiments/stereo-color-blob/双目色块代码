import time, os, sys, gc, math  # 添加math模块
from machine import UART,FPIOA,Pin,Timer
from media.sensor import * #导入sensor模块，使用摄像头相关接口
from media.display import * #导入display模块，使用display相关接口
from media.media import * #导入media模块，使用meida相关接口
#画幅
dispaly_width=1280
display_height=720
###########################双目初始化#################################
#引脚分配
fpioa=FPIOA()
fpioa.set_function(3, FPIOA.UART1_TXD)
fpioa.set_function(4, FPIOA.UART1_RXD)
fpioa.set_function(21,FPIOA.GPIO21)  #将GPIO21映射到引脚GPIO21
fpioa.set_function(52,FPIOA.GPIO52)  #将GPIO52映射到引脚GPIO52

KEY=Pin(21,Pin.IN, Pin.PULL_UP)  #将LED映射到引脚GPIO21
LED=Pin(52,Pin.OUT)  #将LED映射到引脚GPIO52
uart=UART(1,115200,timeout=1000)

LED.value(1)
state=0
show_flag=False
##########硬件参数，双目未改###############

object_width_cm=2.9

#焦距
focal_length_pixels=498

# 颜色识别阈值 (L Min, L Max, A Min, A Max, B Min, B Max) LAB模型
# 下面的阈值元组是用来识别 红、绿、蓝三种颜色，当然你也可以调整让识别变得更好。
thresholds = [(30, 100, 15, 127, 15, 127), # 红色阈值
              (30, 100, -64, -8, -32, 32), # 绿色阈值
              (0, 30, 0, 64, -128, -20)] # 蓝色阈值
############pid算法参数初始,用于控制舵机############
#                                               #
#                                               #
#################定时器初始化######################
def fun(tim):
    global state
    state=1-state
tim =Timer(-1)
tim.init(period=500,mode=Timer.PERIODIC,callback=fun)
#################串口协议######################
HEADER= b'\xAA'  # 帧头

FOOTER = b'\x55'  # 帧尾
def send(data):
    global uart, HEADER, FOOTER
    data = HEADER + data.encode() + FOOTER
    uart.write(data)
    print("send:", data)
#send 频率
send_counter=0

################找最大色块##########################
def find_max_blob(blobs):
    if not blobs:
        return None
    max_blob = None
    max_area = 0
    for blob in blobs:
        if blob.area() > max_area:
            max_area = blob.area()
            max_blob = blob
    return max_blob
#################计算深度，双目未改##########################
def convert_to_depth(Image_width_pixels, object_width_cm, focal_length_pixels):
    # 计算深度
    return (object_width_cm * focal_length_pixels) / Image_width_pixels
#################计算角度，双目未改##########################
#
def convert_to_angle(blob):
    #center_pos = (centroid_sum / weight_sum)  确定直线的中心,这一行用于加权求和，
    #centroid_sum 是Σ Ki*blob.cx(),weight_sum是Σ Ki,目前不需要
    deflection_angle = math.atan((blob.cx()-320)/480) #采用图像为QVGA 320*240时候使用
    #######这里可以用deflection_angle = math.atan((blob.cx()-320)/(480-blob.cy())),
    #计算真实角度，这个只是为了方便调用舵机
    deflection_angle = math.degrees(deflection_angle)#弧度转角度制
    return deflection_angle
######################媒体资源初始###################
#CSI1接口
sensor1 = Sensor(id=1) #构建摄像头对象
sensor1.reset() #复位和初始化摄像头
sensor1.set_framesize(width = 640, height = 480) #设置帧大小
sensor1.set_pixformat(Sensor.RGB565) #设置输出图像格式
#bind_info = sensor1.bind_info(x = 640, y = (display_height-sensor1.height())//2)
#Display.bind_layer(**bind_info, layer = Display.LAYER_OSD1) #输出通道1

#CSI0接口
sensor0 = Sensor(id=0) #构建摄像头对象
sensor0.reset() #复位和初始化摄像头#
sensor0.set_framesize(width = 640, height = 480) #设置帧大小FHD(1920x1080)，默认通道0
sensor0.set_pixformat(Sensor.RGB565) #设置输出图像格式
#bind_info = sensor0.bind_info(x = 0, y =  (display_height-sensor0.height())//2)
#Display.bind_layer(**bind_info, layer = Display.LAYER_OSD0) #输出通道2

#使用IDE缓冲区输出图像,显示尺寸和sensor配置一致。
Display.init(Display.LT9611, width = 1280, height = 720,osd_num=2,to_ide = True)

MediaManager.init() #初始化media资源管理器

sensor1.run() #启动sensor
sensor0.run() #启动sensor

clock = time.clock()

#######################显示核心######################
def show_find_blobs(img0, img1):
    global sensor0,sensor1, show_flag, thresholds, LED, state, send_counter, clock
    if show_flag:
        blobs_0 = img0.find_blobs([thresholds[2]])
        blobs_1 = img1.find_blobs([thresholds[2]])
        ########################这里处理深度和角度#############



        ######################################################
        # 分别处理，避免空指针
        if blobs_0:
            max_blob_0 = find_max_blob(blobs_0)
            if max_blob_0:
                # 降低串口发送频率
                send("左侧色块中心点x:{}, y:{}, 宽度:{}, 高度:{},帧率:{}\n".format(
                    max_blob_0.cx(), max_blob_0.cy(), max_blob_0.w(), max_blob_0.h(),clock.fps()))
                img0.draw_rectangle(max_blob_0.rect())
                img0.draw_cross(max_blob_0.cx(), max_blob_0.cy())
                img0.draw_string_advanced(max_blob_0[0], max_blob_0[1]-35, 30,"左侧",color =(255,0,0))

        if blobs_1:
            max_blob_1 = find_max_blob(blobs_1)
            if max_blob_1:

                send("右侧色块中心点x:{}, y:{}, 宽度:{}, 高度:{},帧率:{}\n".format(
                    max_blob_1.cx(), max_blob_1.cy(), max_blob_1.w(), max_blob_1.h(),clock.fps()))
                img1.draw_rectangle(max_blob_1.rect())
                img1.draw_cross(max_blob_1.cx(), max_blob_1.cy())
                img1.draw_string_advanced(max_blob_1[0], max_blob_1[1]-35, 30,"右侧",color =(255,0,0))

        send_counter+=1
        # LED控制
        if blobs_0 or blobs_1:
            LED.value(state)
        else:
            LED.value(0)
    else:
        pass
try:
    while True:
        clock.tick()
        img0=sensor0.snapshot()  # 获取一帧图像
        img1=sensor1.snapshot()  # 获取一帧图像
        if not KEY.value():
            time.sleep_ms(10)
            if not KEY.value():
                show_flag=not show_flag
                while not KEY.value():
                    pass
        show_find_blobs(img0,img1)
        Display.show_image(img0, x=0, y= (display_height-sensor0.height())//2, layer=Display.LAYER_OSD0) #显示左侧图片
        Display.show_image(img1, x=640, y= (display_height-sensor1.height())//2, layer=Display.LAYER_OSD1) #显示右侧图片
        time.sleep_ms(2)  # 增加延时
        gc.collect()  # 定期回收内存
except Exception as e:
    print("error occurred:", e)
finally:
        # 每个 sensor 都需要执行 stop
    if isinstance(sensor0, Sensor):
      sensor0.stop()
    if isinstance(sensor1, Sensor):
      sensor1.stop()
    # 销毁显示
    Display.deinit()
    os.exitpoint(os.EXITPOINT_ENABLE_SLEEP)
    time.sleep_ms(100)
    # 释放媒体缓冲区
    MediaManager.deinit()

