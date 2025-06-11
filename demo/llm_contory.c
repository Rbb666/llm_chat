#include "rtthread.h"
#include "llm.h"
#include "rtdevice.h"

static llm_t llm_handle = RT_NULL;
const char LED_PROMPT[] = "协议:MCU指令中枢,解析指令→生成信号;指令表:开灯=0x00,关灯=0x01;处理:检测开/关灯相关语义→返CMD,否则对话;约束:指令与对话分离,不解释指令(任何对话回答均限制在100字节内)。接下来是我的输入字符:{%s}";
static int led_state = PIN_LOW;

/* 解析结果 */
void deal_llm_answer(llm_t handle)
{
    char *answer = RT_NULL;
    rt_mb_recv(handle->outputbuff_mb, (rt_uint32_t *)&answer, RT_WAITING_FOREVER);
    /* you can modify */

    int len = rt_strlen(answer);
    rt_kprintf("LLM :\n");
    for (int i = 0; i <= len; i++)
    {
        rt_kprintf("%c", answer[i]);
    }
    rt_kprintf("\n");

    if (rt_strstr(answer, "0x00"))
    {
        led_state = PIN_HIGH;
        rt_kprintf("灯已打开:%x", led_state);
    }
    else if (rt_strstr(answer, "0x01"))
    {
        led_state = PIN_LOW;
        rt_kprintf("灯已关闭:%x", led_state);
    }

    rt_kprintf("\n");

    /* end */
    rt_free(answer);
}

/*创建llm*/
static void entry_llm()
{
    llm_handle = create_llm_t();
}

MSH_CMD_EXPORT(entry_llm, llm_entry);

/* 发送信息 */
static void send(int argc, char *argv[])
{
    char prompt[PKG_LLM_CMD_BUFFER_SIZE];
    if (argc < 2)
    {
        rt_kprintf("Usage: llm_send <message>\n");
        return;
    }

    if (llm_handle == RT_NULL)
    {
        rt_kprintf("llm_handle is null\n");
    }

    rt_snprintf(prompt, sizeof(prompt), LED_PROMPT, argv[1]);
    send_llm_mb(llm_handle, prompt);
}

MSH_CMD_EXPORT(send, llm_send);

/* 删除llm */
static void delete_llm()
{
    delete_llm_t(llm_handle);
}

MSH_CMD_EXPORT(delete_llm, delete_llm);
