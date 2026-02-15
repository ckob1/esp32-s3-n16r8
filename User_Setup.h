// User_Setup.h - 为2.8寸ILI9341定制

#define ILI9341_DRIVER       // 指定驱动芯片

// 引脚定义（匹配你的接线）
#define TFT_MISO -1          // 不接MISO
#define TFT_MOSI 11          // SDI(MOSI) → GPIO11
#define TFT_SCLK 12          // SCK → GPIO12
#define TFT_CS   15          // CS → GPIO15
#define TFT_DC   16          // DC/RS → GPIO16
#define TFT_RST  14          // RST → GPIO14（已连接）

// 背光控制（你直接接3.3V常亮，所以不控制）
#define TFT_BL   -1
#define TFT_BACKLIGHT_ON HIGH

// 屏幕分辨率（标准ILI9341）
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// 添加颜色顺序（如果颜色反转，改ILI9341_RGB）
#define ILI9341_COLOR_ORDER ILI9341_BGR

// 字体和性能优化
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

// SPI速度（降低以提高稳定性）
#define SPI_FREQUENCY  27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000