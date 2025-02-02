
/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025/02/01     Rbb666       Add license info
 */
#include "llm.h"

#include <webclient.h>
#include <cJSON.h>

#define API_KEY PKG_LLM_DOUBAO_API_KEY
#define MODEL_EP PKG_LLM_DOUBAO_MODEL_ID
#define API_URL PKG_LLM_DOUBAO_API_URL

#define WEB_SORKET_BUFSZ 2048

char *doubao_llm_answer(const char *input_text)
{
    size_t resp_len = 0;
    char auth_header[128];
    struct webclient_session *session = RT_NULL;
    char *buffer = RT_NULL, *header = RT_NULL;
    char *response = RT_NULL, *result = RT_NULL;
    cJSON *response_root = NULL;

    buffer = (char *)web_malloc(WEB_SORKET_BUFSZ);
    if (buffer == RT_NULL)
    {
        rt_kprintf("No memory for receive response buffer.\n");
        goto __exit;
    }

    session = webclient_session_create(WEB_SORKET_BUFSZ);
    if (session == RT_NULL)
    {
        rt_kprintf("Failed to create webclient session.\n");
        goto __exit;
    }

    webclient_set_timeout(session, 5000);

    rt_snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n", API_KEY);

    webclient_request_header_add(&header, "Content-Type: application/json\r\n");
    webclient_request_header_add(&header, auth_header);

    cJSON *root = cJSON_CreateObject();
    cJSON *model = cJSON_CreateString(MODEL_EP);
    cJSON *messages = cJSON_CreateArray();
    cJSON *system_message = cJSON_CreateObject();
    cJSON *user_message = cJSON_CreateObject();

    cJSON_AddItemToObject(root, "model", model);
    cJSON_AddItemToObject(root, "messages", messages);

    cJSON_AddItemToArray(messages, system_message);
    cJSON_AddItemToArray(messages, user_message);

    cJSON_AddStringToObject(system_message, "role", "system");
    cJSON_AddStringToObject(system_message, "content", "要求下面的回答严格控制在256字符以内");

    cJSON_AddStringToObject(user_message, "role", "user");
    cJSON_AddStringToObject(user_message, "content", input_text);

    char *payload = cJSON_PrintUnformatted(root);
    if (webclient_request(API_URL, header, (const char *)payload, rt_strlen(payload), (void **)&response, &resp_len) < 0)
    {
        rt_kprintf("Webclient send post request failed.\n");
        goto __exit;
    }

    response_root = cJSON_Parse(response);
    if (response_root == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            rt_kprintf("Error before: %s\n", error_ptr);
        }
        goto __exit;
    }

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(response_root, "choices");
    if (cJSON_IsArray(choices))
    {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        if (first_choice != NULL)
        {
            cJSON *message = cJSON_GetObjectItemCaseSensitive(first_choice, "message");
            cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
            if (cJSON_IsString(content) && content->valuestring != NULL)
            {
                result = rt_strdup(content->valuestring);
            }
        }
    }

__exit:
    if (session)
    {
        webclient_close(session);
        session = RT_NULL;
    }

    if (header)
        web_free(header);

    if (response)
        web_free(response);

    if (buffer)
        web_free(buffer);

    if (root)
        cJSON_Delete(root);

    if (response_root)
        cJSON_Delete(response_root);

    return result;
}
