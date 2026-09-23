---
title: Signal and System note      
data: 2026-06-16 10:41:56
tags: [dsp]           
categories: [DSP]            
description: Signal And System 
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg
---

# 定点数

## LSB

LSB: Least Significant Bit 最低有效位
在定点数（以及所有二进制数）中，LSB 是指二进制表示中最右侧的那一位，它代表了整个数值中最小的权重。    
截断，直接扔掉低位，不做四舍五入。这会引入最多 −1 LSB、平均约 −0.5 LSB 的系统性负偏置（DC bias）  

