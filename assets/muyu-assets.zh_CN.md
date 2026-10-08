<p align="right"><strong>简体中文</strong> · <a href="muyu-assets.md">English</a></p>

# 一念木鱼素材来源

- `images/coloros-muyu/background-240x320.png` 和 `main/assets/coloros_muyu_bg.rgb565` 原样取自上游 `demo/coloros-muyu` 分支提交 `16df9944d0f6a83b475e05acabbea73c8b49c3e1`，遵循仓库 MIT 许可证。原分支说明背景经 ImageGen 编辑。1.7.0 之前的固件嵌入 RGB565LE（240 × 320，153,600 字节），浏览器预览使用 PNG。转换命令：`ffmpeg -i assets/images/coloros-muyu/background-240x320.png -pix_fmt rgb565le -f rawvideo main/assets/coloros_muyu_bg.rgb565`。
- `main/font_muyu_22.c` 为同一分支原样复用的思源黑体 Medium 子集，含数字、空格、加号及七个汉字。[Adobe 思源黑体](https://github.com/adobe-fonts/source-han-sans)的 SIL OFL 1.1 授权保存在 `fonts/SourceHan-OFL.txt`。
- `main/font_muyu_14.c` 为新生成并针对本应用重命名的 14 像素、4 bpp 字库，来源为 [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)。SIL OFL 1.1 授权保存在 `fonts/OFL.txt`。源文件 `NotoSansSC[wght].ttf` SHA-256：`a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da`。使用 `python tools/generate_muyu_font.py /path/to/NotoSansSC.ttf` 重新生成，依赖 Pillow。生成器提取界面中的所有汉字及可打印 ASCII，共 238 个字形、17,812 字节位图。完整源字体仅供开发下载，不嵌入或随应用打包。
- 音效在 `main/muyu_app.c` 启动时合成（16 kHz、16 位、单声道、140 毫秒），由衰减共振和短噪声起音组成，不包含第三方录音。

- `images/cyber-badge/` 保存工牌、员工终端、游戏列表、设置和木鱼页面的实际 240 × 320 LVGL 渲染图。荒坂徽记由 `main/badge_ui.c` 以几何图形重绘，预览状态值为示例。木鱼页面沿用上述素材许可。字库生成器同时收集中文标点及工牌、注册表、身份配置文字。

- `font_badge_10.c` 和 `font_badge_28.c` 为 Noto Sans SC 的 OFL 子集，字形位图分别为 6,547 和 16,612 字节；10 像素字库包含 Muse 操作提示所需字符。通过 `python tools/generate_badge_fonts.py /path/to/NotoSansSC.ttf` 重新生成。10 像素字体用于辅助终端文字，28 像素用于标题和姓名；28 像素字库也会收集姓名字段中的汉字。
- 荒坂品牌视觉参考：[员工牌设计](https://ko-fi.com/s/5aad91d702)、[徽记图样](https://cyberpunk.fandom.com/fr/wiki/Arasaka)。没有下载或打包商品设计图。几何重绘引用的虚构品牌标识权利独立于代码许可。

- `images/cyber-badge/demo-{card,brand,logo}.rgb565` 是已配置工牌布局的合成主机渲染样本。身份信息为虚构示例，头像为原创几何插画，文字使用上述 Noto Sans SC 字体。这些文件仅作为测试输入，不写入用户工牌资料。

- 1.7.0 起，硬件游戏使用 `main/muyu_ui.c` 中原创的 LVGL 矢量几何图形，通过 `main/badge_theme.h` 获取配色。原橙色位图保留为历史参考及旧版网页模拟使用，不再嵌入当前固件。新主题截图使用示例配色，不含个人工牌数据。
