# AttachThreadInput 对真实鼠标输入的影响分析

## 结论

FxPresser早期版本在全局开关开启期间长期执行 `AttachThreadInput`。实机验证发现，即使没有频繁调用 `SetKeyboardState`，长期Attach仍会明显影响游戏内真实鼠标点击和双击。

主要原因不是单个键盘状态值，而是Attach改变了两个线程的整体输入处理关系：游戏线程与工具线程开始共享输入状态，并按照同一输入队列组的顺序协作。任意一方未及时返回消息循环，都可能延后另一方后续的键盘或鼠标输入。

v3.1将Attach生命周期改为跟随当前有效按键方式：

- 当前方式为“共享消息”时自动Attach。
- 切换到“按键+自动窗口”或“按键+手动窗口”时自动Detach。
- 全局开始/停止只管理按键调度线程，不再管理Attach。

实机验证表明，这一调整明显改善了真实鼠标操作，说明长期Attach是此前输入干扰的主要来源。

## 1. Attach不只是共享键盘状态

Windows官方文档说明，`AttachThreadInput`会连接两个线程的输入处理机制。连接后线程可以共享：

- 键盘状态。
- 当前焦点窗口。
- 激活窗口。
- 鼠标捕获状态。
- 键盘和鼠标事件的处理顺序。

两个线程收到的键盘和鼠标事件需要按照到达顺序处理，直到调用 `AttachThreadInput(..., FALSE)`解除连接。

参考：

- [AttachThreadInput function](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput)
- [Creating Windows in Threads](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-windows-in-threads)

因此Attach不应理解为只给工具线程增加键盘状态访问权限。它更接近把游戏线程和工具线程放入同一个输入队列协作组。

## 2. 共享输入队列会引入线程间等待

未Attach时，两个GUI线程各自处理自己的输入：

```text
游戏线程输入队列 ── 独立处理游戏鼠标和键盘
工具线程输入队列 ── 独立处理工具消息
```

Attach后，两个线程需要共同遵守输入顺序：

```text
游戏线程 ─┐
          ├─ 共享输入队列组，按输入顺序协作
工具线程 ─┘
```

Windows为了保证输入顺序，不允许后面的输入消息随意越过前面的输入消息。只有拥有下一个输入消息的线程能够取走它，其他线程需要等待。

这使原本相互独立的异步输入处理，部分变成协作式、同步式处理。如果其中一个线程未及时回到 `GetMessage` 或 `PeekMessage`，另一个线程的后续输入也可能受到影响。

参考：

