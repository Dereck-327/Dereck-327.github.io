# vscode

## makefile

### 使用python的`compiledb`生成`compile_command.json`

```bash
uv pip install compiledb
compiledb make
```

### bear

```bash
make clean && bear -- make
```

## .clangd

```.clangd
CompileFlags:
  # arm-none-eabi-gcc 的 driver，让 clangd 用 GCC 头文件搜索路径
  Compiler: /opt/toolchains/arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-gcc
  Add:
    - --target=arm-none-eabi
  Remove:
    # libclang 不认的 GCC-only 参数，去掉避免假报错
    - --inline
    - -mcpu=*
    - -mthumb*
    - -l*
    - -fdata-sections
    - -ffunction-sections
```
