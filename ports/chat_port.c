/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: MIT
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025/02/01     Rbb666       Add license info
 * 2025/02/03     Rbb666       Unified Adaptive Interface
 * 2025/02/06     Rbb666       Add http stream support
 * 2025/02/10     CXSforHPU    Add llm history support
 */
#include "llm.h"
#include "webclient.h"
#include "llm_config.h"
#include <cJSON.h>

static const char *get_dynamic_api_key(void)
{
    const llm_runtime_config_t *cfg = llm_config_get();
    if (cfg && cfg->is_configured && cfg->api_key[0])
    {
        return cfg->api_key;
    }
    return PKG_LLM_API_KEY;
}

static const char *get_dynamic_model_name(void)
{
    const llm_runtime_config_t *cfg = llm_config_get();
    if (cfg && cfg->is_configured && cfg->model_name[0])
    {
        return cfg->model_name;
    }
    return PKG_LLM_MODEL_NAME;
}

static const char *get_dynamic_api_url(void)
{
    const llm_runtime_config_t *cfg = llm_config_get();
    if (cfg && cfg->is_configured && cfg->api_url[0])
    {
        return cfg->api_url;
    }
    return PKG_LLM_DEFAULT_API_URL;
}

#define WEB_SOCKET_BUF_SIZE PKG_WEB_SORKET_BUFSZ

static char authHeader[128] = {0};
static char responseBuffer[WEB_SOCKET_BUF_SIZE] = {0};
static char contentBuffer[WEB_SOCKET_BUF_SIZE] = {0};

static rt_bool_t append_chunk_to_buffer(char **buffer,
                                        size_t *length,
                                        size_t *capacity,
                                        const char *chunk)
{
    size_t chunk_len;

    if (buffer == RT_NULL || length == RT_NULL || capacity == RT_NULL || chunk == RT_NULL)
    {
        return RT_FALSE;
    }

    chunk_len = rt_strlen(chunk);
    if (chunk_len == 0)
    {
        return RT_TRUE;
    }

    if (*capacity < (*length + chunk_len + 1))
    {
        size_t new_capacity = (*capacity == 0) ? WEB_SOCKET_BUF_SIZE : *capacity;

        while (new_capacity < (*length + chunk_len + 1))
        {
            size_t proposed = new_capacity << 1;
            if (proposed <= new_capacity)
            {
                new_capacity = (*length + chunk_len + 1);
                break;
            }
            new_capacity = proposed;
        }

        if (*buffer == RT_NULL)
        {
            *buffer = (char *)rt_malloc(new_capacity);
            if (*buffer == RT_NULL)
            {
                return RT_FALSE;
            }
            (*buffer)[0] = '\0';
        }
        else
        {
            char *new_buf = (char *)rt_realloc(*buffer, new_capacity);
            if (new_buf == RT_NULL)
            {
                return RT_FALSE;
            }
            *buffer = new_buf;
        }

        *capacity = new_capacity;
    }

    rt_memcpy(*buffer + *length, chunk, chunk_len);
    *length += chunk_len;
    (*buffer)[*length] = '\0';
    return RT_TRUE;
}

/**
 * @brief: create char for request payload.
 * @messages: llm_obj.messages.
 * @return: the  char for request payload.
 * if you want to modify the request payload, you can modify the following code.
 **/
/** Create request payload for LLM API
 *  @messages: llm_obj.messages
 *  @return: the char for request payload
 *  if you want to modify the request payload, you can modify the following code
 */
rt_weak char *create_payload(cJSON *messages)
{
    cJSON *requestRoot = cJSON_CreateObject();
    /* Use dynamically configured model name */
    cJSON *model = cJSON_CreateString(get_dynamic_model_name());
    cJSON *messages_copy = cJSON_Duplicate(messages, 1);
    char *payload = NULL;
    cJSON_AddItemToObject(requestRoot, "model", model);
    cJSON_AddItemToObject(requestRoot, "messages", messages_copy);
#ifdef PKG_LLMCHAT_STREAM
    cJSON_AddBoolToObject(requestRoot, "stream", RT_TRUE);
#else
    cJSON_AddBoolToObject(requestRoot, "stream", RT_FALSE);
#endif
    payload = cJSON_PrintUnformatted(requestRoot);
    cJSON_Delete(requestRoot);

    return payload;
}

