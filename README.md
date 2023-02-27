 ![language](https://img.shields.io/badge/language-C-green.svg) ![build](https://img.shields.io/badge/build-Windows-blue.svg) ![build](https://img.shields.io/badge/build-linux-FF1010.svg)

HEVC image encoder lite
===========================

A lightweight **H.265/HEVC** intra-frame encoder for **grayscale image compression**. It is with only 1600 lines of C, which may be the most understandable HEVC implementation.

一个轻量级 **H.265/HEVC 帧内编码器**，用于进行**灰度图像压缩**。代码量仅为 1600 行 C 语言，易于理解。

测试结果：该代码在压缩 [kodak提供的24张图像](https://r0k.us/graphics/kodak/) 时 (转为灰度图像后再压缩)，**在同质量下文件大小比 JPEG 小 38% ，比 JPEG2000 小 25% ，比 WEBP 小 13%** 。

另外，如果想了解 HEVC 图像压缩原理，可以阅读我写的文章 [H.265/HEVC 帧内编码详解：CU层次结构、预测、变换、量化、编码](https://zhuanlan.zhihu.com/p/607679114)

　

本代码的特点：

* **输入**： **PGM 8-bit 灰度图像文件** （后缀为 .pgm）
  * PGM 是一种非常简单的未压缩灰度图像格式。Linux 系统往往可以直接查看。而 Windows 没有内置 PGM 文件查看器，可以使用 PhotoShop 或 WPS office 查看 PGM 文件，或者使用[该网站](https://filext.com/online-file-viewer.html)在线查看。

* **输出**： **H.265/HEVC 码流文件** （后缀为 .h265 或 .hevc）
  * 可以使用 [File Viewer Plus](https://fileinfo.com/software/windows_file_viewer) 软件或 [Elecard HEVC Analyzer](https://elecard-hevc-analyzer.software.informer.com/) 软件来查看。

* 质量参数可取 0~4 ，对应 HEVC 的量化参数 (Quantize Parameter, QP) 的 4, 10, 16, 22, 28 。越大则压缩率越高，质量越差。
* HEVC的实现代码 ([src/HEVCe.c](./src/HEVCe.c)) **具有极高可移植性**：
  * 只使用两种数据类型： 8-bit 无符号数 (unsigned char) 和 32-bit 有符号数 (int) ；
  * 不调用任何头文件；
  * 不使用动态内存。

　

支持的 HEVC 特性：

- CTU : 32x32
- CU : 32x32, 16x16, 8x8
- TU : 32x32, 16x16, 8x8, 4x4 。CU 拆分 TU 的最大深度=1 (每个CU单独作为TU，或者分成4个小TU，而小TU不分为更小的TU)
- part_mode : 8x8 的 CU 可能单独作为 PU (`PART_2Nx2N`) ，也可能分成4个 PU (`PART_NxN`)
- 支持全部 35 种预测模式
- 简化的 RDOQ (Rate Distortion Optimized Quantize)

　

# 代码说明

代码文件在目录 [src](./src) 中。包括 3 个文件：

- [HEVCe.c](./src/HEVCe.c) ：实现了 HEVC image encoder
- [HEVCe.h](./src/HEVCe.h) ：是 [HEVCe.c](./src/HEVCe.c) 的头文件，引出 top 函数 (`HEVCImageEncoder`) 供调用。
- [HEVCeMain.c](./src/HEVCeMain.c) ：包含 `main` 函数的文件，是调用 `HEVCImageEncoder` 的一个示例，负责读取 PGM 文件并获得输入图像，输给 `HEVCImageEncoder` 函数进行编码，然后将码流存入文件。

　

这里对 [HEVCe.c](./src/HEVCe.c) 中的 top 函数 (`HEVCImageEncoder`) 说明如下：

```c
int HEVCImageEncoder (                 // 返回输出的 HEVC 码流的长度（单位：字节）
    unsigned char       *pbuffer,      // 输出的 HEVC 码流将会存在这里
    const unsigned char *img,          // 图像的原始灰度像素需要在这里输入，每个像素占8-bit（也即一个 unsigned char），按先左后右，先上后下的顺序。
    unsigned char       *img_rcon,     // 重构后的图像（也即压缩再解压）的像素会存在这里，每个像素占8-bit（也即一个 unsigned char），按先左后右，先上后下的顺序。注意：即使你不关心重构后的图像，也要在这里传入一个和输入图像同样大小的数组空间，否则 HEVC image encoder 不会正常工作。
    int                 *ysz,          // 输入图像高度。对于不是32的倍数的值，会补充为32的倍数，因此这里是指针，函数内部会修改该值。
    int                 *xsz,          // 输入图像宽度。对于不是32的倍数的值，会补充为32的倍数，因此这里是指针，函数内部会修改该值。
    const int            qpd6          // 质量参数，可取 0~4 ，对应 HEVC 的量化参数 (Quantize Parameter, QP) 的 4, 10, 16, 22, 28 。越大则压缩率越高，质量越差。
);
```

　

# 编译

### Windows (Visual Studio)

将 [src](./src) 目录中的三个源文件加入 Visual Studio 工程，并编译即可。

### Windows (命令行)

如果你把 Visual Studio 里的 C 编译器 (`cl.exe`) 加入了环境变量，也可以用命令行 (CMD) 进行编译。运行命令：

```bash
cl .\src\*.c /FeHEVCe.exe /Ox
```

该命令的含义是输出可执行文件名为 `HEVCe.exe` ，开启最大化优化 (`/Ox`)

在这里，我已用 cl (用于 x86 的 Microsoft (R) C/C++ 优化编译器 17.00.50727.1) 将其编译好，可执行文件为 [HEVCe.exe](./HEVCe.exe)

### Linux

运行命令：

```bash
gcc src/*.c -lm -o HEVCe -O3 -Wall
```

该命令的含义是输出可执行文件名为 `HEVCe` ，开启最大化优化 (`-O3`) ，报告所有 Warning (`-Wall`)  (实际上并没有任何 Warning) 。

在这里，我已用 gcc (Ubuntu 7.5.0-3ubuntu1~18.04) 7.5.0 将其编译好，可执行文件为 [HEVCe](./HEVCe)

　

# 运行

### Windows (命令行)

Windows 下的命令格式 (CMD) ：

```bash
HEVCe  <input-image-file(.pgm)>  <output-file(.hevc/.h265)>  [<质量参数(0~4)>]
```

我在 [testimage](./testimage) 目录里提供了 24 张 PGM 图像文件供测试。例如在Windows下，可以运行命令：

```bash
HEVCe testimage/01.pgm 01.hevc
```

该命令的含义是把 `testimage/01.pgm` 压缩为 `01.hevc` 。

### Linux

Linux 下的命令格式 ：

```bash
./HEVCe  <input-image-file(.pgm)>  <output-file(.hevc/.h265)>  [<质量参数(0~4)>]
```

　

# 压缩率/质量评估

我编写了一个 Python 脚本 [HEVCeval.py](./HEVCeval.py) 来把这个 HEVC image encoder 与其它3种图像压缩标准 (JPEG, JPEG2000, WEBP) 进行对比。它需要在 Windows 上运行，会调用 [HEVCe.exe](./HEVCe.exe) 把指定文件夹中的图像压缩为 .h265 文件。然后不断试探生成与该 HEVC 压缩码流质量相同 (SSIM 值最接近) 的 JPEG, JPEG2000, WEBP 文件。然后比较他们的文件大小。文件越小，说明同质量下的压缩率越高。

使用以下命令：

```bash
python HEVCeval.py <输入的原始图像的目录> <输出的图像目录>
```

例如：

```bash
python HEVCeval.py testimage testimage_out
```

意为对 [testimage](./testimage) 目录里的所有图像文件进行评估。产生的 HEVC, JPEG, JPEG2000, WEBP 文件都放在 `testimage_out` 目录里。

[testimage](./testimage) 里的24个文件是 [kodak提供的24张图像](https://r0k.us/graphics/kodak/) 转化为灰度后的图像，用这种方法进行评估，结果是本代码生成的 HEVC 图像文件**在同质量下文件大小比 JPEG 小 38% ，比 JPEG2000 小 25% ，比 WEBP 小 13%** 。

　

另外，如果你想测试其它图像的压缩，可以使用我提供的一个 Python 脚本 [ConvertToPGM.py](./ConvertToPGM.py) 来把其它文件格式 (例如.jpg, .png) 转化为灰度的 .pgm 图像文件，使用方法是：

```
python ConvertToPGM.py <输入目录> <输出目录>
```

它能把输入目录里的图像文件转化为 .pgm 图像文件，放在输出目录里。

