# JA2 Stracciatella → PS Vita 移植笔记

## 现状（2026-11-11）
- 上游 `ja2-stracciatella` @ 9fd79a3（2026-11-08），完整交叉编译并打包成 VPK（`build-vita/ja2-stracciatella.vpk`，约 7.5MB）。
- Vita3K：能进主菜单（简中、片头动画、音乐、音效正常）。
- 真机：能进主菜单；片头 `prague.smk` 播完时内存不足 → 已开扩展内存模式、堆 256MB，待验证（需重装 VPK）。
- 按键还没做，只能用正面触屏。

## 目录
| 路径 | 用途 |
|---|---|
| `vita/Containerfile` | VitaSDK 2026.08 镜像 + Rust nightly（含 rust-src） |
| `vita/run.sh` | 在容器内执行命令（podman，`--network=host` 以使用宿主机代理） |
| `vita/build.sh` | 一键 configure + build，产出 VPK |
| `vita/stage-data.sh` | 生成 `vita-data/ja2-stracciatella/`（设备上的数据） |
| `vita/push.sh` | FTP 上传：`vita/push.sh` 传 VPK + 全部数据；`vita/push.sh vpk` 只传 VPK 并替换已安装的 eboot.bin |
| `vita/tools/` | `coreinfo.py` 解析崩溃记录；`sti.py`/`stilist.py` 解包、列出 SLF/STI 资源 |
| `dumps/`、`vita-data/`、`build-vita/` | 日志崩溃记录、设备数据、构建目录（都在 .gitignore 里，路径相对仓库根目录） |

首次使用：`podman build --network=host -t stracciatella-vita-sdk vita`，然后执行 `vita/build.sh`。
FTP：VitaShell 按 SELECT。curl 必须加 `--noproxy '*'`，否则走了系统代理连不上。

## 改了什么（为什么）
### 构建
1. `CMakeLists.txt` 新增 `elseif(VITA)` 分支：
   - SDL3 只有静态库 → 把 `SDL3::SDL3-static` 别名成 `SDL3::SDL3-shared`
   - `-include climits`：GCC+newlib 在 C++ 下只有经过 `<climits>` 才有 `LLONG_MAX`，否则 Lua 的 luaconf.h 报错
   - `MA_NO_RUNTIME_LINKING`：Vita 没有 dlopen（miniaudio 只用于解码，输出走 SDL3）
   - `-Wl,--gc-sections`：丢掉 Rust std 里没用到的代码（省掉大部分缺失符号）；`-Wl,-q` 供 vita-elf-create 使用
   - `-Wl,-u,pthread_cancel -Wl,-u,pthread_once`：静态链接的大坑。libstdc++/libgcc 只**弱引用**这两个符号，弱引用不会把它们从 libpthread.a 拉进来，地址为 0：
     - `__gthread_active_p()` 看 `pthread_cancel` 的地址判断"有没有多线程"→ 返回 0 → 所有 `std::mutex` 加解锁变成空操作，第一次 `condition_variable::wait` 就崩。
     - emutls（`thread_local` 的实现）用 `pthread_once` 初始化自己的锁 → 没链接进来锁一直是 NULL，静态构造 `sol::state` 时就崩。
     - 检查：`arm-vita-eabi-nm build-vita/ja2 | grep " [wU] "`，剩下的弱符号都应是无害的。
   - param.sfo `ATTRIBUTE2=12`（扩展内存模式，App 预算多 109MB）。改了它要重装 VPK，只换 eboot 不够。
   - `vita_create_self` / `vita_create_vpk`，Title ID 为 `JAST00001`
2. `lib-stracciatella/CMakeLists.txt`：Vita 下 pthread 排在 Rust 静态库之后链接。
- 配置参数：`CARGO_BUILD_TARGET=armv7-sony-vita-newlibeabihf`，环境变量 `CARGO_UNSTABLE_BUILD_STD=std,panic_abort`，并关闭 launcher（FLTK）、单元测试、Rust 附带工具。

