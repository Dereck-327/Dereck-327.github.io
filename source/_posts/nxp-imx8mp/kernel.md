
# kernel standalone

## 运行时修改kernel日志级别

```bash
# 8 -> debug
sudo sh -c 'echo 8 > /proc/sys/kernel/printk' 
```


## gdb 查看coredump信息

```bash
# 打开coredump记录
ulimit -c unlimited
# 设置coredump文件路径
sudo sysctl -w kernel.core_pattern=/tmp/core-%e-%p-%t
# gdb 追踪
gdb ./bin /tmp/corefile
# 进入gdb后输入 bt 查看堆栈信息
...
(gb)bt
```

## 将行宽设置为 1000（或一个很大的数）

```bash
stty cols 200
```

## libgpiod

### 查看gpio组

```bash
# 查看系统中的所有 GPIO 芯片
gpiodetect
```
