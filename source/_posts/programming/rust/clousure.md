---
title: Rust clousure        
data: 2026-05-06 10:50:21
tags: [rust]             
categories: [clousure]              
description: clousure  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# 错误传播运算符`?`

```rs
let socket = UdpSocket::bind(bind_addr).map_err(MSG::Io)?;
```

如果表达式（如 UdpSocket::bind(bind_addr).map_err(MSG::Io)）返回 Ok(val)，就把 val 取出来继续执行后面的代码。  
如果返回 Err(e)，就立刻把 Err(e) 返回，终止当前函数，不再往下执行。  

如果 bind 成功，socket 就是绑定好的 UdpSocket。  
如果 bind 失败，错误会被 map_err(MSG::Io) 转换成 MSG::Io，然后 ? 直接把 Err(MSG::Io(...)) 返回给调用者。  