### `src/vita/newlib_shims.c`（新文件）
3. newlib 缺的 POSIX 桩函数（返回 ENOSYS）、`getpwuid_r` 桩。
4. 堆 256MB（`_newlib_heap_size_user`）。不开扩展内存时 256MB 超预算 → 堆初始化失败、首个 malloc 返回 NULL；128MB 又在播片头时 `std::bad_alloc`（Cinematics.cc 把整个 .smk 读进内存，再把整条音轨解码进不断翻倍扩容的 vector）。
5. 栈：主线程 4MB（`sceUserMainThreadStackSize`，默认 256KB），SDL 线程 1MB（`SDL_THREAD_STACK_SIZE`，默认 64KB）。
6. 启动前把 `HOME` 和 `TMPDIR` 设为 `ux0:data/ja2-stracciatella`。TMPDIR 让日志 `ja2.log` 写到这里（Rust `temp_dir()` 默认 `/tmp`，Vita 上不存在）。
7. 防自动休眠：起一个线程每 10 秒 `sceKernelPowerTick(DISABLE_AUTO_SUSPEND)`。加载时没有输入，系统会把整机休眠，游戏跟着暂停（曾出现 412 秒空白）。
8. 超频（默认关闭）：`ux0:data/ja2-stracciatella/` 下放空文件 `overclock` → CPU 444 / 总线 222 / GPU 222 / Xbar 166 MHz。以后移到启动菜单。待验证：待机唤醒后会不会被重置。

### Rust
9. `fs.rs`：Vita 在 Rust 里算 unix，但 newlib 的 `statvfs` 字段是 u32 → 和 Android 一样返回 1GB 可用空间。
10. `stracciatella.rs`：Vita 下 `get_assets_dir()` 固定返回 `ux0:data/ja2-stracciatella`。
11. `c/path.rs`：Vita 下 `Path_isAbsolute` 把 `ux0:...` 视为绝对路径。
12. `c/fs/tempdir.rs`：Vita 下不用 tempfile 建临时目录，改用 std::fs 建固定的 `ux0:data/ja2-stracciatella/ja2-stracciatella-tmp`（启动时清残留）。
    - **通则**：Rust 从不把 `ux0:...` 当绝对路径，凡是"非绝对就拼 current_dir()"的逻辑都会变成 `app0:/ux0:data/...`；std::fs 直接收 `ux0:` 路径则没问题。
13. `logger.rs`：Vita 下只写文件，用 `LineWriter`。原来 simplelog 每行拆成好几次无缓冲写入，在记忆卡上每行约 0.25 秒；Vita 也没有终端。文件日志级别为 INFO：DEBUG 每打开一个文件记一行，每行落盘都会让游戏卡一下。

### 游戏代码（这两处对所有平台都成立，可提交上游）
14. `src/sgp/SoundMan.cc`：声音缓冲线程的 `std::mutex` + `std::condition_variable` 改成 `SDL_Mutex` / `SDL_Condition`。pthread-embedded 的可取消等待要读调用线程的 pthread 数据，而这个线程是 SDL 直接用 Sony API 建的 → 真机上读到垃圾值崩溃（Vita3K 里恰好是 0，不崩）。
15. `src/game/GameLoop.cc`：主循环捕获异常后先 SLOGE 原始错误，再做紧急存档。新游戏开始前出错时，紧急存档会在 `GetBestPossibleSectorXYZValues` 里访问空的 `Squad` vector 而崩（上游 bug，未修），原始错误就丢了。

### 数据
16. `stage-data.sh` 不排除 `Tilecache`：它是原版数据，不是缓存。

