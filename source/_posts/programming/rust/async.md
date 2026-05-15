---
title: Rust async        
data: 2026-04-23 15:12:32
tags: [rust]             
categories: [async]              
description: rust async  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# async/await

Rust 的 async/await 是一种协作式（Cooperative）异步编程模型。它允许你在等待 I/O 操作（如网络请求、数据库查询）时，挂起当前任务并释放 CPU，去处理其他任务，从而实现高并发。  

## Feature

async 函数不会立即执行，而是返回一个实现了 Future trait 的对象  
- 懒惰性（Lazy）：Future 本质上是一个状态机（State Machine）。当调用一个 async 函数时，它只会创建一个结构体，记录需要做什么，但不会执行任何代码。 
- 驱动（Polling）：只有当通过 .await 显式调用它，或者交给一个异步运行时（如 Tokio）去 poll（轮询）时，这个任务才开始真正向前推进。  

## async & await

- async fn：定义一个异步函数。它会将函数体编译为一个包含所有局部变量的状态机  
- .await：告诉运行时：“我在这里暂停，直到这个任务完成。如果没完成，请先去运行别的任务”
  
### 协作式调度

Rust 的异步模型有一个显著特点：Rust 标准库不包含异步运行时（Runtime）。  
这是 Rust 的设计哲学——“零成本抽象”。如果你不需要异步，你的二进制文件里就不会包含沉重的运行时。要运行 async 代码，你需要一个执行器（Executor），最常用的是 Tokio。  
执行器 (Executor)：负责不断调用 poll。如果 Future 还没完成（返回 Pending），它就挂起；如果完成了（返回 Ready），它就继续后续逻辑。  
任务 (Task)：Tokio 提供了 tokio::spawn，它类似于线程，但极其轻量（只有几百字节）。你可以轻松启动成千上万个异步任务，而不会压垮系统。  


