# 万能遥控器固件说明

这是一个基于 ESP-IDF 的 ESP32 BLE HID 遥控器项目。设备通过蓝牙模拟键盘、鼠标、多媒体控制和可选手柄，并通过 OLED 显示当前模式、连接状态、电量和菜单。

## 当前功能

- BLE HID 遥控器，支持键盘、鼠标拖动、媒体键。
- 抖音电脑版、抖音 iOS、抖音安卓、多媒体控制、自定义模式、手柄模式、鼠标模式。
- OLED 菜单，支持模式切换、设置、状态页。
- 电池 ADC 读取，IO3 分压输入，实际电压按读取值的 2 倍计算。
- 首页显示蓝牙图标和电池图标。
- BLE 广播电池电量。
- 网页控制自定义快捷键，ESP32 开热点并提供 `192.168.4.1` 配置页。
- BLE HID 分为 `兼容模式` 和 `完全模式`：
  - `兼容模式`：键盘 + 鼠标 + 多媒体，默认模式，适合 iOS。
  - `完全模式`：键盘 + 鼠标 + 多媒体 + 手柄，适合需要手柄 report 的场景。

## 目录结构

```text
main/
  main.c             程序入口、初始化、主循环、动作分发
  mode_manager.c     模式列表、菜单状态机、按键到动作的映射
  app_display.c      OLED 页面绘制，包括首页、菜单、状态页、确认页、手柄横屏页
  app_gamepad.c      手柄模式按键映射、B+X 三秒进入菜单
  app_mouse.c        鼠标持续移动、逐渐加速、左右键报告
  app_games.c        游戏逻辑，包含俄罗斯方块和雷霆战机像素版
  ble_hid.c          BLE HID 初始化、HID report map、配对/断开/电池通知/发送 HID report
  buttons.c          GPIO 按键扫描、消抖、组合键 bitmask
  battery_adc.c      IO3 电池电压 ADC 读取和百分比计算
  custom_mode.c      自定义快捷键解析、保存、发送序列
  web_control.c      Wi-Fi AP、DNS 劫持、HTTP 配置页面
  ssd1306.c          OLED 驱动、中文字体、图标、滚动条、逆时针 90 度文字绘制
  assets/            GB2312 中文字库和 Unicode 映射
```

## 按键定义

按键 GPIO 在 `main/buttons.c` 中：

```c
BUTTON_KEY_UP     GPIO5
BUTTON_KEY_DOWN   GPIO6
BUTTON_KEY_LEFT   GPIO7
BUTTON_KEY_RIGHT  GPIO4
BUTTON_KEY_FUNC1  GPIO41
BUTTON_KEY_FUNC2  GPIO40
BUTTON_KEY_FUNC3  GPIO42
BUTTON_KEY_FUNC4  GPIO39
```

`buttons_poll()` 返回单个按键，用于普通菜单和普通模式。

`buttons_poll_mask()` 返回多个按键的 bitmask，用于手柄模式和组合键。

## 菜单结构

菜单逻辑在 `main/mode_manager.c`。

根菜单：

- `模式切换`
- `设置`
- `游戏`
- `状态`

设置菜单：

- `断开连接`
- `配对模式`
- `恢复出厂设置`
- `网页控制`
- `当前是兼容模式` / `当前是完全模式`

进入 `配对模式` 会断开当前连接、清除 ESP32 内全部绑定记录和设备槽，并生成一个新的、断电保存的随机静态 BLE 地址后开始广播。旧手机或电脑保存的旧设备地址不会继续自动抢连；完成新配对后，新地址会保持不变以便后续自动连接。

`兼容/完全模式` 切换有二级确认页。第一次按确认只进入确认页，确认页按 F1 才会保存、清配对并重启，F2 取消。

游戏菜单：

- `俄罗斯方块`
- `雷霆战机`
- `弹砖块`
- `贪吃蛇`

游戏保持竖屏逻辑：横着握住遥控器时，OLED 物理方向相当于逆时针旋转 90 度，所以游戏按 64x128 的竖屏画布绘制，再旋转输出到 OLED。游戏中 F2 退出返回菜单。

## 模式列表

模式枚举在 `main/mode_manager.c`：

```c
typedef enum {
    APP_MODE_DOUYIN_PC = 0,
    APP_MODE_DOUYIN_IOS,
    APP_MODE_DOUYIN_ANDROID,
    APP_MODE_MEDIA_CTRL,
    APP_MODE_CUSTOM,
    APP_MODE_GAMEPAD,
    APP_MODE_MOUSE,
} app_mode_t;
```

