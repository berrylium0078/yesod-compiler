# testsuit-collection

## lv1 - lv9

复制自 autotest

## lvX

克隆自[仓库](https://github.com/jokerwyt/sysy-testsuit-collection)，我删掉了一些性能测试用例，只保留了解释器能在 2 秒内解释执行的。此外还删除了一个包含超长语句的程序 - 我的编译器在试图解析时递归爆栈了。

在我的电脑上，剩余的测试点解释运行总时间大约是 20~40 秒。

## poly

用 AI 生成的测试用例，包含一些多项式长度临界情况、以及多项式全家桶（并不全……）的参考实现。