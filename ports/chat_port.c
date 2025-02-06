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

char *get_llm_answer(const char *inputText)
{
    struct webclient_session *webSession = NULL;
    char *allContent = NULL;
    int bytesRead, responseStatus;
    cJSON *responseRoot = NULL;

    // Create web session
    webSession = webclient_session_create(WEB_SOCKET_BUF_SIZE);
    if (webSession == NULL)
    {
        rt_kprintf("Failed to create webclient session.\n");
        goto cleanup;
    }

    // Create JSON payload
    cJSON *requestRoot = cJSON_CreateObject();
    cJSON *model = cJSON_CreateString(LLM_MODEL_NAME);
    cJSON *messages = cJSON_CreateArray();
    cJSON *systemMessage = cJSON_CreateObject();
    cJSON *userMessage = cJSON_CreateObject();

    cJSON_AddItemToObject(requestRoot, "model", model);
    cJSON_AddItemToObject(requestRoot, "messages", messages);
#ifdef PKG_LLMCHAT_STREAM
    cJSON_AddBoolToObject(requestRoot, "stream", RT_TRUE);
#else
    cJSON_AddBoolToObject(requestRoot, "stream", RT_FALSE);
#endif
    cJSON_AddItemToArray(messages, systemMessage);
    cJSON_AddItemToArray(messages, userMessage);

    cJSON_AddStringToObject(systemMessage, "role", "system");
    cJSON_AddStringToObject(systemMessage, "content", "");

    cJSON_AddStringToObject(userMessage, "role", "user");
    cJSON_AddStringToObject(userMessage, "content", inputText);

    char *payload = cJSON_PrintUnformatted(requestRoot);
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
                    size_t oldLen = oldAllContent ? rt_strlen(oldAllContent) : 0;
                    size_t newLen = rt_strlen(contentBuffer);
                    size_t totalLen = oldLen + newLen + 1;

                    char *newAllContent = (char *)web_malloc(totalLen);
                    if (newAllContent)
                    {
                        newAllContent[0] = '\0';
                        if (oldAllContent)
                        {
                            rt_strcpy(newAllContent, oldAllContent);
                        }
                        strcat(newAllContent, contentBuffer);
                        allContent = newAllContent;
                        rt_kprintf("%s", contentBuffer);
                        rt_free(oldAllContent);
                    }
                    else
                    {
                        rt_kprintf("Memory allocation failed, content truncated!\n");
                    }

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
        webclient_close(webSession);

    if (requestRoot)
        cJSON_Delete(requestRoot);

    if (responseRoot)
        cJSON_Delete(responseRoot);

    if (payload)
        cJSON_free(payload);

    return allContent;
}