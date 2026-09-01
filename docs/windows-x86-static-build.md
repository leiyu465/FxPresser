# Windows 32 位静态构建

该构建方式参考作者旧版使用的 Qt 5.9.9 精简配置，但不复用旧版二进制文件。Qt、Windows 平台插件和 MinGW 运行时均静态链接，最终运行目录只有一个 `FxPresser.exe`。

## 产物特征

- 目标架构：Windows x86（32 位）
- Qt：5.9.9，官方 `qtbase` 源码
- 链接：Qt、QWindows 平台插件、libgcc、libstdc++ 静态链接
- 关闭：ICU、HarfBuzz-NG、Freetype、OpenGL、PNG/JPEG、DBus、SQL 驱动
- 保留：Qt Core、Gui、Widgets、Windows 系统字体与平台后端
- 外部依赖：仅 Windows 自带的系统 DLL
- 未使用 UPX 或其他加壳工具

## 构建环境

推荐 Debian 12、Ubuntu 24.04 或相近的 Linux 环境。安装依赖：

```bash
sudo apt update
sudo apt install \
  build-essential curl xz-utils patch perl zip \
  g++-mingw-w64-i686
```

脚本会检查所有必需命令，但不会自动调用 `apt` 或修改系统软件。

## 一键构建

在仓库根目录执行：

```bash
./scripts/build-windows-x86-static.sh
```

首次构建需要下载约 43 MiB 的 Qt 源码并完整编译 Qt，耗时取决于网络和 CPU。后续执行会复用 `.build/windows-x86-static` 中已经校验、配置和编译的文件。

可调整并发数或把临时构建目录放到 `/tmp`：

```bash
JOBS=8 BUILD_ROOT=/tmp/fxpresser-static-build \
  ./scripts/build-windows-x86-static.sh
```

`BUILD_ROOT` 只允许位于仓库的 `.build/` 子目录或 `/tmp/`，防止错误变量指向其他用户目录。

## 输出文件

```text
dist/windows-x86-static/FxPresser.exe
dist/FxPresser-windows-x86-static.zip
```

ZIP 中只有一个 EXE。配置文件和调试日志仍由程序在首次运行时自行创建。

## 源码校验与兼容补丁

脚本从 Qt 官方归档下载：

```text
https://download.qt.io/new_archive/qt/5.9/5.9.9/submodules/qtbase-opensource-src-5.9.9.tar.xz
```

固定 SHA-256：

```text
d5a97381b9339c0fbaf13f0c05d599a5c999dcf94145044058198987183fed65
```

校验失败时脚本会停止，不会继续编译。随后应用 `patches/qt-5.9.9-gcc13-mingw.patch`：

1. 为 `std::numeric_limits` 补充缺失的 `<limits>` 标准头文件。
2. 避免 Qt 5.9.9 与现代 MinGW Windows SDK 重复定义 `TOUCHINPUT`。

补丁只解决现代工具链兼容性，不改变 Qt 运行逻辑。

## 验证产物

确认架构：

```bash
file dist/windows-x86-static/FxPresser.exe
```

预期包含：

```text
PE32 executable (GUI) Intel 80386
```

检查导入 DLL：

```bash
i686-w64-mingw32-objdump -p \
  dist/windows-x86-static/FxPresser.exe |
  sed -n 's/^\s*DLL Name: //p'
```

列表中不应出现 `Qt5*.dll`、`libgcc*.dll`、`libstdc++*.dll`、ICU 或其他随包 DLL。出现的 `KERNEL32.dll`、`USER32.dll`、`GDI32.dll` 等属于 Windows 系统组件。

确认管理员清单：

```bash
strings dist/windows-x86-static/FxPresser.exe |
  grep requireAdministrator
```

## Qt 开源许可

本构建使用 Qt 5.9.9 开源版本并进行静态链接。正式分发时需要遵守 Qt 对 LGPL/GPL 和第三方组件的许可要求，包括但不限于：

- 向用户明确说明使用了 Qt，并提供适用的许可证文本。
- 提供实际使用的 Qt 完整对应源码（包括本仓库中的兼容补丁），或由发布者控制的有效书面获取方式。
- 允许用户替换 Qt 并重新链接应用，提供所需的应用目标文件、构建说明和安装信息；或者在适用情况下采用兼容的开源发布方式/Qt 商业许可。
- 不以 DRM 或其他条款限制许可证授予的修改、逆向和重新链接权利。

Qt 官方说明：

- <https://www.qt.io/development/open-source-lgpl-obligations>
- <https://www.qt.io/faq/qt-open-source-licensing>

这部分文档不是法律意见；对外正式发布前应根据实际发布方式复核许可义务。

## 常见问题

### 为什么不直接使用 oldversion.zip 的 Qt DLL？

旧包是 32 位 MSVC 2015 ABI，我们之前的程序是 64 位 MinGW ABI，二者不能混用。此外旧包仍隐含依赖 Visual C++ 2015 Runtime。当前方案全部从 Qt 官方源码重新构建，来源和配置均可复现。

### 为什么 EXE 大约 15 MiB？

Qt Core、Gui、Widgets、Windows 平台插件和 C/C++ 运行时都在同一个文件中。它与旧版 EXE 加三个 Qt DLL 的解压总大小接近，只是从四个文件合并成一个文件。

### 是否可以用 UPX 继续压缩？

不建议。UPX 可能提高杀毒软件启发式误报率。发布时使用 ZIP 压缩即可。
