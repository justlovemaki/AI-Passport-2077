<p align="right">
  <strong>简体中文</strong> · <a href="pipeline-guide.md">English</a>
</p>

# AI Agent 指南：版本更新、固件编译与设备烧录自动化流程

本文档为 AI Agent 提供固件版本号更新、编译、打包验证及烧录的标准工作流。

## 1. 自动化流水线工具：`tools/pipeline.py`

仓库使用 `tools/pipeline.py` 同步更新版本、执行普通构建及显式开发工具。正式发布／保留数据交付会有意组合该工具、仓库门禁及 `tools/flash_badge.py`；任何单个构建命令都不视为实机验收。

### 发布流程

1. **只递增一次版本，同步全部公开版本位置并构建**：
   ```bash
   python tools/pipeline.py --bump patch
   ```
   只有明确决定版本时才使用 `--bump minor`、`--bump major` 或 `--version X.Y.Z`。再次普通构建必须省略全部版本参数。

2. **运行完整仓库门禁**：
   ```bash
   ./tools/validate.sh
   ```
   该步骤独立于流水线构建，需要激活 ESP-IDF 5.5.3，并分别报告主机测试与固件编译结果。

3. **使用完整私密备份执行保留数据安装**：
   ```bash
   python tools/flash_badge.py --port <PORT> \
     --backup-dir <仓库外的私密目录> --build build
   ```
   安装器会验证现有布局，在任何写入前读取完整 8 MiB Flash，分段写入并校验固件，最后提交分区表，再核对 NVS／PHY 和全部资料卡 bank 后复位。

### 其他操作

- **仅限开发的应用增量写入**（不属于发布或保留数据验收流程）：
  ```bash
  python tools/pipeline.py --flash
  ```
- **旧版选定分区备份／恢复工具**必须显式指定私密目录：
  ```bash
  python tools/pipeline.py --backup-only --backup-dir <私密目录>
  python tools/pipeline.py --restore-only --backup-dir <私密目录>
  ```

## 2. 核心执行阶段

### 阶段 1：更新固件版本号

`--bump` 与 `--version` 会同时更新三处：

- `main/badge_profile.h`：`#define BADGE_VERSION "X.Y.Z"`
- `README.md`：英文公开固件版本
- `README.zh_CN.md`：对应的简体中文固件版本

只接受数字形式 `X.Y.Z`。现有三处值不一致、标记缺失或重复时，在写文件前直接失败。工具先准备全部替换，再提交文件，最后写入作为事实来源的头文件，随后校验同步结果并运行仓库检查。普通调用不带 `--bump`／`--version` 时只读取当前版本，绝不自动递增。

### 阶段 2：激活 ESP-IDF 5.5.3 环境
- 工具链路径：`D:\esp\esp-idf`
- Windows PowerShell 环境激活：
  ```powershell
  [Environment]::SetEnvironmentVariable('MSYSTEM', '')
  Set-Location D:\esp\esp-idf
  . .\export.ps1
  Set-Location <项目根目录>
  ```

### 阶段 3：构建与 0x0 合并固件打包
- 编译应用：`idf.py build`
- 合并完整固件：`idf.py merge-bin -o FoloToy-AI-Passport-full.bin`
- 校验固件布局：`python tools/verify_firmware.py build`

该构建不能代替 `./tools/validate.sh`；交付前仍须运行完整门禁。固件门禁会把同一个隔离、验证通过的构建中的合并及分段文件重新发布到 `build/`，后续保留数据安装器不会误用流水线留下的旧分段产物。

### 阶段 4：烧录写入设备（严格区分数据保护与全刷）

> **⚠️ 核心安全原则**：
> - **日常开发 / 功能升级**：**严禁直接刷写 0x0 全镜像**！0x0 写入会擦除 `0x9000`（NVS 配置、Wi-Fi 账号密码）和 `0x690000`/`0x7d0000`（工牌个人资料、二维码）。
> - **保留数据的正式安装（推荐）**：完整门禁后使用 `tools/flash_badge.py`。它会备份全部 8 MiB，并按上述流程校验受保护区域。
- **仅写应用**：只写 `0x10000` 可避开已知用户分区，因此保留为开发快捷方式；但该命令本身不包含完整写前备份和写后受保护区域验收。

- **正式／保留数据安装**：
  ```bash
  python tools/flash_badge.py --port <PORT> \
    --backup-dir <仓库外的私密目录> --build build
  ```
- **仅限开发的应用写入**：
  ```bash
  python tools/pipeline.py --flash
  ```

- **全盘出厂恢复刷写（清除所有数据）**：
  仅当设备为空片或用户明确要求重置时使用：
  ```bash
  python tools/pipeline.py --erase-all
  ```

## 3. 常见问题排查

- **烧录后串口端口消失**：按一下设备正面的 UP/唤醒按键，或重新插拔 USB 数据线以重新枚举串口。
- **压缩大小与实际 Flash 大小**：`esptool` 在串口传输过程中采用了压缩算法，片上解压后物理写入的依然是完整的 8MB 空间。
