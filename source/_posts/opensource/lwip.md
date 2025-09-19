---
title: Eigen
date: 2025-05-12 13:47:20
tags: [logger]            
categories: [open source]        
top_img:
cover:  
---

# lwip ethernet

## mac addr

IPV4的全网广播地址是 FF:FF:FF:FF:FF:FF  
MAC 地址的结构：OUI + NIC  

|前 3 个字节 (24 位)                     | 后 3 个字节 (24 位)|
|----------------------------------------|-------------------|
|OUI (Organizational Unique Identifier)  |NIC 部分 (Network Interface Controller)|
|组织唯一标识符                           |网络接口控制器标识符|
|标识 设备的制造商                       |由制造商分配的唯一序列号|
|由 IEEE 统一分配和管理                   |由制造商自行分配，确保在其 OUI 下唯一|

### OUI

OUI 是由 IEEE（电气与电子工程师学会） 的注册管理机构 RA（Registration Authority） 官方分配给设备制造商的一个全球唯一的标识符。  

- 特殊位：U/L 和 I/G  
  在第一个字节中，有两个非常重要的位（最低有效位），它们定义了地址的属性：  
  - I/G (Individual/Group) Bit：  
    位置：第一个字节的 第 0 位（即最右边的一位）。  
    0：表示这是一个单播地址（Unicast），地址指向网络中的一个特定设备。  
    1：表示这是一个组播地址（Multicast），地址指向一组设备（在以太网中）或一个广播地址（Broadcast，即 FF:FF:FF:FF:FF:FF）。  

  - U/L (Universal/Local) Bit：  
    位置：第一个字节的 第 1 位。  
    0：表示这是一个全局管理地址（Universally Administered Address, UAA）。这是默认情况，即由 IEEE 分配的正常地址。  
    1：表示这是一个本地管理地址（Locally Administered Address, LAA）。这个地址是由网络管理员手动覆盖的，而不是制造商烧录的地址。这在虚拟化或某些网络配置中很有用。

### NIC

由获得 OUI 的制造商自行分配的序列号。制造商负责确保在他们所拥有的每一个 OUI 下，为所生产的每一个网络接口控制器（网卡）分配一个唯一的、不重复的值。  
制造商会在生产过程中，将唯一的 MAC 地址烧录到网卡的 ROM 中。这被称为 BIA（Burned-In Address）。  
通常，制造商会使用一个简单的序列计数器，每生产一个网卡就递增一次，以确保不会重复

## multicast api
