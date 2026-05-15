---
title: Rust        
data: 2026-04-30 15:11:23
tags: [rust]             
categories: [programming]              
description: comments & documentation
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# 代码注释

```rs
// 行注释
/*
 块注释
*/
```

# 文档注释

- 文档注释需要位于 lib 类型的包中，例如 src/lib.rs 中
- 文档注释可以使用 markdown语法！例如 # Examples 的标题，以及代码块高亮
- 被注释的对象需要使用 pub 对外可见，记住：文档注释是给用户看的，内部实现细节不应该被暴露出去

```rs
/// 文档注释
/// `add_one` 将指定值加1
///
/// # Examples
///
/// ```
/// let arg = 5;
/// let answer = my_crate::add_one(arg);
///
/// assert_eq!(6, answer);
/// ```
pub fn add_one(x: i32) -> i32 {
    x + 1
}

```

文档块注释

```rs
/** `add_two` 将指定值加2

# Examples

```
let arg = 5;
let answer = my_crate::add_two(arg);

assert_eq!(7, answer);
```
*/
pub fn add_two(x: i32) -> i32 {
    x + 2
}
```

`cargo test` 可以运行文档中的测试

## 查看文档doc

```bash
# 生成  target/doc目录下
cargo doc
# 生成并打开 
cargo doc --open
```


# 包和模块注释

```rs
//! 行注释

/*!
 块注释
*/
```

# 保留测试 隐藏文档

在某些时候，希望保留文档测试的功能，但是又要将某些测试用例的内容从文档中隐藏起来：  

```rs
/// ```
/// # // 使用#开头的行会在文档中被隐藏起来，但是依然会在文档测试中运行
/// # fn try_main() -> Result<(), String> {
/// let res = world_hello::compute::try_div(10, 0)?;
/// # Ok(()) // returning from try_main
/// # }
/// # fn main() {
/// #    try_main().unwrap();
/// #
/// # }
/// ```
pub fn try_div(a: i32, b: i32) -> Result<i32, String> {
    if b == 0 {
        Err(String::from("Divide-by-zero"))
    } else {
        Ok(a / b)
    }
}
```

# 文档注释中的代码跳转

```rs
/// `add_one` 返回一个[`Option`]类型
pub fn add_one(x: i32) -> Option<i32> {
    Some(x + 1)
}
```

```rs
use std::sync::mpsc::Receiver;

/// [`Receiver<T>`]   [`std::future`].
///
///  [`std::future::Future`] [`Self::recv()`].
pub struct AsyncReceiver<T> {
    sender: Receiver<T>,
}

impl<T> AsyncReceiver<T> {
    pub async fn recv() -> T {
        unimplemented!()
    }
}

pub mod a {
    /// `add_one` 返回一个[`Option`]类型
    /// 跳转到[`crate::MySpecialFormatter`]
    pub fn add_one(x: i32) -> Option<i32> {
        Some(x + 1)
    }
}

pub struct MySpecialFormatter;
```

```rs
/// 跳转到结构体  [`Foo`](struct@Foo)
pub struct Bar;

/// 跳转到同名函数 [`Foo`](fn@Foo)
pub struct Foo {}

/// 跳转到同名宏 [`foo!`]
pub fn Foo() {}

#[macro_export]
macro_rules! foo {
  () => {}
}
```

# 文档搜索别名

```rs
#[doc(alias = "x")]
#[doc(alias = "big")]
pub struct BigX;

#[doc(alias("y", "big"))]
pub struct BigY;
```
