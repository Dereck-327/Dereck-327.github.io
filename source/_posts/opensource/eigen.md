---
title: Eigen
date: 2025-05-12 13:47:20
tags: [logger]            
categories: [open source]        
top_img:
cover:  
---

## 矩阵Matrix 模板参数
``` cpp
Matrix<typename Scalar,
       int RowsAtCompileTime,
       int ColsAtCompileTime,
       int Options = 0,
       int MaxRowsAtCompileTime = RowsAtCompileTime,
       int MaxColsAtCompileTime = ColsAtCompileTime>
```
Options 参数是指定矩阵使用行为主存储顺序，比如RowMajor(枚举) 就是指定行主序，一行在内存中是连续的。
MaxRowsAtCompileTime和MaxColsAtCompileTime在需要指定时非常有用，即使在编译时不知道矩阵的确切大小，但在编译时知道一个固定的上限。这样做的最大原因可能是为了避免动态内存分配。


## 点乘和叉乘
dot(), cross()

## 欧几里得范数
squaredNorm : 所有系数的平方之和
norm : 欧几里得范数的平方根
v.lpNorm<Eigen::Infinity>() : 无穷范数,向量中绝对值最大的元素的绝对值
v.lpNorm<1>() : L1范数，向量中各个元素的绝对值相加


## 按值传递和按引用传递
按值传递对象在 C++ 中几乎总是一个非常糟糕的用法，因为这会创建无用的副本，应该通过引用传递它们。
对于 Eigen，这一点更为重要：按值传递固定大小的可向量化 Eigen 对象不仅效率低下，而且可能是非法的或使程序崩溃！
原因是这些 Eigen 对象具有对齐修饰符，在按值传递时会不遵守这些修饰符


## colPivHouseholderQr 
用于执行基于 Householder 变换的列主元 QR 分解。该函数用于解决线性方程组、最小二乘问题以及其他涉及 QR 分解的数值计算任务。
### QR分解
QR 分解是一种常用的矩阵分解方法，用于将一个矩阵分解为一个正交矩阵和一个上三角矩阵的乘积。这种分解在数值计算和线性代数中有广泛的应用，例如求解线性方程组、最小二乘拟合、特征值计算等。
Q 是一个 m × m 的正交矩阵，即 Q<sup>T</sup>Q = QQ<sup>T</sup> = I，其中 I 是单位矩阵。R 是一个 m × n 的上三角矩阵，即 R 的主对角线以下的元素均为零。

### 智能指针
std::unique_ptr : 独占所有权，不允许多个unique_ptr共享一个对象。避免双重删除的情况，只能转移所有权。
通过std::move 转移所有权

std::shared_ptr : 共享所有权，可以多个shared_ptr共享一个对象，最后一个shared_ptr被销毁时，管理的对象才被销毁.
其设计主要针对单个对象，对处于同一块内存的对象有性能优化，对于数组不建议使用shared_ptr,用vector
``` cpp
// 创建shared_ptr
std::shared_ptr<class> obj_ptr = std::make_shared<class>();
```
std::weak_ptr : 非所有权指针，用于解决shared_ptr的循环引用，不增加引用计数。weak_ptr只能从shared_ptr构造


### explicit 关键字
防止构造函数和转化运算符的隐式转化
