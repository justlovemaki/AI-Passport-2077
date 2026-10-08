<p align="right">
  <strong>简体中文</strong> · <a href="muse.md">English</a>
</p>

# Muse 小程序

`muse` 小程序通过社区 Muse Gadget 协议把 AI Passport 连接到设备主人的 Muse 账号。按住上键时录制 16 kHz 单声道音频，松开后发送语音便笺，并在统一的工牌顶栏下分页显示最长 2 KiB 的文字回复；每页最多九行，短按 OK 翻页。本版不提供 TTS、完整聊天历史、额度查询或远程 Shell/设备命令执行。

## 配置

1. 在本地配置页中，让工牌连接 2.4 GHz Wi-Fi。
2. 从 [Muse Gadgets](https://gadgets.muse.ai/) 获取自己的 SDK Token，并阅读适用条款。
3. 在配置页保存 SDK Token。设备可直连 Muse 时让 HTTP 代理留空；确需可信局域网代理时，只填写代理的 IPv4 地址和 HTTP/混合端口，不接受域名或仅 SOCKS 端口。
4. 打开**小程序 → Muse**。在手机 Muse 的**设置 → 设备**中启用开发者模式，添加屏幕显示的 `MuseGadget-Passport-XXXXXX`；工牌出现实体确认提示时按 OK，并选择工牌当前正在使用的 Wi-Fi。
5. 等待显示“我准备好了”，按住上键说话，松开发送。

工牌协议对 SDK Token 只写不读，浏览器无法回读原值。更换 Token 会清除旧 Muse 账号配对；只修改代理会保留配对。小程序拥有会话时拒绝修改配置。Token、Wi-Fi、配对材料保存在 NVS，不得提交到仓库或写入日志。

社区配对包含实体确认和加密配置，但没有厂商证明；只在可信网络中配对。TLS 证书校验和 Muse Noise 会话保持启用。该接入不会写 eFuse、启用 SDK OTA、开放家庭网络隧道或执行云端下发的设备命令。

## 按键

| 操作 | 功能 |
| --- | --- |
| 按住上键 / 松开上键 | 录音 / 发送语音便笺，单次最长 30 秒 |
| 短按下键 | 停止本地录音或等待；已提交云端的任务仍可能继续 |
| 短按 OK | 确认配对、错误重试或回复翻页 |
| 就绪/回复页长按下键 | 循环切换 Muse 独立形象并保存 |
| 配对/错误页长按下键 | 清除 Muse 账号配对，保留 SDK Token、系统 Wi-Fi 和工牌资料 |
| 长按 OK | 打开全局返回选择器 |

应用只在打开期间拥有麦克风、BLE 配对、TLS/Noise 缓冲和工作任务。退出时先等待工作任务结束，关闭 BLE 与网络会话，释放大块解析缓冲并暂停 codec，最后删除 LVGL 页面。

## 来源与验收

适配层来自 [`manchunx7-bit/ai-passport-muse`](https://github.com/manchunx7-bit/ai-passport-muse) 提交 `6e461e71760e75286ff50d479a6f6db565db6d00`（本地集成采用 MIT），以及 [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk) 在 `components/passport_muse/NOTICE.md` 中记录的提交（Apache-2.0）。界面复用固件中已有的小智矢量形象元素，不重复引入图片资源；Muse 的风格选项独立持久化，不跟随小智，也不包含官方 Jollybot 素材。

发布前须在实机验证：首次 Token 配置、手机发现与实体确认、直连和可选代理连通、非静音录音、连续两轮回复、多页翻页、取消、断线重连、各阶段退出，以及反复进入/退出后无任务或堆泄漏。记录 BLE 配对、TLS 连接、录音和回复订阅阶段的最低空闲堆与最大连续块。固件编译或主机协议测试不能代替实机验收。
