---
title: Rust        
data: 2026-04-22 09:44:11
tags: [rust]             
categories: [programming]              
description: program  
top_img: /image/jizi.png 
cover: /image/动漫少女.jpg   
---

# type 

## Option<T>

Option<T>: 枚举类型，处理缺失值的核心, 取代 null 

```rs
// Option 定义
enum Option<T> {
    Some(T), // 盒子装了一个类型为 T 的值
    None,    // 盒子是空的
}
```

Some(T)：代表“有值”。T 是具体的数据类型(比如 i32、String 等)  
None：代表“没值”。它是专门用来表示“什么都没有”的占位符  


# 生命周期

## 静态生命周期 `'static`

```rs
#![allow(unused)]
fn main() {
// String literals are always 'static — they live in the binary's read-only section
let s: &'static str = "hello";  // Same as: static const char* s = "hello"; in C

// Constants are also 'static
static GREETING: &str = "hello";

// Common in trait bounds for thread spawning:
fn spawn<F: FnOnce() + Send + 'static>(f: F) { /* ... */ }
// 'static here means: "the closure must not borrow any local variables"
// (either move them in, or use only 'static data)
}
```

### 静态字符串字面量 `&'static str`

在代码中直接写出的字符串字面量（如 "hello"）是硬编码在二进制文件里的。  
- 存储位置： 存储在编译后可执行文件的只读数据段 (Read-only Data Segment) 中。  
- 生命周期： 当程序加载到内存中时，这些数据就已经存在了，直到程序结束才会被释放。因此，不需要在堆上分配空间，也不需要手动释放。  
- 结论： 所有的字符串字面量默认都是 `'static` 的。这意味着你可以将它们传递给任何要求 `'static` 生命周期的地方，编译器会允许这种引用。  

### 静态变量 `static`

使用 static 关键字定义的变量与字符串字面量类似。  

```rs
static GREETING: &str = "hello";
```

- 全局可见性： static 变量是全局的，它们在内存中拥有固定的地址。  
- 线程安全： 由于 static 变量是全局的，如果它们是可变的（static mut），多个线程同时修改它们会导致数据竞争。因此，Rust 默认要求 static 变量必须满足 Sync trait（即在多线程间共享是安全的）。  
- 生命周期： 它们在程序的整个运行期间都保持有效。  
  
### Trait Bound (特征约束)中的 `'static`

在函数签名中出现 'static 时，意义发生了变化，不再仅仅指"存在于二进制文件中的数"。  

```rs
/*
* 泛型函数 F是占位符 
* `<F: FnOnce() + Send + 'static>` 是特征约束 传入的F必须同时满足这三个条件 否则编译器会报错
* FnOnce() : 表示f是一个闭包函数 且至少可以被调用一次
*   当定义 |...| { ... } 时，编译器会自动为这个闭包实现 Fn、FnMut 或 FnOnce。
*   FnOnce 是最宽松的（它允许闭包消耗/移动所捕获的变量），这在多线程场景下非常重要，因为需要把数据“转移”进新线程。
* Send : 表示该类型的所有权可以在线程间安全地转移 
*   Rust 的内存安全机制: 
*      不是所有数据都可以在线程间随意传递（例如 Rc<T> 就不满足 Send，因为它内部的引用计数不是线程安全的）。如果你想把闭包送到新线程，闭包本身必须是 Send 的
*/
fn spawn<F: FnOnce() + Send + 'static>(f: F)
```

当 `'static` 用于 `Trait Bound`（例如 T: `'static`）时，它的意思是："类型 T 中包含的所有引用，其生命周期必须至少是 `'static` 的"  
意思就是 该类型不能包含任何"非静态"的引用  

满足该`'static`  
- 不捕获任何引用： 如果闭包内部没有使用任何外部变量，它是 `'static` 的  
- 使用 `move` 关键字： 通过 `move` 将变量的所有权转移到闭包内部。此时闭包持有的是数据本身，而不是对数据的"引用"，因此它不再受局部变量生命周期的约束  
- 数据本身就是 `'static` 的： 比如指向全局常量的引用 

# 智能指针

## 何时使用 `Box` 分配 vs 栈分配