/**
 * @brief: deal the answer of the llm
 * @handle: llm_t
 * if you want to modify the dealing, you can modify the following code.
 **/
rt_weak void deal_llm_answer(llm_t handle)
{
    char *answer = RT_NULL;
    rt_mb_recv(handle->outputbuff_mb, (rt_uint32_t *)&answer, RT_WAITING_FOREVER);
    /* You can modify this section */

    int len = rt_strlen(answer);
    rt_kprintf("LLM :\n");
    for (int i = 0; i <= len; i++)
    {
        rt_kprintf("%c", answer[i]);
    }
    rt_kprintf("\n");

    /* End of modifiable section */
    rt_free(answer);
}

/**
 * @brief: add message to messages
 * @input_buffer: your input buffer or assistant output buffer.
 * @role: 'user' or 'assistant'.
 * @handle: llm_t.
 **/
void add_message2messages(const char *input_buffer, char *role, llm_t handle)
{
    if (!cJSON_IsArray(handle->messages))
    {
        handle->messages = cJSON_CreateArray();
    }

    cJSON *message = create_message(input_buffer, role);
    cJSON_AddItemToArray(handle->messages, message);
}

/**
 * @brief: creat a message
 * @input_buffer: your input buffer or assistant output buffer.
 * @role: 'user' or 'assistant'.
 * @return cJSON* The message object.
 * such as:
 * {"role": "user", "content": "Hello!"}
 * {"role": "assistant", "content": "Hi there! How can I assist you today?"}
 **/
cJSON *create_message(const char *input_buffer, char *role)
{
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", role);
    cJSON_AddStringToObject(message, "content", input_buffer);

    return message;
}

/**
 * @brief: clear messages
 * @handle: llm_obj
 **/
void clear_messages(llm_t handle)
{
    cJSON_Delete(handle->messages);
    handle->messages = cJSON_CreateArray();
}

/**
 * @brief: create the llm_t->messages and llm_t->get_answer of llm_t
 * @return llm_t
 **/
llm_t create_llm_t()
{
    llm_t handle = (llm_t)rt_malloc(sizeof(struct llm_obj));
    if (handle == RT_NULL)
    {
        rt_kprintf("Failed to allocate llm handle.\n");
        return RT_NULL;
    }

    rt_memset(handle, 0x00, sizeof(struct llm_obj));

    rt_err_t result = init_llm(handle);

    if (result != RT_EOK)
    {
        LLM_DBG("The llm interpreter thread create failed.\n");
        return RT_NULL;
    }

    return handle;
}

/**
 * @brief: delete the llm_t
 **/
void delete_llm_t(llm_t handle)
{
    if (handle != RT_NULL)
    {
        rt_thread_detach(&handle->thread);
        cJSON_Delete(handle->messages);
        rt_mb_delete(handle->inputbuff_mb);
        rt_mb_delete(handle->outputbuff_mb);
        rt_free(handle);
    }
    else
    {
        rt_kprintf("Error: can`t free llm_t.\n");
    }
}

/**
 * @brief: display the llm_t->messages
 **/
void display_llm_messages(llm_t handle)
{
    char *jsonString = cJSON_PrintUnformatted(handle->messages);
    int len = strlen(jsonString);
    for (int i = 0; i < len; i++)
    {
        rt_kprintf("%c", jsonString[i]);
    }

    cJSON_free(jsonString);
}

/**
 * @brief Get the answer from a large language model API.
 *
 * This function sends a POST request to a large language model API with a JSON payload.
 *
 * @param messages llm_obj.messages.
 *  you need to use 'add_message2messages()' to add messages which you want to send to the model,before using this function.
 * @return char* The model's answer as a string.
 * you need to free the memory by using rt_free(),when you don`t use the answer.
 *
 * example:
 #ifdef PKG_LLMCHAT_HISTORY_PAYLOAD
            add_message2messages(input_buffer, "user", &handle);

            char *result = handle.get_answer(&handle, handle.messages);

            add_message2messages(result, "assistant", &handle);

#else

            add_message2messages(input_buffer, "user", &handle);

            char *result = handle.get_answer(&handle, handle.messages);

            rt_free(result);
            clear_messages(&handle);
 *
 **/