## 静态排查结论（2026-11-10）
- 渲染：只用 SDL3 的 2D renderer，GXM 渲染器可用 ✅（已实测）
- 音频：`SDL_OpenAudioDeviceStream`；miniaudio 只做解码 ✅（已实测）
- 线程：运行时只有主线程、SDL 音频线程、声音缓冲线程、防休眠线程。
- **按键无响应**：游戏只 `SDL_Init(SDL_INIT_VIDEO)`，没初始化手柄子系统。归入输入适配。

## 输入设计（已和用户商定，等真机能跑后再实现）
实现：新增 `vita_input.cc`，读取手柄和背面触摸，转成 SDL 的鼠标/键盘事件再塞回队列；游戏的输入代码不改，只需另外初始化 `SDL_INIT_GAMEPAD`。设置项写在 ja2.json 里，以后放进启动菜单。
- **三种光标控制同时可用**：正面触屏点击（SDL 默认把正面触屏转成鼠标）／背面触摸板／左摇杆，配合 ✕/○ 点击。
- **✕/○**：一个当左键，一个当右键。默认跟随 Vita 系统的确认键设置，也可以手动指定 `x_left` 或 `o_left`。
- **右键的其他方式**：按住 R 再点正面触屏；不用长按。
- **背面触摸**（`rear_touch`：`off` / `relative` / `absolute`，默认 `relative`）：
  - relative：像笔记本触控板，带加速度；两指滑动 = 滚动地图。
  - absolute：触点直接对应屏幕位置。
  - 两种模式下，触摸都只移动光标、不点击（可以看悬停预览），点击交给 ✕/○；按住 ✕ 再滑动 = 拖动。另有"背面轻点 = 左键"的选项，默认关闭。
  - 防误触：只跟踪第一根手指，两侧握持区不算；absolute 模式下把去掉握持区后的有效区域拉伸到整个屏幕，否则屏幕边缘点不到。
  - 可调参数：灵敏度、有效区域边距。
  - 背面触摸由我们自己处理，不用 SDL 的 `SDL_HINT_VITA_TOUCH_MOUSE_DEVICE`（它会一碰就点击）。待确认：SDL3 里背面触摸的坐标朝向（左右是否镜像）、上报为哪个触摸设备。