- 包含的类型很大， 不想复制在栈上    
- 需要递归类型（例如包含自身的链表节点）
- 需要特征对象（`Box<dyn Trait>` ）
- 用于创建指向堆分配类型的指针。无论类型如何，指针始终是固定大小的<T>

## Borrowing Rules Visualization

不能在可变借用（&mut）存在时再访问之前的不可变借用（&），这是 Rust 的借用规则（Borrow Checker）保证内存安全的核心机制。  

```rs
fn borrowing_rules_example() {
    let mut data = vec![1, 2, 3, 4, 5];
    
    // Multiple immutable borrows - OK
    let ref1 = &data;
    let ref2 = &data;
    println!("{:?} {:?}", ref1, ref2);  // Both can be used
    
    // Mutable borrow - exclusive access
    let ref_mut = &mut data;
    ref_mut.push(6);
    // ref1 and ref2 can't be used while ref_mut is active
    
    // After ref_mut is done, immutable borrows work again
    let ref3 = &data;
    println!("{:?}", ref3);
    // println!("{:?}", ref1); // error 不能在可变借用（&mut）存在时再访问之前的不可变借用（&）
}
```

## Interior Mutability: Cell<T> and RefCell<T>

### Cell<T> — interior mutability for Copy types

- 提供内部可变性，即对引用的特定元素进行写访问，否则这些元素是只读的  
- 通过复制值进行工作（需要 T: Copy for .get()）  

### RefCell<T> — interior mutability with runtime borrow checking

RefCell<T> provides a variation that works with references  
- Enforces Rust borrow-checks at runtime instead of compile-time
- Allows a single mutable borrow, but panics if there are any other references outstanding
- Use .borrow() for immutable access and .borrow_mut() for mutable access

### Shared Ownership: Rc<T>

Rc<T>: Reference Counted  
Arc<T>（Atomic Reference Counted，原子引用计数）

允许不可变数据的引用计数共享所有权  

```rs
use std::rc::Rc;
#[derive(Debug)]
struct Employee {employee_id: u64}
fn main() {
    let mut us_employees = vec![];
    let mut all_global_employees = vec![];
    let employee = Employee { employee_id: 42 };
    let employee_rc = Rc::new(employee);
    us_employees.push(employee_rc.clone());
    all_global_employees.push(employee_rc.clone());
    let employee_one = all_global_employees.get(0); // Shared immutable reference
    for e in us_employees {
        println!("{}", e.employee_id);  // Shared immutable reference
    }
    println!("{employee_one:?}");
}
```

![Smart Pointer Mapping](image/programming/rust/SmartPointerMapping.png)

| 指针类型 | 所有权模式         | 线程安全 | 是否可修改          |
|----------|--------------------|----------|---------------------|
| Box<T>   | 独占所有权         | 是       | 是                  |
| Rc<T>    | 共享所有权（单线程）| 否       | 否（需加 RefCell）  |
| Arc<T>   | 共享所有权（多线程）| 是       | 否（需加 Mutex/RwLock） |

### Breaking Reference Cycles with Weak<T>

用weak打破循环引用

```rs
use std::rc::{Rc, Weak};

struct Node {
    value: i32,
    parent: Option<Weak<Node>>,  // Weak reference — doesn't prevent drop
}

fn main() {
    let parent = Rc::new(Node { value: 1, parent: None });
    let child = Rc::new(Node {
        value: 2,
        parent: Some(Rc::downgrade(&parent)),  // Weak ref to parent
    });

    // To use a Weak, try to upgrade it — returns Option<Rc<T>>
    if let Some(parent_rc) = child.parent.as_ref().unwrap().upgrade() {
        println!("Parent value: {}", parent_rc.value);
    }
    println!("Parent strong count: {}", Rc::strong_count(&parent)); // 1, not 2
}
```

## 总结