- [When you share an input queue, you have to wait your turn](https://devblogs.microsoft.com/oldnewthing/20130605-00/?p=4163)
- [Sharing an input queue takes what used to be asynchronous and makes it synchronous](https://devblogs.microsoft.com/oldnewthing/20130607-00/?p=4143)

## 3. 为什么鼠标双击尤其敏感

一次双击由多条有时序要求的输入构成：

```text
第一次按下
第一次释放
第二次按下
第二次释放
```

游戏还可能在过程中读取或维护：

- 当前焦点窗口。
- 鼠标捕获窗口。
- 鼠标按钮状态。
- 消息时间。
- 光标位置。
- 游戏自行记录的两次点击间隔。

如果长期Attach后某一方暂时没有继续处理输入，可能出现：

```text
第一次点击进入共享输入队列
  ↓
游戏线程执行逻辑、渲染或同步调用
  ↓
共享输入队列等待该线程完成本轮输入
  ↓
第二次点击未能及时交给游戏
  ↓
游戏将其识别为两次单击，而不是双击
```

微软提供的示例也说明，附加输入队列中一个未及时处理的键盘消息，可以继续阻塞属于另一个线程的鼠标点击。

参考：

- [A pathological program which ignores the keyboard](https://devblogs.microsoft.com/oldnewthing/20130606-00/?p=4153)

## 4. 工具线程有消息循环仍不能完全避免影响

旧实现已经给共享线程提供了Windows消息循环：

```text
GetMessage
TranslateMessage
DispatchMessage
```

这比没有消息循环安全，但不能消除Attach本身引入的同步关系。

共享线程执行一个按键时需要完成：

```text
收到自定义按键命令
GetKeyboardState
SetKeyboardState
SendMessageTimeout(DOWN)
等待释放间隔
SendMessageTimeout(UP)
返回消息循环
```

在完整按键方法返回前，共享线程暂时不会继续调用 `GetMessage`。即使默认释放间隔只有约27ms，也可能与真实鼠标点击重叠。

此外，游戏本身可能：

- 在渲染循环中间隔处理窗口消息。
- 使用带消息范围过滤的 `PeekMessage`。
- 同时使用DirectInput和窗口消息。
- 在部分逻辑中执行同步等待。
- 自己计算鼠标点击或双击间隔。

未Attach时，这些行为主要影响游戏自己的输入队列；Attach后，工具线程和游戏线程会互相影响输入推进。

## 5. 焦点、激活和鼠标捕获也会共享

线程GUI输入状态不仅包含键盘状态，还包括：

- `hwndActive`：活动窗口。
- `hwndFocus`：键盘焦点窗口。
- `hwndCapture`：鼠标捕获窗口。
- 菜单状态。
- 移动和缩放状态。
- 光标状态。

这些信息可以通过 `GUITHREADINFO`观察。

参考：

- [GUITHREADINFO structure](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-guithreadinfo)
- [GetFocus function](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getfocus)

Attach以后，两个线程不再具有完全独立的焦点、激活和鼠标捕获概念。即使工具没有主动调用 `SetCapture`，游戏内部读取这些状态时所处的输入环境也已经发生变化。

这可能影响：

- 鼠标按下后的捕获。
- 拖动。
- 双击。
- 菜单状态。
- 激活和失活切换。
- 游戏对当前输入所有权的判断。

## 6. Attach调用本身还会重置键盘状态

`AttachThreadInput`官方文档特别说明，调用该函数后，`GetKeyState`和 `GetKeyboardState`所使用的键盘状态会被重置。

因此即使完全没有调用 `SetKeyboardState`，Attach发生的一瞬间也可能改变游戏观察到的键盘状态。这主要解释键盘和组合键异常，不是鼠标双击问题的首要原因，但进一步说明Attach并不是无副作用的只读连接。

## 7. v3.1为什么明显改善

v3.1的自动选择流程是：

```text
游戏没有焦点
  ↓
自动选择共享消息
  ↓
Attach

游戏获得焦点
  ↓ 最多约100ms
自动选择按键+手动窗口
  ↓
Detach
```

当用户真正回到游戏并操作鼠标时，焦点检测线程会很快切换方式并解除Attach。解除后：

- 游戏重新拥有独立输入队列。
- 工具线程不再参与游戏鼠标事件的处理顺序。
- 游戏的焦点和鼠标捕获状态恢复独立。
- 工具线程的消息循环或短暂等待不再拖延游戏输入。

这与实机观察到的鼠标操作明显改善一致。

## 8. 后续诊断依据

当前自动焦点检测周期为100ms。因此游戏刚从后台切换到前台时，可能存在最多约100ms的过渡窗口：

```text
游戏已经获得焦点
但自动检测尚未执行下一轮
当前仍处于Attach状态
```

如果出现“刚切回游戏的第一次点击或双击偶尔失败，稍等后恢复”，会进一步支持Attach输入队列同步是主要原因。

可通过调试日志对照以下时间点：

- 游戏获得焦点的时间。
- 自动模式从共享消息切换到按键+手动窗口的时间。
- Detach完成时间。
- 鼠标异常发生时间。

如果未来仍需优化，可优先考虑缩短焦点检测周期或在明确的窗口激活事件上立即刷新模式，而不是重新引入全局长期Attach。

## 9. 最终判断

影响真实输入的因素按可能性排序如下：

1. 长期Attach使游戏线程和工具线程共享输入队列，输入处理从异步变为同步协作。
2. 任一线程暂时没有继续取消息，都可能延后另一线程的真实鼠标输入。
3. 焦点、激活窗口和鼠标捕获状态被共享，改变了游戏观察到的输入环境。
4. Attach调用本身会重置键盘状态。
5. `SetKeyboardState`会进一步影响键盘状态，但不是鼠标双击异常的必要条件。

因此，v3.1按当前有效模式及时Attach和Detach，是比“全局开关期间常驻Attach”更合理的资源生命周期。
