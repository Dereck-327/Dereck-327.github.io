
# 基于Yocto编译imx-boot

1. 检查磁盘空间，至少需要200GB空间  

wsl.exe --system -d Ubuntu-22.04 df -h /mnt/wslg/distro
Ubuntu-22.04为采用wsl.exe -l查看到的安装版本

2. 代理设置  

export https_proxy=<http://127.0.0.1:7897> http_proxy=<http://127.0.0.1:7897> all_proxy=socks5://127.0.0.1:7897
不同代理设置方式不一样，用代理下载更快

3. 安装必要的库
sudo apt-get install build-essential chrpath cpio debianutils diffstat file gawk gcc git iputils-ping libacl1 liblz4-tool locales python3 python3-git python3-jinja2 python3-pexpect python3-pip python3-subunit socat texinfo unzip wget xz-utils zstd
这里面可能仍然缺少一些库，后续编译出错的时候自行Google安装即可

4. Setting up the Repo utility
mkdir ~/bin (this step may not be needed if the bin folder already exists)
curl <https://storage.googleapis.com/git-repo-downloads/repo> > ~/bin/repo
chmod a+x ~/bin/repo
export PATH=~/bin:$PATH

5. Yocto Project Setup
mkdir imx-yocto-bsp
cd imx-yocto-bsp
repo init -u <https://github.com/nxp-imx/imx-manifest> -b imx-linux-walnascar -m imx-6.12.34-2.1.0.xml
repo sync

6. Image Build
DISTRO=fsl-imx-xwayland MACHINE=imx8mp-ddr4-evk source ./imx-setup-release.sh -b bld-xwayland
激活环境设置设备参数和编译路径，设备参数的选项还没搞懂
DISTRO=fsl-imx-xwayland MACHINE=imx8mp-ddr4-evk bitbake core-image-minimal -c populate_sdk
编译kernel镜像和编译工具链
DISTRO=fsl-imx-xwayland MACHINE=imx8mp-ddr4-evk bitbake -c deploy imx-boot
编译U-boot镜像

自行编译imx-boot

###### Host package setup ######

$ sudo apt install gawk wget git diffstat unzip texinfo gcc build-essential chrpath socat cpio python3 python3-pip python3-pexpect xz-utils debianutils iputils-ping python3-git python3-jinja2 libegl1-mesa libsdl1.2-dev pylint3 xterm python3-subunit mesa-common-dev zstd liblz4-tool rsync curl

###### Execute script for SDK installation ######

chmod a+x fsl-imx-xwayland-glibc-x86_64-core-image-minimal-armv8a-imx8mpevk-toolchain-6.12-walnascar.sh

##### To obtain the SDK (.sh file) you have to do it in Yocto with #####

bitbake imx-image-core -c populate_sdk
在编译目录下的tmp/deploy/sdk

###### Set environment for build ######

source /opt/fsl-imx-xwayland/6.12-walnascar/environment-setup-armv8a-poky-linux
export ARCH=arm64
export AS="$CC"
export LD="$CC"

###### Build Uboot ######

mkdir uboot_build
cd uboot_build
git clone <https://github.com/nxp-imx/uboot-imx> -b lf_v2025.04
cd uboot-imx
make distclean
make imx8mp_evk_defconfig
make dtbs

###### Build ARM Trusted Firmware (ATF) ######

cd ..
git clone <https://github.com/nxp-imx/imx-atf> -b lf_v2.12
cd imx-atf/
make PLAT=imx8mp bl31

###### Download the DDR training bin ######

cd ..
mkdir firmware-imx
cd firmware-imx
wget <https://www.nxp.com/lgfiles/NMG/MAD/YOCTO/firmware-imx-8.29-8741a3b.bin>
此处的下载链接需要从i.MX Linux Release Notes里面获取，此文件属于NXP预编译好的文件，貌似不能自己编译
chmod a+x firmware-imx-8.29-8741a3b.bin
./firmware-imx-8.29-8741a3b.bin

###### Download imx-mkimage and build the boot image ######

cd ~
git clone <https://github.com/nxp-imx/imx-mkimage> -b lf-6.12.34_2.1.0
cd imx-mkimage

###### Now you may copy all the files need for build boot image (flash.bin) ######

cp ../uboot-imx/spl/u-boot-spl.bin iMX8M/
cp ../uboot-imx/u-boot-nodtb.bin iMX8M/
cp ../uboot-imx/dts/upstream/src/arm64/freescale/imx8mp-evk.dtb iMX8M/
cp ../imx-atf/build/imx8mp/release/bl31.bin iMX8M/
cp ../firmware-imx/firmware-imx-8.29-8741a3b/firmware/ddr/synopsys/lpddr4_pmu_train_* iMX8M/
cp ../uboot-imx/tools/mkimage iMX8M/mkimage_uboot
make SOC=iMX8MP flash_evk

