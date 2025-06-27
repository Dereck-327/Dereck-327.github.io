---
title: Qt
date: 2025-05-12 12:47:20
tags: [qt]            
categories: [open source]        
top_img:
cover:  
---

# 关于Qt的一些笔记

## Qt Creator的安装用清华镜像

注意qt的账户分商务和社区版本  
<https://mirrors.tuna.tsinghua.edu.cn/qt/>

## moveToThread

moveToThread 设置线程亲和性的作用是将对象及其事件处理关联到指定的线程。在 Qt 等框架中，每个对象默认属于创建它的线程，只有属于某个线程的对象才能在该线程中安全地接收和处理事件。