| 智能指针类型 | 所有权模式               | 线程安全 | 是否可修改          | 主要用途                                                                 |
|--------------|--------------------------|----------|---------------------|--------------------------------------------------------------------------|
| `Box<T>`     | 独占所有权               | 是       | 是                  | 用于堆分配内存，适合递归类型、特征对象等。                               |
| `Rc<T>`      | 共享所有权（单线程）     | 否       | 否（需加 `RefCell`）| 单线程环境下的引用计数，用于不可变数据的共享所有权。                     |
| `Arc<T>`     | 共享所有权（多线程）     | 是       | 否（需加 `Mutex`/`RwLock`） | 多线程环境下的引用计数，用于不可变数据的共享所有权。                     |
| `RefCell<T>` | 独占所有权（内部可变性） | 否       | 是                  | 运行时借用检查，允许在不可变引用下进行可变操作（单线程）。               |
| `Cell<T>`    | 独占所有权（内部可变性） | 否       | 是                  | 提供内部可变性，适用于 `Copy` 类型，避免引用。                           |
| `Mutex<T>`   | 独占所有权               | 是       | 是                  | 提供线程间的互斥锁，确保数据在多线程环境下的安全修改。                   |
| `RwLock<T>`  | 独占所有权               | 是       | 是                  | 提供读写锁，允许多个线程同时读取或一个线程写入。                         |
| `Weak<T>`    | 弱引用（避免循环引用）   | 是（配合 `Arc`） | 否                  | 配合 `Rc` 或 `Arc` 使用，避免循环引用导致内存泄漏。                     |
| `Cow<T>`     | 借用或拥有               | 是       | 否                  | 用于高效的只读操作，支持在需要时进行克隆以获得所有权。                   |
| `Pin<T>`     | 固定内存位置             | 是       | 否                  | 用于确保数据在内存中的位置不会改变，常用于异步编程。                     |


# Crates dependencies and SemVer

## At least 0.10.0, but anything < 0.11.0 is fine

```toml
[dependencies]
rand = { version = "0.10.0"}
```

## Only 0.10.0, and nothing else

```toml
[dependencies]
rand = { version = "=0.10.0"}
```

## Don’t care; cargo will select the latest version

```toml
[dependencies]
rand = { version = "*"}
```

# Build Profiles: Controlling Optimization

## profiles

| C/GCC Flag       | Cargo.toml Key  | Values                          |
|-------------------|-----------------|---------------------------------|
| `-O0` / `-O2` / `-O3` | `opt-level`    | 0, 1, 2, 3, "s", "z"           |
| `-flto`          | `lto`           | false, "thin", "fat"            |
| `-g` / no `-g`   | `debug`         | true, false, "line-tables-only" |
| `strip` command  | `strip`         | "none", "debuginfo", "symbols", true/false |
| —                 | `codegen-units` | 1 = best opt, slowest compile   |

### opt-level

- 0: 无优化
- 1：轻量优化
- 2：平衡优化
- 3：最大化性能
- "s": 减小二进制文件的大小（优化运行时性能的同时尽量减小生成的二进制文件的大小）
- "z": 优化完全偏向于减小文件大小，可能会牺牲部分运行时性能

| opt-level | 优化目标               | 编译速度 | 运行时性能 | 二进制大小 | 适用场景                           |
|-----------|------------------------|----------|------------|------------|------------------------------------|
| 0         | 无优化                 | 最快     | 最慢       | 最大       | 开发和调试阶段                     |
| 1         | 轻量优化               | 较快     | 较慢       | 较大       | 性能测试或快速开发                 |
| 2         | 平衡优化               | 中等     | 快         | 中等       | 生产环境的默认选择                 |
| 3         | 最大化性能             | 最慢     | 最快       | 较大       | 高性能计算或实时系统               |
| "s"       | 减小二进制文件大小     | 中等     | 较快       | 较小       | 嵌入式开发或存储空间有限的场景     |
| "z"       | 极限压缩二进制文件大小 | 中等     | 较慢       | 最小       | 微控制器或极端存储受限的场景       |


```toml
# Cargo.toml — build profile configuration

[profile.dev]
opt-level = 0          # No optimization (fast compile, like -O0)
debug = true           # Full debug symbols (like -g)

[profile.release]
opt-level = 3          # Maximum optimization (like -O3)
lto = "fat"            # Link-Time Optimization (like -flto)
strip = true           # Strip symbols (like the strip command)
codegen-units = 1      # Single codegen unit — slower compile, better optimization
panic = "abort"        # No unwind tables (smaller binary)
```

```bash
cargo build              # Uses [profile.dev]
cargo build --release    # Uses [profile.release]
```

# Build Scripts (build.rs): Linking C Libraries

