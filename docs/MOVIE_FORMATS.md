# 影片格式与容错 / Movie formats and recovery

从 **0.2.75-shared-aot** 起，Windows 和 Android 使用同一套影片读取及容错逻辑。

| 内容 / Content | 支持 / Supported |
| --- | --- |
| 封装 / Containers | MP4、MOV、M4V、MKV、WebM、AVI、ASF/WMV、MPEG-PS、MPEG-TS、FLV；H.264/H.265 裸流 |
| 视频 / Video | H.264、H.265/HEVC、VP8、VP9、MPEG-1/2、MPEG-4、MJPEG、FLV、WMV1/2/3、VC-1 |
| 音频 / Audio | AAC、MP3、Vorbis、Opus、FLAC、AC-3/E-AC-3、常见 PCM、WMA1/2/Pro/Lossless |

原版 WMV3/WMA2 和 Final Full Patch 1.0.3.0 的 WMV2/WMA Pro 仍受支持。
影片按内容识别，保留原脚本要求的文件名（例如 `mv100.pak`）及 `IC` 相对路径即可。
资源保持外置。AV1、DTS 等未列出的编码没有保证支持。

文件缺失、权限/读取失败、容器损坏、编码不受支持，或播放中发生解码错误时，
程序会停止该影片的音轨、清除影片画面，并按影片结束继续原脚本。不会把坏影片
反复重启。后续正常影片仍可播放。运行诊断保留 `skipped_movies`、`last_skip_file`
和 `last_skip_reason`，便于定位资源问题。未知游戏指令、客体内存错误和其他非媒体
错误仍正常报错。

显示需要时转换为 8-bit YUV420P，多声道音频混合为双声道 PCM；没有 HDR 色调映射。
ASF 保留无需扫描影片数据的头部读取路径，其他需要探测的格式使用有界探测。
组件验证不等同于完整游戏或所有设备上的画面、音画同步验收。

## English

From **0.2.75-shared-aot**, Windows and Android share the same movie reader and
recovery logic. The table above lists the selected containers and codecs.
Original WMV3/WMA2 and Final Full Patch 1.0.3.0 WMV2/WMA Pro remain supported.
Detection uses content, so keep the filenames expected by the game, such as
`mv100.pak`, and the existing `IC` paths. Resources remain external. Codecs not
listed, including AV1 and DTS, are not guaranteed to work.

Missing or unreadable files, invalid containers, unsupported codecs and decode
errors end the affected movie, stop its audio, clear its picture and complete
the script's movie wait. Playback is not repeatedly restarted. Later valid
movies can still play. Diagnostics retain `skipped_movies`, `last_skip_file`
and `last_skip_reason`. Unknown game instructions, guest-memory faults and
unrelated non-media errors still fail explicitly.

Video is converted to 8-bit YUV420P when needed; multichannel audio is mixed to
stereo PCM. HDR tone mapping is not provided. ASF keeps its header-only startup
path, and formats needing stream discovery use bounded probing. Component
checks do not establish whole-game, presentation or A/V-sync compatibility on
every device.
