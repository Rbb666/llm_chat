
/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025/02/01     Rbb666       Add license info
 */
#ifndef __LLM_H_
#define __LLM_H_

#include <rtthread.h>

#define LLM_USING_DEBUG
#ifndef LLM_USING_DEBUG
#define LLM_DBG(fmt, ...)
#else
#define LLM_DBG(fmt, ...)                \
do{                                          \
    rt_kprintf("[\033[32mLLM\033[0m] "); \
    rt_kprintf(fmt, ##__VA_ARGS__);          \
}while(0)
#endif

#ifndef LLM_THREAD_STACK_SIZE
    #define LLM_THREAD_STACK_SIZE 10240
#endif
#ifndef LLM_CMD_SIZE
    #define LLM_CMD_SIZE 512
#endif
#ifndef LLM_HISTORY_LINES
    #define LLM_HISTORY_LINES 5
#endif

enum llm_input_stat
{
    LLM_WAIT_NORMAL,
    LLM_WAIT_SPEC_KEY,
    LLM_WAIT_FUNC_KEY,
};

struct llm_obj
{
    rt_thread_t thread;
    struct rt_semaphore rx_sem;

    enum llm_input_stat stat;

    int   argc;

    char llm_history[LLM_HISTORY_LINES][LLM_CMD_SIZE];
    rt_uint16_t history_count;
    rt_uint16_t history_current;

    char line[LLM_CMD_SIZE];
    rt_uint16_t line_position;
    rt_uint8_t line_curpos;

    rt_device_t device;
    rt_err_t (*rx_indicate)(rt_device_t dev, rt_size_t size);

    char *(*get_answer)(const char *input_text);
};
typedef struct llm_obj *llm_t;

char *get_llm_answer(const char *input_text);

#endif
