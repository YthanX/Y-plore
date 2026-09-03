###YOLO思想，预设描框
import os, gc,time
from libs.PlatTasks import DetectionApp
from libs.PipeLine import PipeLine
from libs.Utils import *
from libs.calculate import process_detection
from libs.servo_control import reset_servos
from media.sensor import *
from media.display import *
from media.media import *
display_mode = "VIRT"#自己加的，应该可行
#整个显示画面
show_size=[1280,720]#dispaly_size
#单目显示尺寸
paint_size = [640,480]

#osd输出尺寸
rgb888p_size = [640,480]

osd_layer_num=2

sensor_left_id=0
sensor_right_id=1
#################################注意最后输出的缩放可以调整上面三个参数实现##################
# Set root directory path for model and config
root_path = "/sdcard/mp_deployment_source/"

# Load deployment configuration
deploy_conf = read_json(root_path + "/deploy_config.json")
kmodel_path = root_path + deploy_conf["kmodel_path"]              # KModel path
labels = deploy_conf["categories"]                                # Label list 
confidence_threshold = deploy_conf["confidence_threshold"]        # Confidence threshold 置信度
nms_threshold = deploy_conf["nms_threshold"]                      # NMS threshold 极大抑制值
model_input_size = deploy_conf["img_size"]                        # Model input size，输入尺寸
nms_option = deploy_conf["nms_option"]                            # NMS strategy，极大抑制值策略
model_type = deploy_conf["model_type"]                            # Detection model type
anchors = []
if model_type == "AnchorBaseDet":
    anchors = deploy_conf["anchors"][0] + deploy_conf["anchors"][1] + deploy_conf["anchors"][2]

# Inference configuration
inference_mode = "video"                                          # Inference mode: 'video'
debug_mode = 0                                                    # Debug mode flag

# Create and initialize the video/display pipeline
pl_0 =PipeLine(rgb888p_size=rgb888p_size, display_mode=display_mode,
               display_size=show_size,paint_size=paint_size,
               osd_layer_num=osd_layer_num,id=sensor_left_id,right_id=sensor_right_id)
pl_0.create()

display_size = pl_0.get_display_size()

# Initialize object detection application
det_app = DetectionApp(inference_mode,kmodel_path,labels,
                       model_input_size,anchors,model_type,confidence_threshold,
                       nms_threshold,rgb888p_size,display_size,paint_size,debug_mode=debug_mode)

# Configure preprocessing for the model#预处理
det_app.config_preprocess()
#
max_res = None                                              # Variable to store maximum detection result
depth = 0                                                   # Variable to store depth
angle = 0                                                   # Variable to store angle
# Main loop: capture, run inference, display results#神经网络路径
try:
    while True:
        with ScopedTiming("total", 1):
            img = pl_0.get_frame()
            #继续处理神经网络
            res = det_app.run(img)
            det_app.draw_result(pl_0.osd_img, res)
            max_res, depth, angle = process_detection(res, det_app)
            pl_0.show_image()
            #做另一个相机的显示
            pl_0.show_right_image(max_res)
            gc.collect()
        time.sleep_ms(2)
finally:
    reset_servos()  # Reset servos to default position
    det_app.deinit()
    pl_0.destroy()




