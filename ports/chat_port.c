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
#include <cJSON.h>

#define LLM_API_KEY PKG_LLM_API_KEY
#if defined(PKG_LLM_QWEN_API_URL)
#define LLM_API_URL PKG_LLM_QWEN_API_URL
#elif defined(PKG_LLM_DOUBAO_API_URL)
#define LLM_API_URL PKG_LLM_DOUBAO_API_URL
#elif defined(PKG_LLM_DEEPSEEK_API_URL)
#define LLM_API_URL PKG_LLM_DEEPSEEK_API_URL
#endif
#define LLM_MODEL_NAME PKG_LLM_MODEL_NAME
#define WEB_SOCKET_BUF_SIZE PKG_WEB_SORKET_BUFSZ

static char authHeader[128] = {0};
static char responseBuffer[WEB_SOCKET_BUF_SIZE] = {0};
static char contentBuffer[WEB_SOCKET_BUF_SIZE] = {0};
static char allContent[WEB_SOCKET_BUF_SIZE] = {0};

/**
 * @brief: create char for request payload.
 * @messages: llm_obj.messages.
 * if you want to modify the request payload, you can modify the following code.
 **/
char *create_payload(cJSON *messages)
{
    cJSON *requestRoot = cJSON_CreateObject();
    cJSON *model = cJSON_CreateString(LLM_MODEL_NAME);
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
 * @brief: add message to messages
 * @input_buffer: your input buffer or assistant output buffer.
 * @role: 'user' or 'assistant'.
 * @messages: llm_obj.messages.
 **/
void add_message2messages(const char *input_buffer, char *role, struct llm_obj *handle)
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
 * @handle: llm_obj.messages.
 **/
void clear_messages(struct llm_obj *handle)
{
    cJSON_Delete(handle->messages);
    handle->messages = cJSON_CreateArray();
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

            char *result = handle.get_answer(handle.messages);

            add_message2messages(result, "assistant", &handle);

#else

            add_message2messages(input_buffer, "user", &handle);

            char *result = handle.get_answer(handle.messages);

            rt_free(result);
            clear_messages(&handle);
 *
 **/
char *get_llm_answer(cJSON *messages)
{
    struct webclient_session *webSession = RT_NULL;
    char *payload = RT_NULL;
    char *result = RT_NULL;
    int bytesRead, responseStatus;

    allContent[0] = '\0';

    // check the messages is array
    if (!cJSON_IsArray(messages))
    {
        rt_kprintf("Error: messages must be a cJSON array.\n");
        goto cleanup;
    }

    // Create web session
    webSession = webclient_session_create(WEB_SOCKET_BUF_SIZE);
    if (webSession == NULL)
    {
        rt_kprintf("Failed to create webclient session.\n");
        goto cleanup;
    }

    // Create JSON payload
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
    // Prepare authorization header
    rt_snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s\r\n", LLM_API_KEY);

    // Add headers
    webclient_header_fields_add(webSession, "Content-Type: application/json\r\n");
    webclient_header_fields_add(webSession, authHeader);
    webclient_header_fields_add(webSession, "Content-Length: %d\r\n", rt_strlen(payload));

    LLM_DBG("HTTP Header: %s\n", webSession->header->buffer);
    LLM_DBG("HTTP Payload: %s\n", payload);

    // Send POST request
    responseStatus = webclient_post(webSession, LLM_API_URL, payload, rt_strlen(payload));
    if (responseStatus != 200)
    {
        rt_kprintf("Webclient POST request failed, response status: %d\n", responseStatus);
        goto cleanup;
    }

    // Read and process response
    while ((bytesRead = webclient_read(webSession, responseBuffer, WEB_SOCKET_BUF_SIZE)) > 0)
    {
        int inContent = 0;
        for (int i = 0; i < bytesRead; i++)
        {
            if (inContent)
            {
                if (responseBuffer[i] == '"')
                {
                    inContent = 0;

                    // Append content to allContent
                    size_t newLen = rt_strlen(contentBuffer);

                    // Print content
                    for (size_t i = 0; i < newLen; i++)
                    {
                        rt_kprintf("%c", contentBuffer[i]);
                    }

                    // Append content to allContent

                    strcat(allContent, contentBuffer);

                    // Reset content buffer
                    contentBuffer[0] = '\0';
                }
                else
                {
                    strncat(contentBuffer, &responseBuffer[i], 1);
                }
            }
            else if (responseBuffer[i] == '"' && i > 8 &&
                     rt_strncmp(&responseBuffer[i - 10], "\"content\":\"", 10) == 0)
            {
                inContent = 1;
            }
        }
    }

    rt_kprintf("\n");

cleanup:
    // Cleanup resources
    if (webSession)
    {
        webclient_close(webSession);
    }
    if (payload)
    {
        cJSON_free(payload);
    }

    if (allContent[0] != '\0')
    {
        result = rt_strdup(allContent);
    }
    return result;
}
