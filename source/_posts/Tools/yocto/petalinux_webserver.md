---
title: Yocto        
data: 2025-0924 14:06:20
tags: [Tools]             
categories: [linux]             
description: petalinux yocto webserver 
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# petalinux 构建嵌入式webserver

## add nginx

petalinuxde yocto layers中已经有了webserver的配方, 直接build就行

```bash
petalinux-build -c nginx
```

### nginx添加进镜像

在`project-spec/meta-user/conf/user-rootfsconfig`中添加

```conf
CONFIG_nginx
```

然后在`petalinux-config -c rootfs`中的user layer中就会出现nginx 选择然后再进行编译即可