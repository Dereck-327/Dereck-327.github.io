---
title: Yocto        
data: 2025-0924 14:06:20
tags: [Tools]             
categories: [linux]             
description: linux yocto  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---


# Yocto定制linux (主机为ubuntu)

## 构建步骤

### 下载安装yocoto工程会用到的软件包

```bash
sudo apt install gawk wget git-core diffstat unzip texinfo gcc-multilib
sudo apt install build-essential chrpath socat cpio python3 python3-pip python3-pexpect 
sudo apt install xz-utils debianutils iputils-ping python3-git python3-jinja2 libegl1-mesa libsdl1.2-dev 
sudo apt install pylint3 xterm
```

### clone yocoto项目

```bash
git clone git://git.yoctoproject.org/poky
```

1. 首先你需要初始化构建环境，以此来得到一些重要的环境变量，进入poky目录，运行命令：source oe-init-build-env build，此时系统会创建一个构建目录build（该目录存放整个构建输出内容，或者可以将其称作构建环境），并跳转到该目录下。  
2. 使用vim打开build/conf/local.conf文件（该文件提供一些全局配置），增加两行内容：

  ```bash
  BB_NUMBER_THREADS = "4"
  PARALLEL_MAKE = "-j 4"
  ```

这两行内容分别是构建器使用的线程数，和编译器（Make）使用的线程数，建议设置为小与系统实际能提供的最大线程数，否则默认使用最大线程数。这样做的目的是避免线程数太多导致内存空间不足，以至于构建失败，或者你也可以打开交换内存来增加内存空间避免这一问题，

3. 运行命令`bitbake core-image-sato`

### 验证你构建的镜像是否能够使用

运行命令runqemu tmp/deploy/images/qemux86-64/bzImage-qemux86-64.bin tmp/deploy/images/qemux86-64/core-image-sato-qemux86-64.ext4 。  
此时，系统将会使用默认参数启动刚刚构建的位于build/tmp/deploy/images/qemux86-64/目录下的操作系统镜像

## 嵌入自定义软件包

建立一个自己的层（一般在顶级目录下，以meta开头的目录都叫层，每个层都是独立的，因此为了避免污染其他层，我们建立自己的层），进入poky顶层目录，执行命令`mkdir meta-mylayer`。  
对于每个层，都需要有自己层的配置文件，用于告诉构建器层中菜谱文件的位置及其他内容，因此我们提供这样一个文件，进入meta-mylayer目录中，新建一个文件夹conf，并在该目录下新建一个layer.conf文件，其内容如下：  

```conf
BBPATH .= ":${LAYERDIR}"
BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend \
           "

BBFILE_COLLECTIONS += "mylayer"
BBFILE_PATTERN_mylayer := "^${LAYERDIR}/"
BBFILE_PRIORITY_mylayer = "5"
```

为了能够让编译系统能够把自定义的层包含进去，还需要修改build/conf/bblayers.conf文件，该文件中BBLAYERS变量会声明参与编译的所有层，我们把新建的层添加到变量中：  

```conf
*
BBLAYERS ?= "\
  *
  /home/**/poky/meta-mylayer \
```

### 软件包实现

在meta-mylayer目录中新建文件夹recipes-apps/hello，并进入该目录，新建文件夹hello-1.0，在这个文件夹新建三个文件，内容如下： 

```h
// helloprint.h

void printHello(void);
```

```c
// helloprint.c

#include <stdio.h>
#include "printhello.h"

void printHello(void)
{
  printf("Hello world, xiba yesai");
  return;
}
```

```c
// hello.c
#include "helloprint.h"

int main()
{
  printHello();
  return 0;
}
```

将这三个文件打包为hello-1.0.tar.gz，并存放到hello-1.0目录下。

### .bb文件实现

需要设计软件包菜谱，这个菜谱告诉了构建器从哪里找包，并如何编译安装到镜像中，在recipes-apps/hello目录下新建hello_1.0.bb菜谱文件，其内容如下：

```bb
SUMMARY = "Simple Hello World Application"
DESCRIPTION = "A test application to demonstrate how to create a recipe \
              by directly compiling C files with BitBake."

# a category this package belong to
SECTION = "examples"
# this package is optional, lacking of it doesn't cause error
PRIORITY = "optional"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "\
    file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://hello-1.0.tar.gz"

# S = "${WORKDIR}"
# fix WARN_QA error
TARGET_CC_ARCH += "${LDFLAGS}"

do_compile() {
    ${CC} -c helloprint.c
    ${CC} -c hello.c
    ${CC} -o hello hello.o helloprint.o
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 hello ${D}${bindir}
}
```

### 将自定义包添加到镜像

在文件meta/recipes-sato/images/core-image-sato.bb或者build/conf/local.conf中添加一行内容IMAGE_INSTALL += " hello "。重新构建镜像bitbake core-image-sato

## 遇见的问题

### 查找并杀死所有残留的 Bitbake 进程

pkill -f "bitbake-server"
pkill -f "bitbake"
pkill -f "poke"

### bitbake下载设置代理

在终端中临时设置

```bash
export http_proxy="http://127.0.0.1:7897"
export https_proxy="http://127.0.0.1:7897"
export ftp_proxy="http://127.0.0.1:7897"
export no_proxy="127.0.0.1,localhost,.local"
export HTTP_PROXY="$http_proxy"
export HTTPS_PROXY="$https_proxy"
export FTP_PROXY="$ftp_proxy"
export NO_PROXY="$no_proxy"
```

在 build/conf/local.conf 添加一行（没有就加）：

```bash
BB_NUMBER_THREADS = "8"
PARALLEL_MAKE = "-j 16"
BB_ENV_PASSTHROUGH_ADDITIONS = "http_proxy https_proxy ftp_proxy no_proxy HTTP_PROXY HTTPS_PROXY FTP_PROXY NO_PROXY ALL_PROXY all_proxy"
```

git也走代理

```bash
git config --global http.proxy "http://127.0.0.1:7897"
git config --global https.proxy "http://127.0.0.1:7897"
```

