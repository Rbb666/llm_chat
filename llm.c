/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025/02/01     Rbb666       Add license info
 * 2025/02/10     CXSforHPU    Add llm history support
 */
#include "llm.h"
#include "shell.h"

static struct llm_obj handle = {0};

static rt_uint8_t llm_getc(void)
{
    rt_uint8_t ch = 0;
    while (rt_device_read(handle.device, (-1), &ch, 1) != 1)
    {
        rt_sem_take(&(handle.rx_sem), RT_WAITING_FOREVER);
    }
    return ch;
}

static rt_err_t llm_rxcb(rt_device_t dev, rt_size_t size)
{
    return rt_sem_release(&(handle.rx_sem));
}

static rt_bool_t llm_handle_history(const char *prompt)
{
    rt_kprintf("\033[2K\r");
    rt_kprintf("%s%s", prompt, handle.line);
    return RT_FALSE;
}

static void llm_push_history(void)
{
    if (handle.line_position > 0)
    {
        if (handle.history_count >= LLM_HISTORY_LINES)
        {
            if (rt_memcmp(&handle.llm_history[LLM_HISTORY_LINES - 1], handle.line, PKG_LLM_CMD_BUFFER_SIZE))
            {
                int index;
                for (index = 0; index < FINSH_HISTORY_LINES - 1; index ++)
                {
                    rt_memcpy(&handle.llm_history[index][0], &handle.llm_history[index + 1][0], PKG_LLM_CMD_BUFFER_SIZE);
                }
                rt_memset(&handle.llm_history[index][0], 0, PKG_LLM_CMD_BUFFER_SIZE);
                rt_memcpy(&handle.llm_history[index][0], handle.line, handle.line_position);

                handle.history_count = LLM_HISTORY_LINES;
            }
        }
        else
        {
            if (handle.history_count == 0 || rt_memcmp(&handle.llm_history[handle.history_count - 1], handle.line, PKG_LLM_CMD_BUFFER_SIZE))
            {
                handle.history_current = handle.history_count;
                rt_memset(&handle.llm_history[handle.history_count][0], 0, PKG_LLM_CMD_BUFFER_SIZE);
                rt_memcpy(&handle.llm_history[handle.history_count][0], handle.line, handle.line_position);

                handle.history_count++;
            }
        }
    }

    handle.history_current = handle.history_count;
}

int llm_readline(const char *prompt, char *buffer, int buffer_size)
{
    rt_uint8_t ch;

start:
    rt_kprintf(prompt);

    while (1)
    {
        ch = llm_getc();

        if (ch == 0x1b)
        {
            handle.stat = LLM_WAIT_SPEC_KEY;
            continue;
        }
        else if (handle.stat == LLM_WAIT_SPEC_KEY)
        {
            if (ch == 0x5b)
            {
                handle.stat = LLM_WAIT_FUNC_KEY;
                continue;
            }
            handle.stat = LLM_WAIT_NORMAL;
        }
        else if (handle.stat == LLM_WAIT_FUNC_KEY)
        {
            handle.stat = LLM_WAIT_NORMAL;

            if (ch == 0x41)
            {
                if (handle.history_current > 0)
                {
                    handle.history_current--;
                }
                else
                {
                    handle.history_current = 0;
                    continue;
                }

                rt_memcpy(handle.line, &handle.llm_history[handle.history_current][0], PKG_LLM_CMD_BUFFER_SIZE);
                handle.line_curpos = handle.line_position = rt_strlen(handle.line);
                llm_handle_history(prompt);

                continue;
            }
            else if (ch == 0x42)
            {
                if (handle.history_current < (handle.history_count - 1))
                {
                    handle.history_current++;
                }
                else
                {
                    if (handle.history_count != 0)
                    {
                        handle.history_current = handle.history_count - 1;
                    }
                    else
                    {
                        continue;
                    }
                }

                rt_memcpy(handle.line, &handle.llm_history[handle.history_current][0], PKG_LLM_CMD_BUFFER_SIZE);
                handle.line_curpos = handle.line_position = rt_strlen(handle.line);
                llm_handle_history(prompt);

                continue;
            }
            else if (ch == 0x44)
            {
                if (handle.line_curpos)
                {
                    rt_kprintf("\b");
                    handle.line_curpos--;
                }
                continue;
            }
            else if (ch == 0x43)
            {
                if (handle.line_curpos < handle.line_position)
                {
                    rt_kprintf("%c", handle.line[handle.line_curpos]);
                    handle.line_curpos++;
                }
                continue;
            }
        }

        if (ch == '\0' || ch == 0xFF)
        {
            continue;
        }

        else if (ch == 0x7f || ch == 0x08)
        {
            if (handle.line_curpos == 0)
            {
                continue;
            }

            handle.line_curpos--;
            handle.line_position--;

            if (handle.line_position > handle.line_curpos)
            {
                rt_memmove(&handle.line[handle.line_curpos], &handle.line[handle.line_curpos + 1],
                           handle.line_position - handle.line_curpos);
                handle.line[handle.line_position] = 0;

                rt_kprintf("\b%s  \b", &handle.line[handle.line_curpos]);

                int index;
                for (index = (handle.line_curpos); index <= (handle.line_position); index++)
                {
                    rt_kprintf("\b");
                }
            }
            else
            {
                rt_kprintf("\b \b");
                handle.line[handle.line_position] = 0;
            }
            continue;
        }

        else if (ch == '\r' || ch == '\n')
        {
            llm_push_history();

            rt_kprintf("\n");
            if (handle.line_position == 0)
            {
                goto start;
            }
            else
            {
                rt_uint8_t temp = handle.line_position;

                rt_strncpy(buffer, handle.line, handle.line_position);
                rt_memset(handle.line, 0x00, sizeof(handle.line));
                buffer[handle.line_position] = 0;
                handle.line_curpos = handle.line_position = 0;
                return temp;
            }
        }

        else if (ch == 0x04)
        {
            if (handle.line_position == 0)
            {
                return 0;
            }
            else
            {
                continue;
            }
        }

        else if (ch == '\t')
        {
            continue;
        }

        if (handle.line_position >= PKG_LLM_CMD_BUFFER_SIZE)
        {
            continue;
        }

        if (handle.line_curpos < handle.line_position)
        {
            rt_memmove(&handle.line[handle.line_curpos + 1], &handle.line[handle.line_curpos],
                       handle.line_position - handle.line_curpos);
            handle.line[handle.line_curpos] = ch;

            rt_kprintf("%s", &handle.line[handle.line_curpos]);

            int index;
            for (index = (handle.line_curpos); index < (handle.line_position); index++)
            {
                rt_kprintf("\b");
            }
        }
        else
        {
            handle.line[handle.line_position] = ch;
            rt_kprintf("%c", ch);
        }

        ch = 0;
        handle.line_curpos++;
        handle.line_position++;
    }
}