## c/cxx源码在项目

You can even compile C source files directly from a Rust crate  

### 添加构建依赖

```toml
...
[build-dependencies]
cc = "1"
```

```rs
// build.rs
fn main() {
    cc::Build::new()
        .file("src/c_helpers/ipmi_raw.c")
        .include("/usr/include/bmc")
        .compile("ipmi_raw");   // Produces libipmi_raw.a, linked automatically
    println!("cargo::rerun-if-changed=src/c_helpers/ipmi_raw.c");
}
```

## 链接已存在的库

在 crate 根目录使用 build.rs 文件：  

```rs
// build.rs — runs before compiling the crate

fn main() {
    // Link a system C library (like -lbmc_ipmi in gcc)
    println!("cargo::rustc-link-lib=bmc_ipmi");

    // Link cxx 标准库
    println!("cargo:rustc-link-lib=stdc++");

    // Where to find the library (like -L/usr/lib/bmc)
    println!("cargo::rustc-link-search=/usr/lib/bmc");

    // Re-run if the C header changes
    println!("cargo::rerun-if-changed=wrapper.h");
}
```

# Cross-Compilation

```bash
# Install a cross-compilation target
rustup target add aarch64-unknown-linux-gnu

# Cross-compile
cargo build --target aarch64-unknown-linux-gnu --release
```

在 .cargo/config.toml 中指定链接器：  

```toml
[target.aarch64-unknown-linux-gnu]
linker = "aarch64-linux-gnu-gcc"
```

| C Cross-Compile                  | Rust Equivalent                                                                 |
|----------------------------------|---------------------------------------------------------------------------------|
| `apt install gcc-aarch64-linux-gnu` | `rustup target add aarch64-unknown-linux-gnu` + 安装对应的链接器                  |
| `CC=aarch64-linux-gnu-gcc make`  | `.cargo/config.toml` 中 `[target.X] linker = "..."`                             |
| `#ifdef __aarch64__`             | `#[cfg(target_arch = "aarch64")]`                                               |
| Separate Makefile targets        | `cargo build --target ...`                                                      |

---



# trait 特征

```rs
pub trait Summary {
    fn summarize(&self) -> String;
}
```

特征只定义行为看起来是什么样的，而不定义行为具体是怎么样的. 只定义特征方法的签名，而不进行实现，此时方法签名结尾是 ;，而不是一个 {}。  
关于特征实现与定义的位置需要满足孤儿规则:
    - 如果想要为类型 A 实现特征 T，那么 A 或者 T 至少有一个是在当前作用域中定义的！  
可以确保其它人编写的代码不会破坏你的代码，也确保了你不会莫名其妙就破坏了风马牛不相及的代码  

impl Trait : 只是一个语法糖

## 使用特征作为函数参数

```rs
// impl Summary : 实现了Summary特征 的 item 参数
pub fn notify(item: &impl Summary) {
    println!("Breaking news! {}", item.summarize());
}
```

## `trait bound`特征约束

形如 T: Summary 被称为特征约束。  

```rs
pub fn notify<T: Summary>(item: &T) {
    println!("Breaking news! {}", item.summarize());
}
```

### 多重约束

```rs
// 语法糖形式  
pub fn notify(item: &(impl Summary + Display)) {}

// 特征约束形式
pub fn notify<T: Summary + Display>(item: &T) {}
```

### Where 约束

当特征约束变得很多时，函数的签名将变得很复杂  

```rs
fn some_function<T: Display + Clone, U: Clone + Debug>(t: &T, u: &U) -> i32 {}
```

使用where约束改进  

```rs
fn some_function<T, U>(t: &T, u: &U) -> i32
    where T: Display + Clone,
          U: Clone + Debug
{}
```

### 函数返回中的 impl Trait

可以通过 impl Trait 来说明一个函数返回了一个类型，该类型实现了某个特征：  

```rs
fn returns_summarizable() -> impl Summary {
    Weibo {
        username: String::from("sunface"),
        content: String::from(
            "xxx",
        )
    }
}
```

