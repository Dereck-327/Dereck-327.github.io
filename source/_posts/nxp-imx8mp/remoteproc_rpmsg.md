---
title: ARM-NXP        
data: 2025-11-27 11:06:20
tags: [NXP IMX8MP]             
categories: [rpmsg]             
description: nxp arm linux note
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---


# remoteproc 模式下外设的访问

## atf信任固件修改

需要给M核访问指定外设的权限

## uboot更改

在uboot中分配外设的访问权限

## kernel rpmsg 设备树修改

kernel和M和对外设的管脚设置电器属性会冲突，需要删除kernel设备树种对外设管脚的pinctrl