static void llm_run(void *p)
{
    char input_buffer[PKG_LLM_CMD_BUFFER_SIZE] = {0};
    const char *device_name = RT_CONSOLE_DEVICE_NAME;

    handle.device = rt_device_find(device_name);
    if (handle.device == RT_NULL)
    {
        LLM_DBG("The msh device find failed.\n");
        return;
    }

    handle.rx_indicate = handle.device->rx_indicate;
    rt_device_set_rx_indicate(handle.device, llm_rxcb);

    if (handle.argc == 1)
    {
        rt_kprintf("\nPress CTRL+D to exit llm shell.\n");
    }

    while (1)
    {
        int length = llm_readline("Enter command: ", input_buffer, PKG_LLM_CMD_BUFFER_SIZE);

        if (length == 0)
        {
            rt_kprintf("Exit terminal.\n");
            break;
        }
        else if (length > 0)
        {
            #ifdef PKG_LLMCHAT_HISTORY_PAYLOAD
            cJSON *message_user = cJSON_CreateObject(); // Create the object
            cJSON_AddStringToObject(message_user, "role", "user"); // Add "role"
            cJSON_AddStringToObject(message_user, "content", input_buffer); // Add "content"
            cJSON_AddItemToArray(handle.messages, message_user); // Add to array
            
            char *result = handle.get_answer(handle.messages);
            
            cJSON *message_assistant = cJSON_CreateObject(); // Create the object
            cJSON_AddStringToObject(message_assistant, "role", "assistant"); // Add "assitant"
            cJSON_AddStringToObject(message_assistant, "content", result); // Add "content"
            cJSON_AddItemToArray(handle.messages, message_assistant); // Add to array
            
            #else
            cJSON *messages = cJSON_CreateArray();
            cJSON *message_user = cJSON_CreateObject();
            
            cJSON_AddStringToObject(message_user, "role", "user");              // Add "role":"user"
            cJSON_AddStringToObject(message_user, "content", input_buffer);     // Add "content":"ques"
            cJSON_AddItemToArray(messages, message_user);                       // Add to array for payload

            handle.get_answer(messages);

            //clear memory
            cJSON_Delete(messages);

            #endif

        }
        else
        {
            rt_kprintf("No valid input.\n");
        }

        rt_memset(input_buffer, 0, sizeof(input_buffer));
    }

    rt_sem_detach(&(handle.rx_sem));
    rt_device_set_rx_indicate(handle.device, handle.rx_indicate);
    rt_kprintf(FINSH_PROMPT);
}

static int llm2rtt(int argc, char **argv)
{
    static rt_bool_t history_init = RT_FALSE;

    if (history_init == RT_FALSE)
    {
        rt_memset(&handle, 0x00, sizeof(struct llm_obj));
        history_init = RT_TRUE;
    }
    else
    {
        handle.stat = LLM_WAIT_NORMAL;
        handle.argc = 0;
        rt_memset(handle.line, 0x00, PKG_LLM_CMD_BUFFER_SIZE);
        handle.line_position = 0;
        handle.line_curpos = 0;
        handle.device = RT_NULL;
        handle.rx_indicate = RT_NULL;
    }

    rt_sem_init(&(handle.rx_sem), "llm_rxsem", 0, RT_IPC_FLAG_FIFO);

    handle.argc = argc;
    handle.get_answer = get_llm_answer;
    handle.messages = cJSON_CreateArray();

#if defined(RT_VERSION_CHECK) && (RTTHREAD_VERSION >= RT_VERSION_CHECK(5, 1, 0))
    rt_uint8_t prio = RT_SCHED_PRIV(rt_thread_self()).current_priority + 1;
#else
    rt_uint8_t prio = rt_thread_self()->current_priority + 1;
#endif
    rt_err_t result = rt_thread_init(&handle.thread,
                                     "llm_td",
                                     llm_run, RT_NULL,
                                     &handle.thread_stack[0], sizeof(handle.thread_stack),
                                     prio, 10);
    if (result != RT_EOK)
    {
        rt_sem_detach(&(handle.rx_sem));
        LLM_DBG("The llm interpreter thread create failed.\n");
        return RT_ERROR;
    }
    rt_thread_startup(&handle.thread);

    return RT_EOK;
}
MSH_CMD_EXPORT_ALIAS(llm2rtt, llm, llm Interactive Terminal.);