模式名称在 `MODE_NAMES`：

```c
static const char * const MODE_NAMES[MODE_COUNT] = {
    "抖音电脑版",
    "抖音IOS",
    "抖音安卓",
    "多媒体控制",
    "自定义模式",
    "手柄模式",
    "鼠标模式",
};
```

首页按键说明在 `MODE_ACTIONS`：

```c
static const char * const MODE_ACTIONS[MODE_COUNT][4] = {
    [APP_MODE_MEDIA_CTRL] = {"音量+", "音量-", "待机", "切歌"},
};
```

这只是屏幕显示文字，不是真正执行逻辑。真正动作在 `make_action()`。

## 修改某个模式的按键功能

普通模式的按键动作主要改 `main/mode_manager.c` 里的 `make_action()`。

例：多媒体模式现在是：

```c
case APP_MODE_MEDIA_CTRL:
    if (key == BUTTON_KEY_UP) {
        a.type = MODE_ACTION_MEDIA;
        a.value = 0x00E9; // Volume Increment
    } else if (key == BUTTON_KEY_DOWN) {
        a.type = MODE_ACTION_MEDIA;
        a.value = 0x00EA; // Volume Decrement
    } else if (key == BUTTON_KEY_LEFT) {
        a.type = MODE_ACTION_MEDIA;
        a.value = 0x00B6; // Previous Track
    } else if (key == BUTTON_KEY_RIGHT) {
        a.type = MODE_ACTION_MEDIA;
        a.value = 0x00B5; // Next Track
    }
    break;
```

常用动作类型：

- `MODE_ACTION_KEYBOARD_KEY`：发送键盘按键。
- `MODE_ACTION_ABS_MOUSE_DRAG`：执行鼠标拖动/滚动逻辑。
- `MODE_ACTION_MEDIA`：发送 Consumer Control 多媒体键。
- `MODE_ACTION_CUSTOM_SHORTCUT_PRESS`：走自定义快捷键。

每个自定义按键最多可保存 `127` 个字符，并可连续发送最多 `128` 个键盘按键。

## 鼠标模式

- 方向键持续移动鼠标，按住时间越长移动越快，并支持斜向移动。
- 功能键四是鼠标左键，功能键三是鼠标右键。
- 抖音电脑版按功能键四临时进入鼠标模式，临时模式中按功能键二返回。
- 从模式切换菜单正式进入鼠标模式时，功能键三是鼠标右键，功能键一进入菜单。

具体发送动作在 `main.c` 的动作分发部分：

```c
if (action.type == MODE_ACTION_KEYBOARD_KEY) {
    ble_hid_send_key_combo(...);
} else if (action.type == MODE_ACTION_MEDIA) {
    ble_hid_send_consumer(...);
}
```

## 新增一个普通模式

新增模式按这个顺序改：

1. 修改 `main/mode_manager.h`

把 `MODE_COUNT` 加 1。

```c
#define MODE_COUNT 7
```

如果模式数量超过 `MENU_ITEM_MAX`，也要同步增大。

2. 修改 `main/mode_manager.c`

在 `app_mode_t` 里新增枚举：

```c
APP_MODE_NEW_MODE,
```

在 `MODE_NAMES` 增加显示名：

```c
[APP_MODE_NEW_MODE] = "新模式",
```

当前代码 `MODE_NAMES` 没写显式下标，也可以直接按枚举顺序追加。

在 `MODE_ACTIONS` 增加首页提示：

```c
[APP_MODE_NEW_MODE] = {"上键功能", "下键功能", "待机", "左右功能"},
```

在 `make_action()` 增加动作逻辑：

```c
case APP_MODE_NEW_MODE:
    if (key == BUTTON_KEY_UP) {
        a.type = MODE_ACTION_KEYBOARD_KEY;
        a.value = 0x52;
    }
    break;
```

3. 如需特殊显示

普通模式不需要改显示。特殊界面改 `main/app_display.c` 的 `draw_mode_view()`。

4. 如需特殊输入

普通模式用单键输入即可。像手柄模式这种需要组合键或连续状态的，建议单独新建模块，参考 `main/app_gamepad.c`。

## 手柄模式

手柄模式逻辑在 `main/app_gamepad.c`。

物理设备逆时针横过来后，方向映射为：

```c
原 RIGHT = 手柄上
原 LEFT  = 手柄下
原 UP    = 手柄左
原 DOWN  = 手柄右
```

ABXY 映射为：

