<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

应用背景、字库、音效合成方式、许可证及重新生成步骤见[一念木鱼素材来源](muyu-assets.zh_CN.md)。

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 音效钥匙扣资源

`audio/voice-keychain/` 现有 25 类、709 段原始 Opus 音频。原有素材中 695 段来自 Shinku-Chen/ai-passport 的 feature/voice-keychain 分支（提交 `71c45cabc1b2f3b73b4929d71cafd926ccbb11f5`），均保持字节不变。首个目录为“高燃BGM”，新增 14 段用户提供的 15 秒素材，界面曲目仅显示歌名；素材由 `tools/encode_voice_bgm.py` 转为 16 kHz 单声道、18 kbit/s CBR、20 ms 帧的 Opus，并执行 -16 LUFS 响度统一、-5 dB 峰值余量和 7.2 kHz 低通处理。所有数据包均使用 2 字节小端长度前缀。`SOURCE.txt` 记录来源和权利边界；音频素材未按 MIT 重新授权。

`tools/build_voice_pack.py` 继续把原有 3,293,101 字节素材放入两个音效分区，并将 494,158 字节 BGM 嵌入应用镜像，因此不改变分区布局。共享 Noto Sans SC 14px UI 字库覆盖新增分组及曲目名称；字体许可见 `fonts/OFL.txt`。

1.9.1：按用户确认的 46 组各保留一段；`audio/voice-keychain/selection.json` 记录原有素材的保留和移除项，保留文件未重新编码。

- [城市电台集成、MIT 来源及主题预览](../docs/leo-radio.zh_CN.md)：源码来源 `assets/radio/`，实际 LVGL 预览 `assets/images/leo-radio/`。

小智来源许可与声明：`xiaozhi/LICENSE`、`xiaozhi/NOTICE`。字幕字库使用现有 Noto Sans SC OFL 字体，由 `tools/generate_xiaozhi_font.py` 生成。详见[集成说明](../docs/xiaozhi.zh_CN.md)。

[竖版首页字体及离线嵌入](../docs/portrait-home.zh_CN.md)：Orbitron 和 Caveat，SIL OFL；原始 TTF、拉丁字符 WOFF2 子集及许可证保存在 `fonts/`。

共享 14px 字库：`fonts/ui14-characters.txt` 保存原有覆盖集合；`tools/generate_shared_fonts.py` 生成三个页面共用的 `main/font_ui_14.c`。仍使用 Noto Sans SC（SIL OFL），不改变原字形像素与尺寸。字幕字库补充字符前检查源字体 cmap，避免把缺字占位符编入固件。

小智表情：本项目原创素材，按仓库 MIT 许可证提供，位于 images/xiaozhi-face/*.svg。tools/generate_face_assets.py 离线转换为 main/xiaozhi_face_assets.c 中可随主题变色的 A8 图片。详见[表情与时钟](../docs/face-clock.zh_CN.md)。

公路之王的 SVG（images/xiaozhi-face/round_*.svg）按用户提供的角色参考图重绘；参考图片本身没有纳入仓库或发布包。

赛博摇卦的经典原文和计算规则来自 CyberYAO 提交
`3c4780feea04737e6e59b6e1849e591710e6656c`，保留
[MIT 许可](cyberyao/LICENSE)。适配后的原文表位于 `main/yao_text_data.c`，
未分发上游图片、音频或字体素材。

赛博摇卦的 `interpret-request.ogg` 是本项目生成的固定中文合成语音，不来自上游录音。内容为请小智查询并解读保留的卦象。`main/assets/yao_request.h` 保存其每帧 60 毫秒的 Opus 数据，用于直接上传，不含个人录音。

公司名称使用 `fonts/NotoSansSC-brand-latin.woff2`（Noto Sans SC，SIL OFL 1.1，见 `fonts/OFL.txt`），为 700 字重的 ASCII 与省略号子集。运行 `tools/generate_brand_font.py <NotoSansSC.ttf>` 生成。离线配置页以内嵌 BadgeBrand 使用；其他标题与签名字体保持原样。