- **按键**：左摇杆 = 光标；右摇杆 = 滚动地图；Start = Esc；Select = 屏幕快捷键面板；L = 第二层快捷键。各键的具体分配见 [HOTKEYS.md](HOTKEYS.md)（草案，战术画面和地图屏幕各一套）。
- 上游已有 Android 的触屏处理（`TacticalViewPortTouchCallbackTB`，`Turn_Based_Input.cc`），实现时先看看能否复用。
- **文字输入**：靠 SDL3 调出 Vita 系统输入法，需验证。
- 不做两段式点击。
- **按键第一版（2026-11-11）**：`sgp/Input.cc` 的 `VitaPollGamepad()`，主循环每次游戏循环前调用一次；`SGP.cc` 在 Vita 上另外初始化 `SDL_INIT_GAMEPAD`。左摇杆 = 光标（死区 0.2，二次曲线，满推约 700 像素/秒）；系统确认键 = 左键，另一个 = 右键；Start = Esc，△ = Enter，□ = Space，方向键 = 方向键。还没做：L/R/Select、右摇杆、背面触摸、按画面切换的快捷键层、ja2.json 设置项。
- **背面触摸第一版（2026-11-11）**：`VitaPollRearTouch()`，直接用 `sceTouchPeek(SCE_TOUCH_PORT_BACK)`，不经过 SDL。只做 relative 模式：只移动光标、不点击；只跟踪第一根落在左右各 15% 握持区以外的手指；增益 0.35，快速滑动最多 ×3 加速。还没做：absolute 模式、两指滚动、设置项。
- **Vita 设置页（2026-11-11）**：`src/vita/VitaSettings.{h,cc}`，新增 `VITA_SETTINGS_SCREEN`（`ScreenIDs.h`、`Screens.cc`，仅 Vita）。入口是选项界面左下的 "Vita settings" 按钮。界面用英文（游戏本体是英文，中文来自汉化 mod）。每项都是点一下换下一个值：Left click button（System default/Cross/Circle）、Stick cursor speed、Rear touchpad（Off/Touchpad (drag)/Direct (absolute)）、Rear touchpad speed；速度分 4 档。保存在 `$HOME/.ja2/vita.ini`（key=value）。背面触摸的 absolute 模式也在这时实现：去掉两侧握持区后的区域对应整个屏幕。没在 Vita3K 里看过排版。
- **右摇杆滚动（2026-11-11）**：`VitaPollGamepad()` 只在 `GAME_SCREEN` 生效，往 `gsScrollXOffset/YOffset`（鼠标中键拖动平移用的偏移量）里加，推得越多滚得越快（死区 0.2，二次曲线，满推约 900 像素/秒）。偏移量钳在 ±60，免得在地图边缘滚不动时越积越多。
- **LiveArea（2026-11-11）**：`src/vita/sce_sys/`，CMake 里用 `vita_create_vpk(... FILE ...)` 打包。icon0 取 GOG 版 `goggame-1207658696.ico` 的 256×256 图（裁掉白圈）；bg.png 是主菜单国旗 `loadscreens/mainmenubackground.sti` 压暗；startup.png 是 `ja2logo.sti` 叠在压暗的国旗上。PNG 都用 pngquant 转成 8 位索引色。STI 解包脚本：`vita/tools/sti.py <slf> <name.sti> <out.png>`。改图标要重装 VPK。`template.xml` 的 style 要用 `psmobile`：最初写的 `a1` 系统不认，LiveArea 会退回默认外观，把图标放大当背景和启动图。
- **Select 隐藏人物栏（2026-11-11）**：`Interface.cc` 的 `ToggleTacticalPanelHidden()`。隐藏时 `ShutdownCurrentPanel()`（和进地图屏幕时一样），把 `m_VIEWPORT_END_Y`/`m_VIEWPORT_WINDOW_END_Y`/`m_worldClippingRect` 扩到 `SCREEN_HEIGHT`，自己定义全屏的 `gViewportRegion`，禁用雷达点击区域，并 `SetAllAutoFacesInactive()`（否则小队面板的头像还会每帧画在地图上）。隐藏期间 `InitializeCurrentPanel()` 直接走全屏分支，所以切到地图屏幕再回来仍保持隐藏；`RenderPanel`、雷达、时钟都跳过。`SetCurrentTacticalPanelCurrentMerc` 在隐藏时不碰小队面板按钮（否则空按钮引用会崩）。显示人物栏时两侧由纹理重新画上（见"人物栏两侧"），不会残留隐藏时画的地图。已知没处理：雷达切成小队列表模式时的点击区域。没上机测过。
- **格子光标（2026-11-11）**：`src/vita/VitaTactical.{h,cc}`。游戏判断光标所在格用的是平面换算（`GetMouseWorldCoords`，不管地形和屋顶高度），这里用同一套换算：`VitaStepTile` 让方向键走到屏幕方向上的相邻格（屏幕右 = 格坐标 (+1,-1)，下 = (+1,+1)），光标放在格子中心；离视口边缘不到 40 像素时改为滚动地图（往 `gsScrollXOffset/YOffset` 加整格的量）。`VitaSnapToTile` 让左摇杆在地图上时逐格吸附，到了人物栏上恢复自由移动。按住方向键 350ms 后每 120ms 重复；其他画面方向键仍是方向键。
- **主菜单按键选择（2026-11-11）**：`MainMenuScreen.cc` 的 `HandleMenuKeyNavigation`：上下键把光标移到上/下一个可用的菜单按钮中心（跳过禁用的"读取游戏"），回车激活光标所在按钮。手柄上方向键就是上下，✕ 点击或 △ 回车都行。
- **L/R 切换佣兵（2026-11-11）**：`VitaSelectAdjacentMerc`（`VitaTactical.cc`），按小队面板顺序选上/下一个佣兵并居中，逻辑同空格键（`FindNextMercInTeamPanel` + `HandleLocateSelectMerc` + `LocateSoldier`）；上一个是沿"下一个"的循环反推出来的。
- **隐藏人物栏时的快捷键崩溃（2026-11-11）**：`Turn_Based_Input.cc` 里 14 处快捷键会读人物栏按钮的 `->Enabled()`，人物栏隐藏时按钮为空会崩。统一改成 `PanelBtnEnabled()`：按钮不存在就当作可用，交给快捷键的其他条件判断。
- **情境动作菜单（2026-11-11）**：`src/vita/VitaActionMenu.cc`。先做过"按住 △ 轮盘、松开执行"，用户觉得不适合 Vita，改成：战术画面轻按 △ 打开光标处的 3×3 菜单（8 个动作 + 中间 Cancel）；方向键在格子里移动高亮，左摇杆直接指向某格；确认键执行，○ / △ / Start 关闭；触屏点某一项也能执行。打开期间每帧把按钮标记为需要重画并刷新所在区域（同原版 `RenderMovementMenu`），否则会被走动的人物盖掉。打开期间 `VitaPollGamepad` 把所有按键交给菜单，菜单吃掉的按下记在 `eaten[]`，对应的抬起不再传给游戏（单独的鼠标抬起会被当成点击）。动作按光标下的目标（`gUIFullTarget` + `guiUIFullTargetFlags`，门用 `FindStructure(..., STRUCTURE_ANYDOOR)`）决定，英文标签。动作都走游戏自己的路：切光标模式 + 补点击（`VitaQueueClick`，跑/走/蹲行/爬行先按 `UIHandleMovementMenu` 的方式设好移动方式和姿势），或模拟快捷键（`VitaTapKey`：s/c/p 姿势、Alt+R 换弹、a 自动包扎、x 换位、b 点射、j 爬屋顶、z 潜行），或直接调用（分配任务菜单、`SetNewPanel` 打开物品栏）。对自己佣兵的姿势/换弹先选中他。门：手形光标点门，等原版开门菜单弹出后自动按下选中的那项（`VitaDoorMenuActivate`，该项不可用就留着菜单让玩家自己选，最多等约 30 秒）。
  - 表格：地面 = Run/Walk/Sneak/Crawl/Pick up/Look/Climb/Stealth；自己人 = Stand/Reload/Crouch/Prone/Auto bandage/Swap places/Assignment/Inventory；敌人 = Attack/Burst on/off/Look；其他人 = Talk/Look/Attack；门 = Open/Examine/Lockpick/Kick/Use key/Untrap/Crowbar/Explosive。草案里的"瞄准"和"给物品"没做（原版没有对应的单步操作）。
