# EPLIA Patches for NASM

升级 NASM 版本时，搜索 `EPLIA` 标记定位所有修改点：

```bash
grep -rn "EPLIA" --include="*.c" --include="*.h"
```

基线版本: NASM 3.02rc0

## 分类

### A. 上游 Bug (应提交 PR，合并后可移除)

| #   | 文件                              | 修改                      | 说明                                                                                 |
| --- | --------------------------------- | ------------------------- | ------------------------------------------------------------------------------------ |
| A1  | `nasmlib/file.c`                  | `os_mangle_filename` 注释 | MultiByteToWideChar 返回值是字符数不是字节数，原代码 `wclen << 1` 恰好正确但注释误导 |
| A2  | `asm/preproc.c` free_Token (两处) | 释放 `t->text.p.ptr`      | TOKEN_BLOCKSIZE!=0 和 ==0 两个分支都缺少对堆文本的释放                               |
| A3  | `asm/preproc.c` PP_DEFSTR         | `delete_tlist(tline)`     | `%defstr` 转换后未释放原始 token 列表，NASMX 每 pass 泄漏 ~1143 token                |
| A4  | `asm/labels.c` find_label         | `nasm_free(label_str)`    | 局部标签拼接字符串在提前返回时未释放                                                 |

### B. 库模式清理 (NASM 作为命令行工具不需要，嵌入使用必须)

| #   | 文件                                      | 修改                                     | 说明                                                      |
| --- | ----------------------------------------- | ---------------------------------------- | --------------------------------------------------------- |
| B1  | `asm/preproc.c`                           | 新增 `free_conds()`                      | 释放 Include 栈的条件链                                   |
| B2  | `asm/preproc.c`                           | 新增 `free_include()`                    | 完整释放 Include 栈条目 (conds/expansion/data/mmacro)     |
| B3  | `asm/preproc.c` pp_tokline                | 调用 `free_include(i, true)`             | 替换原来的 fclose + nasm_free                             |
| B4  | `asm/preproc.c` pp_cleanup_pass           | 调用 `free_include(i, false)`            | 替换原来的 fclose + nasm_free                             |
| B5  | `asm/preproc.c`                           | 新增 `free_filehash()`                   | 释放文件哈希表条目、路径缓冲区、键                        |
| B6  | `asm/preproc.c` pp_cleanup_session        | 调用 `free_macros()` + `free_filehash()` | 会话结束时释放宏定义和文件缓存                            |
| B7  | `asm/error.c`                             | 新增 `error_cleanup_session()`           | 释放 warning_stack 初始条目                               |
| B8  | `include/error.h`                         | 声明 `error_cleanup_session()`           | 头文件声明                                                |
| B9  | `asm/nasm.c` main()                       | 释放全局字符串                           | inname/outname/listname/errname/depend_target/depend_file |
| B10 | `include/nasmlib.h`                       | `#include "memleak.h"`                   | 内存泄漏检测集成入口                                      |
| B11 | `include/memleak.h` + `nasmlib/memleak.c` | 整个文件                                 | 内存泄漏检测模块 (新增文件，不修改上游)                   |

## 升级步骤

1. 更新子模块到新版本
2. `grep -rn "EPLIA"` 检查哪些修改被覆盖
3. A 类: 检查上游是否已修复，未修复则重新应用
4. B 类: 对照此表逐项重新应用 (结构体变化时需适配)
5. 新增文件 (memleak.h/memleak.c) 直接保留，不受上游更新影响
6. 构建并用 demo17.asm 测试: 期望 `No memory leaks detected!`