```c
FUNC3 = A
FUNC1 = B
FUNC2 = X
FUNC4 = Y
```

`FUNC1 + FUNC2` 也就是 `B + X`，同时按住 3 秒进入菜单。

手柄方向键目前被模拟成普通 Button 5-8，而不是 Hat Switch，这样在游戏或按键绑定软件里更容易被识别成普通按钮。

## 游戏功能

游戏逻辑在 `main/app_games.c`，菜单入口在 `main/mode_manager.c` 的 `MENU_PAGE_GAMES`。

当前游戏：

- `俄罗斯方块`
  - 左/右：移动方块
  - 上：旋转
  - 下：加速下落
  - F1：直接落下，结束后重新开始
  - F2：退出
- `雷霆战机`
  - 上/下/左/右：移动飞机
  - F1：发射/重新开始
  - F2：退出
  - 敌机会发射子弹，移动、下压和射击速度随时间增加
- `弹砖块`
  - 左/右：移动挡板
  - F1：发球/重新开始
  - F2：退出
  - 只有当前最下面一排砖块被全部清除后，砖墙才向挡板方向推进一排
- `贪吃蛇`
  - 上/下/左/右：控制方向
  - F1：游戏结束后重新开始
  - F2：退出
  - 穿过边界会从对侧出现；吃到食物后蛇身增长，移动速度随分数提升

如果继续加小游戏：

1. 在 `main/app_games.h` 的 `app_game_type_t` 里新增游戏枚举。
2. 在 `main/mode_manager.c` 的 `GAME_MENU_ITEMS` 增加菜单项。
3. 在 `mode_manager_update()` 的 `MENU_PAGE_GAMES` 分支里把菜单项映射成新的 `MODE_ACTION_GAME_*`。
4. 在 `main.c` 的动作分发里调用 `app_games_start()`。
5. 在 `main/app_games.c` 增加对应的 update/render 函数。

## 兼容模式和完全模式

逻辑在 `main/ble_hid.c`。

`HID_REPORT_MAP_COMPAT`：

- Keyboard
- Mouse
- Consumer Control

`HID_REPORT_MAP_FULL`：

- Keyboard
- Mouse
- Consumer Control
- Gamepad

当前模式保存在 NVS：

```c
#define BLE_HID_DEVICE_NVS_FULL_MODE "fullmode"
```

切换函数：

```c
ble_hid_toggle_hid_mode()
```

切换后会：

1. 保存新模式到 NVS。
2. 清除 BLE 配对。
3. 重启 ESP32。

原因：手机会缓存 HID descriptor。如果不清配对并重启，iOS/Android/Windows 可能继续使用旧描述符。

## 自定义模式和网页控制

网页控制在 `main/web_control.c`。

进入设置里的 `网页控制` 后：

- ESP32 开启 Wi-Fi AP。
- 强制 DNS 指向 `192.168.4.1`。
- 浏览器访问配置页。
- 表单保存到 NVS。

自定义快捷键解析在 `main/custom_mode.c`。

支持示例：

```text
ctrl+c
ctrl+win
shift+a
enter
left
right
H e l l o
```

单个快捷键按下时会发送 press，松开时发送 release。

多个序列会按顺序发送 tap。

## 电池读取

电池模块在 `main/battery_adc.c`。

- ADC 引脚：IO3。
- 硬件使用分压，ADC 读到的是实际电压的一半。
- 代码按 2 倍还原实际电压。
- 3.1V = 0%。
- 4.1V 约等于 100%。

BLE 电池通知在 `main/ble_hid.c` 的 battery task 中，每分钟通知一次。

## OLED 显示

显示逻辑分两层：

- `main/ssd1306.c`：底层绘图、中文字体、图标、滚动条。
- `main/app_display.c`：页面级渲染。

模式切换菜单有滚动条，滚动条只画滑块，不画右侧轨道线。

手柄模式主界面使用 `ssd1306_draw_text16_ccw()` 做逆时针 90 度显示。只有手柄模式主界面旋转，菜单和其它模式不旋转。

## 构建

ESP-IDF 5.4.1 项目，正常构建：

```powershell
idf.py build
idf.py flash monitor
```

如果网页保存时出现：

```text
Header fields are too long
```

项目已经在 `sdkconfig.defaults` 里提高了 HTTP server 限制：

```text
CONFIG_HTTPD_MAX_REQ_HDR_LEN=4096
CONFIG_HTTPD_MAX_URI_LEN=1024
```

如果仍然不够，可以把 header 提到 `8192`。