- **人物栏两侧（2026-11-11）**：960 宽时 640 宽的人物栏两侧各空出 160 像素。`FillEmptySpaceAtBottom`（`Interface_Panels.cc`）在 Vita 上改用原版的界面纹理（`DrawFillerOnSurface`，地图屏幕宽屏用的同一张）代替黑色，同时画到屏幕和后备缓冲。上面放触屏快捷按钮（`VitaCreateSideButtons`，`VitaTactical.cc`），按钮图用 `interface/overheaduibuttons.sti`（战术俯瞰地图的按钮，69×27，金属灰边，0 = 常态、1 = 按下），居中放在 2×3 的格子里；最初用的 `CreateTextButton` 通用按钮边框会略微画出格子，随人物栏一起创建/移除（`InitializeCurrentPanel` / `ShutdownCurrentPanel`），只占底部 120 像素（小队面板模式下再往上 20 像素是地图）。左：End turn(d)、Map(m)、Options(o)、Save(Ctrl+S)、Load(Ctrl+L)、Hide panel；右：Reload(Alt+R)、Burst(b)、Climb(j)、Stealth(z)、Roof view(Tab)、Next merc(空格)。点击只记下动作，下一帧在 `VitaSideButtonsTick` 里执行，因为有的动作会重建人物栏、把正在点的按钮删掉。
- **启动慢（2026-11-11）**：`rust/stracciatella/src/file_formats/slf.rs` 的 `entries_from_input` 把整张条目表一次读进内存再解析。原来每个条目拆成 7 次小读取，22 个 SLF 共 28707 个条目、约 20 万次读卡，启动多花约 50 秒。上游也能受益。
- **已实现（2026-11-11）**：`sgp/Input.cc` 在 Vita 上忽略 SDL 手指事件、接受触屏合成的鼠标事件，正面触屏就是鼠标。原来走上游的手机触屏模式（`gfIsUsingTouch`），一碰屏幕就隐藏指针类光标（`fHideOnTouch`），还改成两段式操作。

