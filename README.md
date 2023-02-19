 ![language](https://img.shields.io/badge/language-C-green.svg) ![build](https://img.shields.io/badge/build-Windows-blue.svg) ![build](https://img.shields.io/badge/build-linux-FF1010.svg)

HEVC-image-encoder-lite
===========================

A lightweight **H.265/HEVC** intra-frame encoder for **grayscale image encoder**. It is with only 2000 lines of C, which may be the most understandable HEVC implementation.

一个轻量级 **H.265/HEVC 帧内编码器**，用于进行**灰度图象压缩**。代码量仅为 2000 行 C 语言，易于理解。

测试结果：在 Rawzor 提供的 [15张大型灰度图象](http://imagecompression.info/test_images/gray8bit.zip) 中，同质量下**比JPEG小35%**，**比JPEG2000小23%** ，**比WEBP小22%**

　

特点：

* **输入**： **PGM 8-bit 灰度图象文件** （后缀为 .pgm）
  * PGM 是一种非常简单的未压缩灰度图像格式。Windows 没有内置 PGM 文件查看器，可以使用 PhotoShop 或 WPS office 打开和查看 PGM 文件，或者使用[该网站](https://filext.com/online-file-viewer.html)在线查看。

* **输出**：**H.265/HEVC 码流文件** （后缀为 .h265 或 .hevc）
  * 可以使用 [File Viewer Plus](https://fileviewerplus.com/) 软件或 [Elecard HEVC Analyzer](https://elecard-hevc-analyzer.software.informer.com/) 来查看。

* 质量参数可取 0~4 ，对应 HEVC 的量化参数 (Quantize Parameter, QP) 的 4, 10, 16, 22, 28 。越大则压缩率越高，质量越差。
* HEVC的实现代码 ([src/HEVCe.c](./src/HEVCe.c)) **具有极高可移植性**：
  * 只使用两种数据类型： 8-bit 无符号数 (unsigned char) 和 32-bit 有符号数 (int) ；
  * **无依赖**：不调用任何头文件；
  * 不使用动态内存。

　

# 代码说明

代码文件在目录 [src](./src) 中。包括 3 个文件：

- [HEVCe.c](./src/HEVCe.c) ：实现了 HEVC image encoder
- [HEVCe.h](./src/HEVCe.h) ：是 [HEVCe.c](./src/HEVCe.c) 的头文件，引出 top 函数 (HEVCImageEncoder) 供调用。
- [HEVCeMain.c](./src/HEVCeMain.c) ：包含 main 函数的文件，是调用 HEVCImageEncoder 的一个示例，负责读取 PGM 文件并获得输入图象，输给 HEVCImageEncoder 函数进行编码，然后将获得的图象存入文件。

　

另外，这里对 [HEVCe.c](./src/HEVCe.c) 中的 top 函数 (HEVCImageEncoder) 说明如下：

```c
int HEVCImageEncoder (           // return:    -1:出错    positive value:成功，返回输出的 HEVC 码流的长度（单位：字节）
unsigned char *pbuffer,          // 输出的 HEVC 码流将会存在这里
unsigned char *img,              // 图象的原始像素需要在这里输入，每个像素占8-bit（也即一个 unsigned char），按先左后右，先上后下的顺序。
unsigned char *img_rcon,         // 重构后的图象（也即压缩再解压）的像素会存在这里，每个像素占8-bit（也即一个 unsigned char），按先左后右，先上后下的顺序。注意：即使你不关心重构后的图象，也要在这里传入一个和输入图象同样大小的数组空间，否则 HEVC image encoder 不会正常工作。
          int *ysz,              // 输入图象高度。对于不是32的倍数的值，会截断为32的倍数，因此这里是指针，函数内部会会修改该值。
          int *xsz,              // 输入图象宽度。对于不是32的倍数的值，会截断为32的倍数，因此这里是指针，函数内部会会修改该值。
    const int  qpd6,             // 质量参数，可取 0~4 ，对应 HEVC 的量化参数 (Quantize Parameter, QP) 的 4, 10, 16, 22, 28 。越大则压缩率越高，质量越差。
    const int  pmode_cand        // 可取 1~35. 越大则压缩率越高，但性能也越差。推荐取值=7
);
```

　

# 编译

### Windows

将以上三个源文件加入 Visual Studio 工程，并编译即可。

在这里，我已用 Visual Studio 2012 on Windows 10 将其编译好，可执行文件为 [HEVCe.exe](./HEVCe.exe)

### Linux

运行命令：

```bash
sh ./build_gcc.sh
```

在这里，我已用 gcc (Ubuntu 7.5.0-3ubuntu1~18.04) 7.5.0 将其编译好，可执行文件为 [HEVCe](./HEVCe)

　

# 运行

Windows 下的命令格式 (CMD) ：

```bash
HEVCe  <input-image-file(.pgm)>  <output-file(.hevc/.h265)>  [<质量参数(0~4)>]
```

Linux 下的命令格式 ：

```bash
./HEVCe  <input-image-file(.pgm)>  <output-file(.hevc/.h265)>  [<质量参数(0~4)>]
```

我在 [testimage](./testimage) 目录里提供了 5 张 PGM 图象文件供测试。例如，你可以用命令：

```bash
HEVCe testimage/1.pgm 1.hevc
```

意为把 `testimage/1.pgm` 压缩为 `1.hevc` 。

　

# 压缩率/质量评估

我编写了一个 Python 脚本 [HEVCeval.py](./HEVCeval.py) 来把这个 HEVC image encoder 与其它3种图象压缩标准 (JPEG, JPEG2000, WEBP) 进行对比。

它会调用 HEVCe.exe 把一个文件夹中的图象压缩为 .hevc 或 .h265 文件，然后不断试探生成与该 HEVC 压缩码流质量相同（SSIM 值接近）的 JPEG, JPEG2000, WEBP 文件。然后比较他们的文件大小。文件越小，说明同质量下的压缩率越高。

使用以下命令：

```bash
python HEVCeval.py <输入的原始图像的目录> <输出的图象目录>
```

例如：

```bash
python HEVCeval.py testimage testimage_out
```

意为对 [testimage](./testimage) 目录里的所有图象文件进行评估。产生的 HEVC, JPEG, JPEG2000, WEBP 文件都放在 testimage_out 目录里。

我使用这种方法对 Rawzor 提供的 [15张大型灰度图象](http://imagecompression.info/test_images/gray8bit.zip) 进行评估，在同质量下生成的 HEVC 文件**比JPEG小35%**，**比JPEG2000小23%** ，**比WEBP小22%**



