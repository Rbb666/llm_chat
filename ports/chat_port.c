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

/********************************************************************************
 * @File name: chat_port.c
 * @Author: CXSforHPU
 * @Version: 1.1
 * @Date: 2025-2-10
 * @Description: create char for request payload.
 * @messages: your want to send messages:        example
 * {"role": "user", "content": "Hello!"}
 *
 * if you want to modify the request payload, you can modify the following code.
 ********************************************************************************/
char *create_payload(cJSON *messages)
{

    cJSON *requestRoot = cJSON_CreateObject();
    cJSON *model = cJSON_CreateString(LLM_MODEL_NAME);

    cJSON_AddItemToObject(requestRoot, "model", model);
    cJSON_AddItemToObject(requestRoot, "messages", messages);
#ifdef PKG_LLMCHAT_STREAM
    cJSON_AddBoolToObject(requestRoot, "stream", RT_TRUE);
#else
    cJSON_AddBoolToObject(requestRoot, "stream", RT_FALSE);
#endif

    return cJSON_PrintUnformatted(requestRoot);
}

char *get_llm_answer(cJSON *messages)
{
    struct webclient_session *webSession = NULL;
    char *allContent = NULL;
    int bytesRead, responseStatus;

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
    char *payload = create_payload(messages);

    if (payload == NULL)
    {
        rt_kprintf("Failed to create JSON payload.\n");
        goto cleanup;
    }

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
                    char *oldAllContent = allContent;
                    size_t newLen = rt_strlen(contentBuffer);

                    // Print content
                    for (size_t i = 0; i < newLen; i++)
                    {
                        rt_kprintf("%c", contentBuffer[i]);
                    }

                    // Append content to allContent
                    if (oldAllContent)
                    {
                        strcat(allContent, contentBuffer);
                    }

                    rt_free(oldAllContent);
                    contentBuffer[0] = '\0'; // Reset content buffer
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

    return allContent;
}
