#include "rtthread.h"
#include "llm.h"

#define LED_PIN    BSP_IO_PORT_01_PIN_02 /* Onboard LED pins */
static rt_thread_t llm_thread= RT_NULL;
static rt_mailbox_t inputbuff_mb = RT_NULL;

const char LED_PROMPT[] = "协议:MCU指令中枢,解析指令→生成信号;指令表:开灯=0x00,关灯=0x01;处理:检测开/关灯相关语义→返CMD,否则对话;约束:指令与对话分离,不解释指令(任何对话回答均限制在100字节内)。接下来是我的输入字符:{%s}";


static void contory_led()
{

    char prompt[PKG_LLM_CMD_BUFFER_SIZE*2] = {0};
    llm_t handle = RT_NULL;
    handle = create_llm_t();

    while (1)
    {
        char *input_buffer = RT_NULL;
        rt_mb_recv(inputbuff_mb, (rt_uint32_t *)&input_buffer,RT_WAITING_FOREVER);
        if (input_buffer[0] == 'q')
        {
            break;
        }
#ifdef PKG_LLMCHAT_HISTORY_PAYLOAD
        add_message2messages(input_buffer, "user", &handle);

        char *result = handle.get_answer(handle.messages);

        add_message2messages(result, "assistant", &handle);

#else
        rt_kprintf("llm_contory: %s\n", input_buffer);
        rt_snprintf(prompt,sizeof(prompt),LED_PROMPT, input_buffer);
        add_message2messages(prompt, "user", handle);

        char *result = handle->get_answer(handle->messages);
        
        display_llm_messages(handle);
        rt_free(result);

        clear_messages(handle);

#endif

        rt_free(input_buffer);
        rt_memset(prompt, 0, sizeof(prompt));

    }

    delete_llm_t(handle);
}

static void send_llm_mb(int argc, char *argv[])
{
    if (argc < 2) {
        rt_kprintf("Usage: llm_send <message>\n");
        return;
    }

    char *buffer = (char *)rt_malloc(strlen(argv[1]) + 1);
    if (buffer == RT_NULL) {
        rt_kprintf("Failed to allocate memory for input buffer\n");
        return;
    }

    rt_memset(buffer, 0, strlen(argv[1]) + 1);
    rt_strncpy(buffer, argv[1], strlen(argv[1]));

    rt_mb_send(inputbuff_mb, (rt_uint32_t)buffer);
}

MSH_CMD_EXPORT(send_llm_mb,llm_send);

static void entry_llm()
{
    rt_uint8_t prio = rt_thread_self()->current_priority + 1;
    llm_thread = rt_thread_create("llm_entry",contory_led, RT_NULL, 40960, prio, 20);

    if (llm_thread)
    {
        /* code */
        rt_thread_startup(llm_thread);
    }
    else
    {
        /* code */
        rt_kprintf("llm_entry thread create failed\n");
    }

    inputbuff_mb = rt_mb_create("llm_mb", 20,RT_IPC_FLAG_FIFO);
}

MSH_CMD_EXPORT(entry_llm,llm_entry);

