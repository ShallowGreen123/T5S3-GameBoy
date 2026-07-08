你正在开发LilyGO T5S3-4.7-e-paper-PRO。

目标：
创建一个名为“epd_60fps_probe”的Stage-1概念验证，验证LilyGO T5S3-4.7-e-paper-PRO是否能运行高速原始EPD视频扫描，然后再集成任何GameBoy模拟器。

硬性要求：
1. 暂时不要集成 CrankBoy、ROM 加载、音频、SD 卡、菜单或触摸功能。
2. 创建一个最小移动块演示：
   - 目标面板：ED047TC1,960x540。
   - 活跃视频测试区域：432x480，置中或置于x=432，y=30，类似PaperBoy。
   - 将黑白移动的矩形或棋盘格图案渲染到1bpp的帧缓冲区中。
   - 在VSYNC时翻转帧缓冲区。
   - 通过串行/ESP_LOG每秒打印一次测量帧时间和帧数。
3. 尽可能使用ESP-IDF风格的API：
   - esp_lcd_i80_bus / DMA，用于8位并行传输。
   - 一个固定在核心1上的FreeRTOS任务，用于持续的EPD扫描。
   - 核心0的主/渲染任务。
4. 棋盘目标：
   - LilyGO T5S3-4.7-e-paper-PRO，ESP32-S3,16MB 闪存，8MB PSRAM。
5. EPD MCU徽章：
   - CKH = GPIO4
   - D0 = GPIO5
   - D1 = GPIO6
   - D2 = GPIO7
   - D3 = GPIO15
   - D4 = GPIO16
   - D5 = GPIO17
   - D6 = GPIO18
   - D7 = GPIO8
   - STV = GPIO45
   - CKV = GPIO48
   - STH = GPIO41
   - LE = GPIO42
6. I2C：
   - SDA = GPIO39
   - SCL = GPIO40
   - PCA9535 = 0x20
   - TPS651851 = 0x68
7. PCA9535制图：
   - IO1_0 = EP_OE
   - IO1_1 = EP_MODE
   - IO1_3 = TPS_PWRUP
   - IO1_4 = VCOM_CTRL
   - IO1_5 = TPS_WAKEUP
   - IO1_6 = TPS_PWR_GOOD输入
   - IO1_7 = TPS_INT输入
8. 首先复用现有LilyGO示例/FastEPD或出厂代码中的安全电源序列。
   - 不要猜测不安全的EPD电压。
   - 如果有现有的板子初始化工具，优先使用。
   - 只有在确认功率良好后，扫描任务才会开始。
9. 如果仓库已经包含 FastEPD 示例，请创建一个新的示例文件夹：
   - “示例/epd_60fps_probe/主要”
   - 添加/调整 PlatformIO 的“src_dir”选择，以便编译本示例。
10. 如果以 PaperBoy 代码为参考，只需重复使用显示流水线的概念：
   - 帧缓冲翻转
   - 状态缓冲
   - DMA 行发送
   - 更新任务
   - VSYNC计数器
   不要把模拟器/音频/UI依赖带入第一阶段。

实施成果：
1. 添加“examples/epd_60fps_probe/main/main.cpp”或“main.c”。
2. 添加一个小的板头，例如：
   - “examples/epd_60fps_probe/main/t5s3_epd_pins.h”
3. 如果没有现有辅助工具，添加最小PCA9535助手：
   - 配置输出/输入寄存器
   - EP_OE、EP_MODE、TPS_PWRUP、VCOM_CTRL、TPS_WAKEUP的设置/清除位
   - 阅读TPS_PWR_GOOD
4. 添加最小EPD扫描模块：
   - “epd_video_init（）”
   - 'epd_video_power_on（）'
   - “epd_video_start（）”
   - “epd_video_flip（）”
   - 'epd_video_get_backbuffer（）'
   - “epd_video_get_vsync_count（）”
5. 先使用保守的时机：
   - 如果60帧不稳定，起始目标为20-30 FPS
   - 将“TARGET_FPS”暴露为宏
   - 后期允许 'TARGET_FPS=60'
6. 添加编译时日志：
   - 打印板名称
   - 检测到打印PSRAM
   - 打印自由的内部堆
   - 打印EPD电源状态
   - 打印测量扫描帧时间
7. 用评论或活跃行更新“platformio.ini”：
   - 'src_dir = 示例/epd_60fps_probe/主要数'
8. 确保它能与现有的仓库环境一起编译。

接受标准：
1. 项目成功构建。
2. 序列日志显示：
   - 启动 OK
   - 检测到的I2C设备
   - PCA9535配置
   - TPS电源良好
   - EPD扫描任务启动
   - 测得帧率
3. 显示一个移动的黑白矩形或移动棋盘格区域。
4. 演示至少运行5分钟，没有崩溃、电压下降、看门狗重置或永久冻结扫描。
5. 此阶段不包含模拟器、SD卡、触摸、音频或UI代码。

重要安全注意事项：
- 除非复制自现有的 LilyGO/FastEPD 或该板的出厂代码，否则不要硬编码未知的 TPS651851 电压寄存器写入。
- 如果未检测到电源良好状态，则停止并记录错误，而不是扫描配电箱。
- 添加一条干净的关机路径，禁用VCOM_CTRL、EP_OE、TPS_PWRUP和TPS_WAKEUP。