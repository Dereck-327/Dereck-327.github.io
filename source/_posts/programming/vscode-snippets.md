
# vscode snippets

在`.vscode`下创建`.code-snippets`文件. e.g.  

```code-snippets
{
  // Broadcom 风格函数文档注释。触发词：输入 brcmdoc 后按 Tab
  "BRCM function doc comment": {
    "scope": "c,cpp",
    "prefix": "brcmdoc",
    "description": "Broadcom Doxygen-style function header (@brief/@param/@trace/@code)",
    "body": [
      "/**",
      "    @brief      ${1:one-line description}",
      "",
      "    @behaviour  ${2|Sync, Non-reentrant,Sync, Reentrant,Async, Non-reentrant|}",
      "",
      "    @pre        ${3:None}",
      "",
      "    @param[in]  ${4:aArg}   ${5:description}",
      "    @param[out] ${6:aOut}   ${7:description}",
      "",
      "    @post       ${8:None}",
      "",
      "    @retval     #BCM_ERR_OK     ${9:success}",
      "    @retval     #BCM_ERR_INVAL_PARAMS ${10:bad args}",
      "",
      "    @trace #BRCM_SWREQ_${11:MODULE}",
      "    @trace #BRCM_SWARCH_${12:MODULE_FUNC}_PROC",
      "",
      "    @code{.unparsed}",
      "    ${13:describe the logic here}",
      "    @endcode",
      "*/",
      "$0"
    ]
  }
}

```

# snippet 语法速查

| 写法 | 含义 |
| --- | --- |
| "prefix": "brcmdoc" | 触发词,输什么词唤出 |
| "scope": "c,cpp" | 只在这些语言生效(去掉则全语言) |
| "body": [...] | 每个数组元素是一行 |
| ${1:默认文本} | 占位符,Tab 跳转,数字是顺序 |
| ${2|A,B,C|} | 下拉选项(逗号分隔) |
| $1 和 $1 重复 | 同号占位符会联动同步输入 |
| $0 | 最终光标位置(每个 snippet 只能一个) |
| \t \\ \" | 制表符 / 反斜杠 / 引号需转义 |
| $TM_FILENAME $CURRENT_YEAR | 内置变量(文件名、年份等,可用于版权头) |