char *get_llm_answer(llm_t handle, cJSON *messages)
{
    struct webclient_session *webSession = RT_NULL;
    char *payload = RT_NULL;
    char *result = RT_NULL;
    char *assembled = RT_NULL;
    size_t assembled_len = 0;
    size_t assembled_cap = 0;
    int bytesRead, responseStatus;
    int inContent = 0;
    size_t current_chunk_len = 0;
    llm_stream_callback_t stream_cb = RT_NULL;
    void *stream_context = RT_NULL;

    if (handle == RT_NULL)
    {
        rt_kprintf("Error: llm handle is NULL.\n");
        return RT_NULL;
    }

    contentBuffer[0] = '\0';

    /* Check if messages is an array */
    if (!cJSON_IsArray(messages))
    {
        rt_kprintf("Error: messages must be a cJSON array.\n");
        goto cleanup;
    }

    stream_cb = handle->stream_cb;
    stream_context = handle->stream_user_data;

    /* Create web session */
    webSession = webclient_session_create(WEB_SOCKET_BUF_SIZE);
    if (webSession == NULL)
    {
        rt_kprintf("Failed to create webclient session.\n");
        goto cleanup;
    }

    /* Create JSON payload */
    payload = create_payload(messages);
    if (payload == NULL)
    {
        rt_kprintf("Failed to create JSON payload.\n");
        goto cleanup;
    }

#ifdef PKG_LLMCHAT_DBG

    int len = strlen(payload);
    for (size_t i = 0; i < len; i++)
    {
        rt_kprintf("%c", payload[i]);
    }

#endif
    /* Prepare authorization header - use dynamic configuration */
    const char *current_api_key = get_dynamic_api_key();
    const char *current_api_url = get_dynamic_api_url();

    rt_snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s\r\n", current_api_key);

    /* Add headers */
    webclient_header_fields_add(webSession, "Content-Type: application/json\r\n");
    webclient_header_fields_add(webSession, authHeader);
    webclient_header_fields_add(webSession, "Content-Length: %d\r\n", rt_strlen(payload));

    LLM_DBG("HTTP Header: %s\n", webSession->header->buffer);
    LLM_DBG("HTTP Payload: %s\n", payload);
    LLM_DBG("Using API URL: %s\n", current_api_url);

    /* Send POST request - use dynamic configuration */
    responseStatus = webclient_post(webSession, current_api_url, payload, rt_strlen(payload));
    if (responseStatus != 200)
    {
        rt_kprintf("Webclient POST request failed, response status: %d\n", responseStatus);
        goto cleanup;
    }

    /* Read and process response */
    while ((bytesRead = webclient_read(webSession, responseBuffer, WEB_SOCKET_BUF_SIZE)) > 0)
    {
        for (int i = 0; i < bytesRead; i++)
        {
            char ch = responseBuffer[i];

            if (inContent)
            {
                if (ch == '"')
                {
                    inContent = 0;

                    if (current_chunk_len > 0)
                    {
                        for (size_t j = 0; j < current_chunk_len; j++)
                        {
                            rt_kprintf("%c", contentBuffer[j]);
                        }

                        if (!append_chunk_to_buffer(&assembled, &assembled_len, &assembled_cap, contentBuffer))
                        {
                            rt_kprintf("Error: insufficient memory for LLM response.\n");
                            goto cleanup;
                        }

                        if (stream_cb)
                        {
                            stream_cb(contentBuffer, stream_context);
                        }
                    }

                    current_chunk_len = 0;
                    contentBuffer[0] = '\0';
                }
                else
                {
                    if (current_chunk_len < (sizeof(contentBuffer) - 1))
                    {
                        contentBuffer[current_chunk_len++] = ch;
                        contentBuffer[current_chunk_len] = '\0';
                    }
                }
            }
            else if (ch == '"' && i >= 10 &&
                     rt_strncmp(&responseBuffer[i - 10], "\"content\":\"", 10) == 0)
            {
                inContent = 1;
                current_chunk_len = 0;
                contentBuffer[0] = '\0';
            }
        }
    }

    if (assembled_len > 0)
    {
        rt_kprintf("\n");
    }

cleanup:
    /* Cleanup resources */
    if (webSession)
    {
        webclient_close(webSession);
    }
    if (payload)
    {
        cJSON_free(payload);
    }

    if (assembled_len > 0 && assembled != RT_NULL)
    {
        result = assembled;
        assembled = RT_NULL;
    }

    if (assembled != RT_NULL)
    {
        rt_free(assembled);
    }
    return result;
}

