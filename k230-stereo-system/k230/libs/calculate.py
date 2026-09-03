from libs.servo_control import sevro_process
from machine import UART, FPIOA, Pin, Timer
import time, math

# ============= 初始化部分（只执行一次）=============
fpioa = FPIOA()
fpioa.set_function(3, FPIOA.UART1_TXD)
fpioa.set_function(4, FPIOA.UART1_RXD)
fpioa.set_function(21, FPIOA.GPIO21)
fpioa.set_function(52, FPIOA.GPIO52)

KEY = Pin(21, Pin.IN, Pin.PULL_UP)
LED = Pin(52, Pin.OUT)
uart = UART(1, 115200, timeout=1000)

LED.value(1)
state = 0
show_flag = False

# 常量#这个也导出来###############3
object_width_cm = 5.0
focal_length_pixels = 463
baseline_cm = 6.0  # 假设基线距离为10厘米
###########################这个也导出来####################
target_idx = "交代"
det_app_osd_image = None
#定时器内置的
def fun(tim):
    global state
    state = 1 - state

tim = Timer(-1)
tim.init(period=500, mode=Timer.PERIODIC, callback=fun)

# ============= 工具函数 =============
def set_cul_para(set_object_width_cm=5.0, set_focal_length_pixels=463,set_baseline_cm=3.0 ,set_target_idx="交代"):
    global object_width_cm, focal_length_pixels, target_idx, baseline_cm
    """设置目标物体宽度和焦距"""
    object_width_cm = set_object_width_cm
    focal_length_pixels = set_focal_length_pixels   
    target_idx = set_target_idx
    baseline_cm = set_baseline_cm
    print(f"Parameters set: object_width_cm={object_width_cm}, focal_length_pixels={focal_length_pixels}, target_idx={target_idx}, baseline_cm={baseline_cm}\n")

def send(data):
    global uart
    HEADER = b'\xFF'
    FOOTER = b'\xFE'
    length = len(data.encode('utf-8'))+3
    data=HEADER +bytes([length])+data.encode('utf-8')+ FOOTER#
    uart.write(data)
    print(f"send:{data}")

def get_current_detection_data(res, det_app):
    """实时获取当前检测数据"""
    current_res_coord_pixels = []

    # 检查数据有效性
    if not res or "boxes" not in res or not res["boxes"]:
        return [], None

    # 实时处理当前检测结果
    for i in range(len(res["boxes"])):
        x = int(res["boxes"][i][0] * det_app.paint_size[0] // det_app.rgb888p_size[0])
        y = int(res["boxes"][i][1] * det_app.paint_size[1] // det_app.rgb888p_size[1])
        width = int(float(res["boxes"][i][2] - res["boxes"][i][0]) * det_app.paint_size[0] // det_app.rgb888p_size[0])
        height = int(float(res["boxes"][i][3] - res["boxes"][i][1]) * det_app.paint_size[1] // det_app.rgb888p_size[1])

        cx = int(x + width / 2)
        cy = int(y + height / 2)
        current_res_coord_pixels.append([cx, cy, width, height])

    # 实时获取图像结果
    current_all_image = det_app.get_image_result()
    return current_res_coord_pixels, current_all_image

def find_max_res(res, det_app):
    """找到最大目标物体 - 使用实时数据"""
    global det_app_osd_image, target_idx

    # 获取实时数据
    current_res_coord_pixels, current_all_image = get_current_detection_data(res, det_app)

    if not current_res_coord_pixels or not current_all_image:
        return None

    if "idx" not in current_all_image:
        return None

    max_res = None
    max_area = 0

    for i in range(len(current_res_coord_pixels)):
        # 安全检查索引
        if i >= len(current_all_image["idx"]):
            continue

        # 修复：处理索引到标签的转换
        current_idx = current_all_image["idx"][i]
        if isinstance(current_idx, int):
            # 如果是整数索引，需要转换为标签名进行比较
            try:
                labels = det_app.labels if hasattr(det_app, 'labels') else []
                if current_idx >= len(labels) or labels[current_idx] != target_idx:
                    continue
            except:
                continue
        else:
            # 如果是字符串或列表，直接比较
            if target_idx not in str(current_idx):
                continue

        r_area = current_res_coord_pixels[i][2] * current_res_coord_pixels[i][3]
        if r_area > max_area:
            max_area = r_area
            max_res = current_res_coord_pixels[i]
            if "rectangle" in current_all_image and i < len(current_all_image["rectangle"]):
                det_app_osd_image = current_all_image["rectangle"][i]

    return max_res

def convert_to_depth(left_x, right_x):
    global focal_length_pixels, baseline_cm
    disparity = abs(left_x - right_x)
    if disparity == 0:
        return 0  # 防止除以零
    depth = (baseline_cm * focal_length_pixels) / disparity
    return depth
    
def convert_to_angle(max_res, det_app):
    deflection_angle = math.atan((max_res[0] - det_app.paint_size[0]//2) / det_app.paint_size[1])#原来是640*480的显示
    deflection_angle = math.degrees(deflection_angle)
    return deflection_angle

def check_key():
    """检查按键状态"""
    global show_flag
    if not KEY.value():
        time.sleep_ms(10)
        if not KEY.value():
            show_flag = not show_flag
            while not KEY.value():
                pass
    return show_flag

def process_detection(res, det_app, right_tem=None):
    """处理一次检测 - 主要调用函数"""
    global show_flag, LED, state,focal_length_pixels

    # 检查按键
    if not KEY.value():
        time.sleep_ms(10)
        if not KEY.value():
            show_flag = not show_flag
            while not KEY.value():
                pass

    if show_flag:
        max_res = find_max_res(res, det_app)  # 使用实时数据
        if max_res:
            #双目深度计算
            depth = 0
            if right_tem and max_res:
                depth=convert_to_depth(left_x=max_res[0], right_x=right_tem[0])
            angle = 0
            if det_app and max_res:
                angle = convert_to_angle(max_res, det_app)
            #这里调用舵机模块，###############################传入深度，优化策略
            Servo_dx,Servo_dy=sevro_process(max_res,det_app.paint_size,focal_length_pixels,depth)
            #利用depth控制激光强度
            send("目标矩形中心点x:{:03d},y:{:03d},宽度:{:03d},高度:{:03d},深度:{:07.2f},角度:{:+06.2f}\n"
                 .format(int(max_res[0]), int(max_res[1]), int(max_res[2]), int(max_res[3]), float(depth), float(angle)))
            #定长发送 ，87bytes, 方便DMA搬运
            LED.value(state)
            print(f"目标找到: x={int(max_res[0])}, y={int(max_res[1])}, angle={float(angle):.2f}degrees, depth={float(depth)}, Servo_dx={Servo_dx}, Servo_dy={Servo_dy}")

            return max_res, angle, depth
        else:
            LED.value(0)
    else:
        LED.value(0)

    return None, None, None

# ============= 移除所有立即执行的代码 =============
# 不再有 clock.tick(), image_process_further() 等立即执行的代码

