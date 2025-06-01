# Change Logs:

## 2025/05/20

* 增加初始化llm_t函数
* 增加删除 llm_t 函数
* llm_obj结构体增加两个消息邮箱，用来传输用户输入与llm输出
```c
    rt_mailbox_t inputbuff_mb;
    rt_mailbox_t outputbuff_mb;
```
* 增添展示聊天记录函数
* 增添发送处理信息函数
* 增添可自定义处理llm输出流程
```c
rt_weak void deal_llm_answer(llm_t handle);
```
* 完善函数注释
* 增加示例demo(使用提示词工程对话)
    * demo/llm_contory.c
* 增加ChangeLog.md日志文件

## 2025/04/18

* Add llm history support
* 添加可自定发送数据包函数
```c
rt_weak char *create_payload(cJSON *messages);
```
* llm_obj 结构体添加messages用来存储对话记录
* 增添添加对话信息函数
* 增添清楚对话信息函数
* 添加部分注释来为函数调用制作示例
* 修复创建payload函数调用时产生的内存泄露问题

## 2025/02/06

* Add http stream support

## 2025/02/03

* Unified Adaptive Interface

## 2025/02/01

* Add license info

