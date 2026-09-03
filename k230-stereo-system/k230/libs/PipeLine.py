import os
import ujson
from media.sensor import *
from media.display import *
from media.media import *
from libs.Utils import ScopedTiming
import nncase_runtime as nn
import ulab.numpy as np
import image
import gc
import sys
import time

# PipeLine类
class PipeLine:#                                                                   #这里加一下画幅            # 这里修改为2#
    def __init__(self,rgb888p_size=[224,224],display_mode="hdmi",display_size=None,paint_size=None,
                 osd_layer_num=2,id=2,right_id=1,anchor_filtered_alpha=0.3,double_para_group=None,debug_mode=0):
        # sensor给AI的图像分辨率
        self.rgb888p_size=[ALIGN_UP(rgb888p_size[0],16),rgb888p_size[1]]
        # 视频输出VO图像分辨率
        if display_size is None:
            self.display_size=None
        else:
            self.display_size=[display_size[0],display_size[1]]
        # 画幅分辨率#修改
        if paint_size is None:
            self.paint_size=None
        else:
            self.paint_size=[paint_size[0],paint_size[1]]
        # 视频显示模式，支持："lcd"(default st7701 800*480)，"hdmi"(default lt9611)，"lt9611"，"st7701"，"hx8399"
        self.display_mode=display_mode
        # sensor对象
        self.id=id
        self.sensor=None
        #######################双目传感器########################
        if double_para_group is None:
            # 默认参数字典
            self.double_para_group = {
                "search_offset_x": 60,
                "search_offset_y": 8,
                "template_threshold": 0.55,
                "search_step": 8
            }
        else:
            self.double_para_group = double_para_group
        # 右摄像头id
        self.right_id=right_id
        self.sensor_right=None
        # osd显示Image对象
        self.osd_img=None
        self.cur_frame=None
        self.debug_mode=debug_mode
        self.osd_layer_num = osd_layer_num
        #创建一个空的osd图像+匹配属性
        self.match_result = None
        self.osd_img_right = image.Image(self.paint_size[0], self.paint_size[1], image.ARGB8888)
        #右侧osd滤波初始化,这里有写成shi山的嫌疑，左侧osd滤波初始化在plattasks.py中
        self.filtered_rects_right = []
        self.alpha_right = anchor_filtered_alpha
    # PipeLine初始化函数
    def create(self,sensor=None,hmirror=None,vflip=None,fps=60):
        with ScopedTiming("init PipeLine",self.debug_mode > 0):
            nn.shrink_memory_pool()
            # 初始化并配置sensor
            brd=os.uname()[-1]
            if brd=="k230d_canmv_bpi_zero":
                self.sensor = Sensor(id=self.id,fps=30) if sensor is None else sensor
            elif brd=="k230_canmv_lckfb":
                self.sensor = Sensor(id=self.id,fps=30) if sensor is None else sensor
            elif brd=="k230d_canmv_atk_dnk230d":
                self.sensor = Sensor(id=self.id,fps=30) if sensor is None else sensor
            else:
                self.sensor = Sensor(id=self.id,fps=fps) if sensor is None else sensor
            self.sensor.reset()
            if hmirror is not None and (hmirror==True or hmirror==False):
                self.sensor.set_hmirror(hmirror)
            if vflip is not None and (vflip==True or vflip==False):
                self.sensor.set_vflip(vflip)
            #配置sensor_right##########################################################################################
            self.sensor_right = Sensor(id=self.right_id,fps=fps) if sensor is None else sensor
            self.sensor_right.reset()
            # 初始化显示
            if self.display_mode=="hdmi":
                # 设置为LT9611显示，默认1920x1080
                if self.display_size==None:
                    Display.init(Display.LT9611,osd_num=self.osd_layer_num, to_ide = True)
                else:
                    Display.init(Display.LT9611, width=self.display_size[0], height=self.display_size[1],osd_num=self.osd_layer_num, to_ide = True)
            elif self.display_mode=="lcd":
                # 默认设置为ST7701显示，480x800
                if self.display_size==None:
                    Display.init(Display.ST7701, osd_num=self.osd_layer_num, to_ide=True)
                else:
                    Display.init(Display.ST7701, width=self.display_size[0], height=self.display_size[1], osd_num=self.osd_layer_num, to_ide=True)
            elif self.display_mode=="lt9611":
                # 设置为LT9611显示，默认1920x1080
                if self.display_size==None:
                    Display.init(Display.LT9611,osd_num=self.osd_layer_num, to_ide = True)
                else:
                    Display.init(Display.LT9611, width=self.display_size[0], height=self.display_size[1],osd_num=self.osd_layer_num, to_ide = True)
            elif self.display_mode=="st7701":
                # 设置为ST7701显示，480x800
                if self.display_size==None:
                    Display.init(Display.ST7701, osd_num=self.osd_layer_num, to_ide=True)
                else:
                    Display.init(Display.ST7701, width=self.display_size[0], height=self.display_size[1], osd_num=self.osd_layer_num, to_ide=True)
            elif self.display_mode=="hx8399":
                # 设置为HX8399显示，默认1920x1080
                if self.display_size==None:
                    Display.init(Display.HX8399, osd_num=self.osd_layer_num, to_ide=True)
                else:
                    Display.init(Display.HX8399, width=self.display_size[0], height=self.display_size[1], osd_num=self.osd_layer_num, to_ide=True)
            elif self.display_mode=="VIRT":
                # 设置为VIRT显示，默认1920x1080,用于ide调试
                if self.display_size==None:
                    Display.init(Display.VIRT, osd_num=self.osd_layer_num, to_ide=True)
                else:
                    Display.init(Display.VIRT, width=self.display_size[0], height=self.display_size[1], osd_num=self.osd_layer_num, to_ide=True)
            else:
                # 设置为LT9611显示，默认1920x1080
                Display.init(Display.LT9611,osd_num=self.osd_layer_num, to_ide = True)
            self.display_size=[Display.width(),Display.height()]
            # 通道0直接给到显示VO，格式为YUV420,设置右摄像头chn0绑定到vide02
            self.sensor.set_framesize(w = self.paint_size[0], h = self.paint_size[1], chn=CAM_CHN_ID_0)
            self.sensor.set_pixformat(PIXEL_FORMAT_YUV_SEMIPLANAR_420, chn=CAM_CHN_ID_0)
            self.sensor_right.set_framesize(w = self.paint_size[0], h = self.paint_size[1], chn=CAM_CHN_ID_0)
            self.sensor_right.set_pixformat(PIXEL_FORMAT_YUV_SEMIPLANAR_420, chn=CAM_CHN_ID_0)
            ##############################################我次奥我设置通道1lyao##############################
            #sensor的通道1也来搅和一下
            self.sensor.set_framesize(w = self.paint_size[0], h = self.paint_size[1], chn=CAM_CHN_ID_1)
            self.sensor.set_pixformat(Sensor.RGB888, chn=CAM_CHN_ID_1)
            #通道1显示到osd0
            self.sensor_right.set_framesize(w = self.paint_size[0], h = self.paint_size[1], chn=CAM_CHN_ID_1)
            self.sensor_right.set_pixformat(Sensor.RGB888, chn=CAM_CHN_ID_1)
            ##################################################################################################
            self.sensor.set_framesize(w = self.rgb888p_size[0], h = self.rgb888p_size[1], chn=CAM_CHN_ID_2)
            # set chn2 output format
            self.sensor.set_pixformat(PIXEL_FORMAT_RGB_888_PLANAR, chn=CAM_CHN_ID_2)

            # OSD图像初始化
            self.osd_img = image.Image(self.paint_size[0],  self.paint_size[1], image.ARGB8888)
            #                                                           #这里修改#
            sensor_bind_info = self.sensor.bind_info(x =  0, y = (self.display_size[1]-self.paint_size[1])//2, chn = CAM_CHN_ID_0)
            Display.bind_layer(**sensor_bind_info, layer = Display.LAYER_VIDEO1)
            #在这里绑定图层到video2
            sensor_bind_info_right = self.sensor_right.bind_info(x =  self.paint_size[0], y = (self.display_size[1]-self.paint_size[1])//2, chn = CAM_CHN_ID_0)
            Display.bind_layer(**sensor_bind_info_right, layer = Display.LAYER_VIDEO2)
            # media初始化
            MediaManager.init()#外部初始化
            # 启动sensor
            self.sensor.run()#外部启动

    # 获取一帧图像数据，返回格式为ulab的array数据
    def get_frame(self):
        with ScopedTiming("get a frame",self.debug_mode > 0):
            self.cur_frame = self.sensor.snapshot(chn=CAM_CHN_ID_2)
            input_np=self.cur_frame.to_numpy_ref()
            return input_np

    # 在屏幕上显示osd_img
    def show_image(self):
        with ScopedTiming("show result",self.debug_mode > 0):
            #                                                 #这里修改#
            Display.show_image(self.osd_img,x= 0, y=(self.display_size[1]-self.paint_size[1])//2,layer=Display.LAYER_OSD3,alpha=255, flag=0)

    def get_display_size(self):
        return self.display_size
    def rect_filter(self, boxes):
        """
        对单个矩形 [x, y, w, h] 做EMA滤波
        rect: 当前帧的[x, y, w, h]
        alpha: 滤波系数，越小越平滑
        """
        if not hasattr(self, "filtered_rect") or self.filtered_rect is None:
            self.filtered_rect = list(boxes)
        else:
            for i in range(4):
                self.filtered_rect[i] = int(self.alpha_right * boxes[i] + (1 - self.alpha_right) * self.filtered_rect[i])
        return self.filtered_rect
    def show_right_image(self,left_rect=None):#传入左侧图像的ROI矩形元组
            # 显示右侧传感器的图像到osd0
            right_frame = self.sensor_right.snapshot(chn=CAM_CHN_ID_1)
            #尝试剪裁roi
            left_frame = self.sensor.snapshot(chn=CAM_CHN_ID_1)#拷贝一下用作裁剪？还是用另一个通道
            if left_rect is None:
                # 如果没有传入左侧图像的ROI矩形，则直接显示右侧图像
                self.osd_img_right.clear()
                Display.show_image(self.osd_img_right, x=self.paint_size[0], y=(self.display_size[1] - self.paint_size[1]) // 2, layer=Display.LAYER_OSD0, alpha=255, flag=0)
                return None
            # *将中心点坐标转换为左上角坐标**
            cx, cy, w, h = left_rect
            x_tl = int(cx - w / 2)
            y_tl = int(cy - h / 2)
            left_rect = (x_tl, y_tl, w, h) # 这是用于裁切的左上角坐标矩形
            try:
                template = left_frame.copy(roi=left_rect)
            except Exception as e:
                print(f"裁切左侧图像失败: {e}, ROI: {left_rect}")
                return None
            # 3. 定义在右侧图像的搜索区域 (ROI)
            # Y轴方向：在原始Y坐标上下小范围搜索，比如上下20个像素
            # X轴方向：由于视差，物体在右图会偏左，所以向左多搜索一些
            search_roi_x = max(0, left_rect[0] - self.double_para_group["search_offset_x"])  #1
            search_roi_y = max(0, left_rect[1] - self.double_para_group["search_offset_y"])   #2  
            search_roi_w = left_rect[0] - search_roi_x + left_rect[2] # 搜索区域宽度
            search_roi_h = left_rect[3] + 16 # 搜索区域高度
            # 保证搜索区域不越界
            search_roi_w = min(search_roi_w, right_frame.width() - search_roi_x)
            search_roi_h = min(search_roi_h, right_frame.height() - search_roi_y)
            search_area = (search_roi_x, search_roi_y, search_roi_w, search_roi_h)
            #要转化为灰度图像比较
            template_gray = template.to_grayscale()
            right_img = right_frame.to_grayscale()
            # 4. 在右侧图像中搜索模板,返回匹配结果，是一个矩形列表，[x, y, w, h]
            self.match_result = None#
            self.match_result = right_img.find_template(template_gray, self.double_para_group["template_threshold"], roi=search_area, step=self.double_para_group["search_step"], search=image.SEARCH_EX)##3，4
            # 只有匹配结果变化时才刷新OSD层
            self.osd_img_right.clear()
            if self.match_result:
                # 5. 如果找到匹配，绘制蓝色矩形框
                print(f"找到匹配的模板，位置: {self.match_result}")
                boxes= [self.match_result[0], self.match_result[1], self.match_result[2], self.match_result[3]] 
                # 进行滤波处理
                boxes = self.rect_filter(boxes)
                x, y, w, h = boxes[0], boxes[1], boxes[2], boxes[3]
                # 绘制矩形框
                self.osd_img_right.draw_rectangle(x, y, w, h, color=(0, 255, 0), thickness=4)
                label_y_pos = y + 5 if y < 30 else y - 25
                self.osd_img_right.draw_string_advanced(x, label_y_pos, 20, "MACTHING 右侧",  color=(0, 255, 0),tickness=4)

            Display.show_image(self.osd_img_right, x=self.paint_size[0], y=(self.display_size[1] - self.paint_size[1]) // 2, layer=Display.LAYER_OSD0, alpha=255, flag=0)

            
    #获取当前匹配结果
    def get_match_result(self):
        if self.match_result:
                # 计算中心点坐标
                x, y, w, h = self.match_result[0], self.match_result[1], self.match_result[2], self.match_result[3]
                cx = x + w // 2
                cy = y + h // 2
                # 返回中心点和宽高
                return [cx, cy, w, h]
        else:
            return None   

    # PipeLine销毁函数
    def destroy(self):
        with ScopedTiming("deinit PipeLine",self.debug_mode > 0):
            os.exitpoint(os.EXITPOINT_ENABLE_SLEEP)
            # stop sensor
            self.sensor.stop()
            # stop sensor_right
            if self.sensor_right is not None:
                self.sensor_right.stop()
            # deinit lcd
            Display.deinit()
            time.sleep_ms(50)
            # deinit media buffer
            MediaManager.deinit()