这种 impl Trait 形式的返回值，在一种场景下非常非常有用，那就是返回的真实类型非常复杂，你不知道该怎么声明时（毕竟 Rust 要求你必须标出所有的类型），此时就可以用 impl Trait 的方式简单返回。例如，闭包和迭代器就是很复杂，只有编译器才知道那玩意的真实类型，如果让你写出来它们的具体类型，估计内心有一万只草泥马奔腾，好在你可以用 impl Iterator 来告诉调用者，返回了一个迭代器，因为所有迭代器都会实现 Iterator 特征  

但是这种返回值方式有一个很大的限制：只能有一个具体的类型，例如：  

```rs
fn returns_summarizable(switch: bool) -> impl Summary {
    if switch {
        Post {
            title: String::from(
                "Penguins win the Stanley Cup Championship!",
            ),
            author: String::from("Iceburgh"),
            content: String::from(
                "The Pittsburgh Penguins once again are the best \
                 hockey team in the NHL.",
            ),
        }
    } else {
        Weibo {
            username: String::from("horse_ebooks"),
            content: String::from(
                "of course, as you probably already know, people",
            ),
        }
    }
}
```

### 通过 `derive` 派生特征

形如 `#[derive(Debug)]` 的代码  
- 这种是一种特征派生语法，被 `derive` 标记的对象会自动实现对应的默认特征代码，继承相应的功能。  
例如 Debug 特征，它有一套自动实现的默认代码，当你给一个结构体标记后，就可以使用 println!("{:?}", s) 的形式打印该结构体的对象  

但是在实际项目中，往往需要对我们的自定义类型进行自定义的格式化输出，以让用户更好的阅读理解我们的类型，此时就要为自定义类型实现 std::fmt::Display 特征：  

```rs
#![allow(dead_code)]

use std::fmt;
use std::fmt::{Display};

#[derive(Debug,PartialEq)]
enum FileState {
  Open,
  Closed,
}

#[derive(Debug)]
struct File {
  name: String,
  data: Vec<u8>,
  state: FileState,
}

impl Display for FileState {
   fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
     match *self {
         FileState::Open => write!(f, "OPEN"),
         FileState::Closed => write!(f, "CLOSED"),
     }
   }
}

impl Display for File {
   fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
      write!(f, "<{} ({})>",
             self.name, self.state)
   }
}

impl File {
  fn new(name: &str) -> File {
    File {
        name: String::from(name),
        data: Vec::new(),
        state: FileState::Closed,
    }
  }
}

fn main() {
  let f6 = File::new("f6.txt");
  //...
  println!("{:?}", f6);
  println!("{}", f6);
}
```

### 特征对象

可以使用特征对象来代表泛型或具体的类型。  

```rs
trait Draw {
    fn draw(&self) -> String;
}

impl Draw for u8 {
    fn draw(&self) -> String {
        format!("u8: {}", *self)
    }
}

impl Draw for f64 {
    fn draw(&self) -> String {
        format!("f64: {}", *self)
    }
}

// 若 T 实现了 Draw 特征， 则调用该函数时传入的 Box<T> 可以被隐式转换成函数参数签名中的 Box<dyn Draw>
fn draw1(x: Box<dyn Draw>) {
    // 由于实现了 Deref 特征，Box 智能指针会自动解引用为它所包裹的值，然后调用该值对应的类型上定义的 `draw` 方法
    x.draw();
}

fn draw2(x: &dyn Draw) {
    x.draw();
}

fn main() {
    let x = 1.1f64;
    // do_something(&x);
    let y = 8u8;

    // x 和 y 的类型 T 都实现了 `Draw` 特征，因为 Box<T> 可以在函数调用时隐式地被转换为特征对象 Box<dyn Draw> 
    // 基于 x 的值创建一个 Box<f64> 类型的智能指针，指针指向的数据被放置在了堆上
    draw1(Box::new(x));
    // 基于 y 的值创建一个 Box<u8> 类型的智能指针
    draw1(Box::new(y));
    draw2(&x);
    draw2(&y);
}
```

### 鸭子类型(duck typing)

只关心值长啥样，而不关心它实际是什么: 当一个东西走起来像鸭子，叫起来像鸭子，那么它就是一只鸭子，就算它实际上是一个奥特曼，也不重要，我们就当它是鸭子  


### 特征对象的动态分发 dynamic dispatch