static void recv_inputBuff_mb(void *handle)
{
    llm_t llm = (llm_t)handle;
    char *input_buffer = RT_NULL;
    while (1)
    {
        rt_mb_recv(llm->inputbuff_mb, (rt_uint32_t *)&input_buffer, RT_WAITING_FOREVER);

        /* Show the input */
        int len = rt_strlen(input_buffer);
        rt_kprintf("USER :\n");
        for (int i = 0; i <= len; i++)
        {
            rt_kprintf("%c", input_buffer[i]);
        }
        rt_kprintf("\n");

        char *result = RT_NULL;
#ifdef PKG_LLMCHAT_HISTORY_PAYLOAD
        add_message2messages(input_buffer, "user", llm);

        result = llm->get_answer(llm, llm->messages);

        add_message2messages(result, "assistant", llm);

#else
        add_message2messages(input_buffer, "user", llm);

        result = llm->get_answer(llm, llm->messages);
#if defined(LLM_DBG)
        display_llm_messages(llm);
#endif
        clear_messages(llm);

#endif

        rt_mb_send(llm->outputbuff_mb, (rt_uint32_t)result);

        deal_llm_answer(llm);

        rt_free(input_buffer);
    }
}

/**
 * @brief send the inputBuffer to llm_chat
 * @param handle the  llm_t
 * @param inputBuffer the inputBuffer
 */
void send_llm_mb(llm_t handle, char *inputBuffer)
{
    char *buffer = (char *)rt_malloc(strlen(inputBuffer) + 1);
    if (buffer == RT_NULL)
    {
        rt_kprintf("Failed to allocate memory for input buffer\n");
        return;
    }

    rt_memset(buffer, 0, strlen(inputBuffer) + 1);
    rt_strncpy(buffer, inputBuffer, strlen(inputBuffer));

    rt_mb_send(handle->inputbuff_mb, (rt_uint32_t)buffer);
}

/**
 * @brief: init the llm_t handle->messages ,llm_obj handle->get_answer
 * @handle: llm_t
 * @return: rt_err_t
 **/
rt_err_t init_llm(llm_t handle)
{
    if (handle == RT_NULL)
    {
        return RT_ERROR;
    }

    handle->get_answer = get_llm_answer;
    handle->messages = cJSON_CreateArray();
    handle->stream_cb = RT_NULL;
    handle->stream_user_data = RT_NULL;

    handle->inputbuff_mb = rt_mb_create("llm_inputbuff_mb", sizeof(char *) * LLM_CHAT_NUM, RT_IPC_FLAG_FIFO);
    if (handle->inputbuff_mb == RT_NULL)
    {
        rt_kprintf("can`t create inputbuff_mb");
    }
    handle->outputbuff_mb = rt_mb_create("llm_outputbuff_mb", sizeof(char *) * LLM_CHAT_NUM, RT_IPC_FLAG_FIFO);
    if (handle->outputbuff_mb == RT_NULL)
    {
        rt_kprintf("can`t create outputbuff_mb");
    }

#if defined(RT_VERSION_CHECK) && (RTTHREAD_VERSION >= RT_VERSION_CHECK(5, 1, 0))
    rt_uint8_t prio = RT_SCHED_PRIV(rt_thread_self()).current_priority + 1;
#else
    rt_uint8_t prio = rt_thread_self()->current_priority + 1;
#endif

    rt_err_t result = rt_thread_init(&handle->thread,
                                     "llm_thread",
                                     recv_inputBuff_mb,
                                     (void *)handle,
                                     &handle->thread_stack[0],
                                     sizeof(handle->thread_stack),
                                     prio,
                                     10);

    if (result != RT_EOK)
    {
        LLM_DBG("The llm interpreter thread create failed.\n");
        return RT_ERROR;
    }
    rt_thread_startup(&handle->thread);

    return RT_EOK;
}
