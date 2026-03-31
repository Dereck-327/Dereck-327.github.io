---
title: gdb & core-dump        
data: 2026-01-19 10:44:27
tags: [os]             
categories: [yocto]              
description: yocto system  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# gdb 

## 基础操作

```bash
gdb ./program
# 退出用q或者quit
```

```bash
list 或 l # ：查看当前行附近的源代码。
l 20 # ：查看第 20 行附近的代码。

run 或 r：开始运行程序。如果程序需要输入参数：r arg1 arg2

```
### 断点管理

```bash
break 15 或 b 15：在第 15 行设断点。
b func_name：在进入某个函数时停下。
info breakpoints：查看所有断点。
delete 1：删除编号为 1 的断点。

```

### 推进执行 (Step/Next)

```bash
next (n)： 单步执行（不进入函数内部，直接跳过）。
step (s)： 单步执行（进入函数内部）。
continue (c)： 继续运行，直到遇到下一个断点或程序结束。
finish： 运行完当前函数并返回到调用处。

```

### 查看变量 (Print/Watch)

```bash
print var (p var)： 查看变量 var 的当前值。
display var： 每次程序暂停时都自动显示该变量。
watch var： 监视变量。一旦 var 的值发生变化，程序立即停下。

```

### 查看调用栈 (Backtrace)

*** bt *** 当程序崩溃（如段错误）时

```bash
backtrace (bt)： 显示完整的函数调用链，告诉你程序是怎么一步步运行到当前报错位置的。
```

# 调试 Core Dump

当程序在后台异常终止时，系统会生成一个 core 文件（记录了内存快照）。  

```bash
gdb ./program core.1234
# 进入后直接bt
```


| 命令       | 缩写 | 功能         | 描述                     |
|------------|------|--------------|--------------------------|
| list       | l    | 列出源码     | 查看当前行附近的源代码   |
| run        | r    | 运行程序     | 开始运行程序             |
| break      | b    | 设置断点     | 在指定位置设置断点       |
| next       | n    | 逐过程       | 单步执行，跳过函数内部   |
| step       | s    | 逐语句       | 单步执行，进入函数内部   |
| print      | p    | 打印变量值   | 查看变量的当前值         |
| continue   | c    | 继续执行     | 运行到下一个断点         |
| backtrace  | bt   | 查看调用栈   | 显示函数调用链           |


# `vscode`和`gdbserver`远程调试

1. 远程终端进入程序目录  
   
  ```bash
gdbserver :1234 ./app
  ```

- `:1234` 表示监听所有网卡的 1234 端口。  
- 此时程序会进入等待状态，直到本地 VS Code 连接  

2. 本地 VS Code 配置调试文件

- 在 VS Code 中打开项目根目录，按下 F5，选择 C++ (GDB/LLDB)，会生成一个 .vscode/launch.json 文件。将其内容修改为如下配置：  

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "GDB Remote Debug",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/app", // 本地编译出的可执行文件路径
            "miDebuggerPath": "/usr/bin/gdb",    // 本地 GDB 路径（如果是交叉编译则指向交叉编译器的GDB）
            "miDebuggerServerAddress": "192.168.1.100:1234", // 远程机器 IP 和端口
            "cwd": "${workspaceFolder}",
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "为 gdb 启用整齐打印",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "sourceFileMap": {
                "/home/remote/project": "${workspaceFolder}" // 关键：映射远程路径到本地路径
            }
        }
    ]
}
```