泛型是在编译期完成处理的：编译器会为每一个泛型参数对应的具体类型生成一份代码，这种方式是静态分发(static dispatch)，因为是在编译期完成的，对于运行期性能完全没有任何影响。  
与静态分发相对应的是动态分发(dynamic dispatch)，在这种情况下，直到运行时，才能确定需要调用什么方法。之前代码中的关键字 `dyn` 正是在强调这一“动态”的特点。  

### 特征对象的限制

#### 对象安全的特征

不是所有特征都能拥有特征对象，只有对象安全的特征才行。当一个特征的所有方法都有如下属性时，它的对象才是安全的：  

- 方法的返回类型不能是 Self
- 方法没有任何泛型参数

对象安全对于特征对象是必须的，因为一旦有了特征对象，就不再需要知道实现该特征的具体类型是什么了。如果特征方法返回了具体的 Self 类型，但是特征对象忘记了其真正的类型，那这个 Self 就非常尴尬，因为没人知道它是谁了。但是对于泛型类型参数来说，当使用特征时其会放入具体的类型参数：此具体类型变成了实现该特征的类型的一部分。而当使用特征对象时其具体类型被抹去了，故而无从得知放入泛型参数类型到底是什么。  

标准库中的 Clone 特征就不符合对象安全的要求：

```rs
pub trait Clone {
    fn clone(&self) -> Self;
}
```

### 关联类型

关联类型是在特征定义的语句块中，声明一个自定义类型，这样就可以在特征的方法签名中使用该类型：

```rs
pub trait Iterator {
    type Item;

    fn next(&mut self) -> Option<Self::Item>;
}
```

### 完全限定语法

完全限定语法是调用函数最为明确的方式  

```rs
<Type as Trait>::function(receiver_if_method, next_arg, ...);
// 第一个参数是方法接收器 receiver （三种 self），只有方法才拥有，例如关联函数就没有 receiver。
```

### 特征定义中的特征约束

有时，我们会需要让某个特征 A 能使用另一个特征 B 的功能(另一种形式的特征约束)，这种情况下，不仅仅要为类型实现特征 A，还要为类型实现特征 B 才行，这就是基特征( super trait )  

```rs
use std::fmt::Display;

trait OutlinePrint: Display {
    fn outline_print(&self) {
        let output = self.to_string();
        let len = output.len();
        println!("{}", "*".repeat(len + 4));
        println!("*{}*", " ".repeat(len + 2));
        println!("* {} *", output);
        println!("*{}*", " ".repeat(len + 2));
        println!("{}", "*".repeat(len + 4));
    }
}
```

和特征约束非常类似，都用来说明一个特征需要实现另一个特征，这里就是：如果你想要实现 OutlinePrint 特征，首先你需要实现 Display 特征。  


# Vector 常见的一些方法示例

```rs
let mut v =  vec![1, 2];
assert!(!v.is_empty());         // 检查 v 是否为空

v.insert(2, 3);                 // 在指定索引插入数据，索引值不能大于 v 的长度， v: [1, 2, 3] 
assert_eq!(v.remove(1), 2);     // 移除指定位置的元素并返回, v: [1, 3]
assert_eq!(v.pop(), Some(3));   // 删除并返回 v 尾部的元素，v: [1]
assert_eq!(v.pop(), Some(1));   // v: []
assert_eq!(v.pop(), None);      // 记得 pop 方法返回的是 Option 枚举值
v.clear();                      // 清空 v, v: []

let mut v1 = [11, 22].to_vec(); // append 操作会导致 v1 清空数据，增加可变声明
v.append(&mut v1);              // 将 v1 中的所有元素附加到 v 中, v1: []
v.truncate(1);                  // 截断到指定长度，多余的元素被删除, v: [11]
v.retain(|x| *x > 10);          // 保留满足条件的元素，即删除不满足条件的元素

let mut v = vec![11, 22, 33, 44, 55];
// 删除指定范围的元素，同时获取被删除元素的迭代器, v: [11, 55], m: [22, 33, 44]
let mut m: Vec<_> = v.drain(1..=3).collect();    

let v2 = m.split_off(1);        // 指定索引处切分成两个 vec, m: [22], v2: [33, 44]
```


[Testing Patterns and Strategies](Testing.md)
[注释和文档](comments_doc.md)
[闭包](clousure.md)






