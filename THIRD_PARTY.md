# 第三方组件

原生版静态链接解码器。以下组件保留其原始版权及许可；MIT 项目许可不替代第三方许可。

| 组件 | 版本 | 来源 | 许可位置 |
| --- | --- | --- | --- |
| nlohmann/json | 3.12.0 | https://github.com/nlohmann/json | `native/vendor/json-LICENSE.MIT` 及头文件内声明 |
| libwebp | 1.6.0 | https://chromium.googlesource.com/webm/libwebp | 源码归档中的 `COPYING`、`PATENTS` |
| libavif | 1.4.2 | https://github.com/AOMediaCodec/libavif | 源码归档中的 `LICENSE` |
| dav1d | 1.5.4 | https://code.videolan.org/videolan/dav1d | 源码归档中的 `COPYING` |
| libyuv 相关许可 | 随依赖保留 | https://chromium.googlesource.com/libyuv/libyuv | `native/vendor/libyuv-LICENSE` |
| LLVM-MinGW 运行库 | 工具链 20260908 | https://github.com/mstorsjo/llvm-mingw | 安装包 `licenses/llvm-mingw-LICENSE.txt`；工具链不随仓库上传 |

源码归档校验值：

```text
A1D5B63D2D38EC9BD03ACF643CAA51FA22EDD1E89C5A109C4807717216BBEC07  dav1d-1.5.4.tar.gz
2B645287340BA5A631D268B551DC2D72BD73AC33335962DD36DCDB6D8366921D  libavif-1.4.2.tar.gz
93A852C2B3EFAFEE3723EFD4636DE855B46F9FE1EFDDD607E1F42F60FC8F2136  libwebp-1.6.0.tar.gz
AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63  json.hpp
```

解码库源码归档包含其上游项目文件；仓库中的窗口截图使用合成示例图，不含个人照片。