## 设备上的数据（`vita/stage-data.sh` 生成 `vita-data/ja2-stracciatella/`，整个拷到 `ux0:data/`）
```
ux0:data/ja2-stracciatella/
├── externalized/  mods/   ← 引擎资源（来自 assets/）
├── data/Data/             ← 原版数据（默认取 Wine 下的 GOG 版，去掉 Temp）
├── .ja2/ja2.json, SavedGames/   ← $HOME/.ja2；ja2.json 已有时不会覆盖
├── ja2.log                ← 游戏日志（运行时生成）
└── overclock              ← 可选，放了就超频
```
默认 ja2.json：简中 + `simplified-chinese-localization` mod，960x544，全屏，`debug: true`（方便看日志）。
大批量传数据用 USB（VitaShell 按 START），FTP 只有约 200KB/s。

## 待办
1. **输入**（工作量最大）：见上面的输入设计。
2. 启动菜单（eboot 内）：分辨率、超频、输入选项等。
3. **LiveArea 图标**：`sce_sys/icon0.png` 等，目前用的是默认图标。
4. 发布前：Title ID 换成不会和其他自制软件冲突的值；`debug` 默认关掉。

## 崩溃排查
- 先看日志 `ux0:data/ja2-stracciatella/ja2.log`（FTP 拉：`curl --noproxy '*' ftp://<ip>:1337/ux0:/data/ja2-stracciatella/ja2.log`）。
- 系统错误码（C2-12828-1）会在 `ux0:data/` 留下 `psp2core-*.psp2dmp`：`python3 vita/tools/coreinfo.py <dump>` 解析，`OURS seg1+0xOFF` 的地址手算成 `0x81000000+OFF` 后用 `vita/run.sh 'arm-vita-eabi-addr2line -e build-vita/ja2 -f -p -C 0x...'` 换算成函数名。
- 栈上的地址是扫描出来的，可能有旧垃圾，要结合源码判断。

## Vita3K 调试
- `~/.local/share/Vita3K/Vita3K/ux0/data/ja2-stracciatella` 软链到 `vita-data/ja2-stracciatella`，不用拷数据。所以 Vita3K 运行后 vita-data 里会多出 ja2.log 和临时目录（push.sh 会跳过）。
- 安装并运行：`vita3k --frontend sdl build-vita/ja2-stracciatella.vpk`；已安装后用 `-r JAST00001`。
- 日志必须用 awk 限流（非法访问刷屏时一次能写出 18GB）。
- 抓栈：拷一份 config.yml 把 `gdbstub: true`，用 `-c` 指定；Vita3K 会停在启动处等 gdb：`vita/run.sh 'arm-vita-eabi-gdb -batch -ex "target remote 127.0.0.1:2159" -ex "break abort" -ex continue -ex bt build-vita/ja2'`。不要给 vita3k 本身套 timeout，它会在 gdb 连上前被杀掉。
- **Vita3K 能跑不代表真机能跑**：未初始化内存在模拟器里常是 0，真机上是垃圾值；内存预算模拟器也不卡。
