---
title: effective python 
date: 2025-05-12 15:10:47
tags: [python]
categories: [编程语言]
top_img:
cover:
---


## 单例类的实现
单例类的实现需要注意几点:<br>
1. 显式创建单例，重写__new__方法
2. 确保线程安全，加线程锁

```python
import threading
class Test:
    _instance = None
    _lock = threading.Lock()

    def __new__(cls, *args, **kwargs):
        if not cls._instance:
            with cls._lock:
                if not cls._instance:
                    cls._instance = super(Test, cls).__new__(cls, *args, **kwargs)
        return cls._instance

    def __init__(self):
        # 初始化代码，确保只会在第一次实例化时执行
        if not hasattr(self, '_initialized'):
            # 初始化代码
            self._initialized = True

if __name__ == "__main__" :
    instance1 = Test()
    instance2 = Test()
    print(instance1 is instance2)  # True
```

## 元类
元类是用来创建类的类，普通的类是创建对象的(其元类是type)，元类是用来创建类的
类对象在实例化的时候会触发元类的__call__ 函数，然后再是构造函数 所以用元类管理的单例类
不能在单例类的构造函数中实例化其元类是同一个的单例类。
用元类管理单例类比较复杂，不如直接用上面的(但需要保证继承顺序是线性的)。但如果这个单例需要多继承的话，用元类更合适(不满足MRO继承链)
因为元类不占用继承链




## 类对象实例化过程
1. __new__ 申请空间
2. __init__ 构造函数，初始化实例
3. return创造的实例

## 装饰器decorator
装饰方法
```python
def myDecorator(func):
    def wrapper(*args, **kwargs):
        print("Something before the function runs")
        result = func(*args, **kwargs)
        print("Something after the function runs")
        return result
    return wrapper

@myDecorator
def say_hello(name):
    print(f"Hello, {name}!")

say_hello("jk")
```

类对象装饰
```python
class MyDecorator:
    def __init__(self, func):
        self.func = func

    def __call__(self, *args, **kwargs):
        print("Before function call")
        result = self.func(*args, **kwargs)
        print("After function call")
        return result

@MyDecorator
def hello(name) :
    print("hello ",name)

hello("jk")
```

## 类对象内置方法

### __str__
对一个对象调用 str() 或使用 print() 函数时，应该返回的字符串表示。__str__ 方法的返回值应该是一个易于阅读且对用户友好的字符串，目的是帮助用户理解对象的内容或状态。
如果没有为对象定义 __str__ 方法，Python 会退而调用 __repr__，但通常 __repr__ 更适合开发人员而不是普通用户。