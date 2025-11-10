/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-11-10     Rbb666       the first version
 */

#include <rtthread.h>
#include <webnet.h>
#include <llm.h>
#include <wn_module.h>

#include "llm_config.h"
#include <string.h>

#define DBG_TAG              "llm_chat"
#ifdef PKG_LLMCHAT_DBG
#define DBG_LVL               DBG_LOG
#else
#define DBG_LVL               DBG_INFO
#endif /* PKG_LLMCHAT_DBG */
#include <rtdbg.h>

static int llm_webnet_init(void);

#define llm_config (*llm_config_get())

static llm_t llm_handle = RT_NULL;

struct llm_stream_context
{
    struct webnet_session *session;
    rt_bool_t has_delta;
};

static void llm_webnet_stream_send_event(struct webnet_session *session,
        const char *event,
        const char *data)
{
    if (session == RT_NULL)
    {
        return;
    }

    if (event && event[0])
    {
        webnet_session_write(session, (const rt_uint8_t *)"event: ", 7);
        webnet_session_write(session, (const rt_uint8_t *)event, strlen(event));
        webnet_session_write(session, (const rt_uint8_t *)"\n", 1);
    }

    if (data == RT_NULL)
    {
        webnet_session_write(session, (const rt_uint8_t *)"data:\n\n", 7);
        return;
    }

    const char *cursor = data;
    while (cursor)
    {
        const char *newline = strchr(cursor, '\n');
        size_t len = newline ? (size_t)(newline - cursor) : strlen(cursor);

        webnet_session_write(session, (const rt_uint8_t *)"data: ", 6);
        if (len > 0)
        {
            webnet_session_write(session, (const rt_uint8_t *)cursor, len);
        }
        webnet_session_write(session, (const rt_uint8_t *)"\n", 1);

        if (newline == RT_NULL)
        {
            break;
        }
        cursor = newline + 1;
    }

    webnet_session_write(session, (const rt_uint8_t *)"\n", 1);
}

static void llm_webnet_stream_on_chunk(const char *chunk, void *user_data)
{
    struct llm_stream_context *context = (struct llm_stream_context *)user_data;

    if (context == RT_NULL || context->session == RT_NULL || chunk == RT_NULL || chunk[0] == '\0')
    {
        return;
    }

    llm_webnet_stream_send_event(context->session, "delta", chunk);
    context->has_delta = RT_TRUE;
}

/** Save configuration to memory - simplified version
 *  Configuration is already in memory, no additional operations needed
 */
static rt_err_t save_config_to_memory(void)
{
    /** Configuration is already in memory, no additional operations needed */
    LOG_D("Configuration updated in memory: API_KEY=%s, MODEL=%s, URL=%s",
          llm_config.api_key, llm_config.model_name, llm_config.api_url);
    return RT_EOK;
}

/** Load configuration from memory - simplified version
 *  Default values are already set during initialization
 */
static rt_err_t load_config_from_memory(void)
{
    /** Default values are already set during initialization */
    LOG_D("Using default configuration from rtconfig.h");
    return RT_EOK;
}

/** CGI handler for getting current configuration
 *  Responds with current LLM configuration in JSON format
 */
static void cgi_get_config_handler(struct webnet_session *session)
{
    const char *mimetype = "application/json";
    cJSON *res_json = RT_NULL;
    char *res_str = RT_NULL;

    LOG_D("=== CGI get_config called ===");

    /** Build configuration response */
    res_json = cJSON_CreateObject();
    if (res_json == RT_NULL)
    {
        LOG_E("Error: cJSON_CreateObject failed");
        goto error;
    }

    cJSON_AddBoolToObject(res_json, "success", cJSON_True);
    cJSON_AddStringToObject(res_json, "apiKey", llm_config.api_key);
    cJSON_AddStringToObject(res_json, "modelName", llm_config.model_name);
    cJSON_AddStringToObject(res_json, "apiUrl", llm_config.api_url);

    res_str = cJSON_PrintUnformatted(res_json);
    if (res_str == RT_NULL)
    {
        LOG_E("Error: cJSON_PrintUnformatted failed");
        cJSON_Delete(res_json);
        goto error;
    }

    LOG_D("Current config sent: API_KEY=%s, MODEL=%s, URL=%s",
          llm_config.api_key, llm_config.model_name, llm_config.api_url);

    session->request->result_code = 200;
    webnet_session_set_header(session, mimetype, 200, "OK", strlen(res_str));
    webnet_session_write(session, (const rt_uint8_t *)res_str, strlen(res_str));

    if (res_str) cJSON_free(res_str);
    if (res_json) cJSON_Delete(res_json);
    LOG_D("=== CGI get_config end ===");
    return;

error:
    const char *error_resp = "{\"success\":false,\"error\":\"Failed to get configuration\"}";
    session->request->result_code = 500;
    webnet_session_set_header(session, mimetype, 500, "Internal Server Error", strlen(error_resp));
    webnet_session_write(session, (const rt_uint8_t *)error_resp, strlen(error_resp));
    LOG_E("=== CGI get_config end (error) ===");
    return;
}

/** CGI handler for updating LLM configuration
 *  Receives JSON configuration and updates the LLM component
 */
static void cgi_config_handler(struct webnet_session *session)
{
    const char *mimetype = "application/json";
    char *post_data = RT_NULL;
    char *original_post_data = RT_NULL;
    cJSON *req_json = RT_NULL, *config_item = RT_NULL;
    cJSON *res_json = RT_NULL;
    char *res_str = RT_NULL;
    rt_size_t post_len = 0;
    rt_err_t result = RT_EOK;

    LOG_D("=== CGI config called ===");
    LOG_D("Session ptr: %p", session);

    if (session == RT_NULL)
    {
        LOG_E("ERROR: Session is NULL");
        return;
    }

    if (session->request == RT_NULL)
    {
        LOG_E("ERROR: session->request is NULL");
        return;
    }

    LOG_D("Method: %d, Content Length: %d",
          session->request->method, session->request->content_length);

    if (session->request->method != WEBNET_POST)
    {
        LOG_E("Error: Not POST method");
        goto error_method;
    }

    post_data = session->request->query;
    original_post_data = post_data;
    post_len = session->request->content_length;

    LOG_D("POST data ptr: %p, length: %d", post_data, post_len);

    if (post_data == RT_NULL || post_len == 0 || post_len > 2048)
    {
        LOG_E("Error: Invalid post_data - ptr:%p len:%d", post_data, post_len);
        goto error;
    }

    LOG_D("POST data preview: '%.*s'", post_data);

    /** Ensure data is null-terminated */
    if (post_data[post_len - 1] != '\0')
    {
        LOG_D("Data not null-terminated, allocating temp buffer");
        char *temp = rt_malloc(post_len + 1);
        if (temp == RT_NULL)
        {
            LOG_E("ERROR: Failed to allocate temp buffer");
            goto error;
        }
        rt_memcpy(temp, post_data, post_len);
        temp[post_len] = '\0';
        post_data = temp;
        LOG_D("Temp buffer created: %s", post_data);
    }

    /** Parse JSON configuration */
    LOG_D("Parsing JSON...");
    req_json = cJSON_Parse(post_data);
    if (req_json == RT_NULL)
    {
        LOG_E("Error: cJSON_Parse failed - invalid JSON");
        goto error;
    }
    LOG_D("JSON parsed successfully");

    /** Extract API key */
    LOG_D("Extracting API key...");
    config_item = cJSON_GetObjectItem(req_json, "apiKey");
    if (config_item && cJSON_IsString(config_item))
    {
        const char *api_key = cJSON_GetStringValue(config_item);
        if (api_key && rt_strlen(api_key) > 0)
        {
            LOG_D("API key found, length: %d", rt_strlen(api_key));
            rt_size_t key_len = rt_strlen(api_key);
            if (key_len >= sizeof(llm_config.api_key))
            {
                LOG_D("WARNING: API key too long, truncating");
                key_len = sizeof(llm_config.api_key) - 1;
            }
            rt_memset(llm_config.api_key, 0, sizeof(llm_config.api_key));
            rt_memcpy(llm_config.api_key, api_key, key_len);
            llm_config.api_key[sizeof(llm_config.api_key) - 1] = '\0';
            LOG_D("API key stored successfully");
        }
        else
        {
            LOG_E("API key is empty or invalid");
        }
    }
    else
    {
        LOG_E("No API key found in request");
    }

    /** Extract model name */
    LOG_D("Extracting model name...");
    config_item = cJSON_GetObjectItem(req_json, "modelName");
    if (config_item && cJSON_IsString(config_item))
    {
        const char *model_name = cJSON_GetStringValue(config_item);
        if (model_name && rt_strlen(model_name) > 0)
        {
            LOG_D("Model name found: %s", model_name);
            rt_size_t name_len = rt_strlen(model_name);
            if (name_len >= sizeof(llm_config.model_name))
            {
                LOG_D("WARNING: Model name too long, truncating");
                name_len = sizeof(llm_config.model_name) - 1;
            }
            rt_memset(llm_config.model_name, 0, sizeof(llm_config.model_name));
            rt_memcpy(llm_config.model_name, model_name, name_len);
            llm_config.model_name[sizeof(llm_config.model_name) - 1] = '\0';
            LOG_D("Model name stored: %s", llm_config.model_name);
        }
    }

    /** Extract API URL */
    LOG_D("Extracting API URL...");
    config_item = cJSON_GetObjectItem(req_json, "apiUrl");
    if (config_item && cJSON_IsString(config_item))
    {
        const char *api_url = cJSON_GetStringValue(config_item);
        if (api_url && rt_strlen(api_url) > 0)
        {
            LOG_D("API URL found: %s", api_url);
            rt_size_t url_len = rt_strlen(api_url);
            if (url_len >= sizeof(llm_config.api_url))
            {
                LOG_D("WARNING: API URL too long, truncating");
                url_len = sizeof(llm_config.api_url) - 1;
            }
            rt_memset(llm_config.api_url, 0, sizeof(llm_config.api_url));
            rt_memcpy(llm_config.api_url, api_url, url_len);
            llm_config.api_url[sizeof(llm_config.api_url) - 1] = '\0';
            LOG_D("API URL stored: %s", llm_config.api_url);
        }
    }

    /** Validate required fields */
    if (rt_strlen(llm_config.api_key) == 0)
    {
        LOG_E("Error: API key is required but empty");
        goto error;
    }

    /** Mark configuration as set */
    llm_config.is_configured = RT_TRUE;
    LOG_D("Configuration marked as configured");

    /** Save configuration to memory */
    LOG_D("Saving config to memory...");
    result = save_config_to_memory();
    if (result != RT_EOK)
    {
        LOG_E("Error: Failed to save config to memory");
        goto error;
    }
    LOG_D("Config saved to memory successfully");

    /** Temporarily skip LLM handle recreation to avoid potential crashes
    *  The new configuration will take effect on next system restart or manual reinit
    *  TODO: Implement safer LLM handle recreation mechanism
    */
    if (llm_handle != RT_NULL)
    {
        LOG_D("LLM handle exists: %p", llm_handle);
        LOG_D("NOTE: LLM handle recreation temporarily disabled for stability");
        LOG_D("New configuration will take effect when LLM is recreated");

        /** Clear existing messages to use fresh conversation */
        if (llm_handle->messages != RT_NULL)
        {
            LOG_D("Clearing existing messages...");
            clear_messages(llm_handle);

            /** Re-add system message with new configuration */
            char system_prompt[512];
            rt_snprintf(system_prompt, sizeof(system_prompt),
                        "You are a helpful AI assistant for AIoT smart terminal on RT-Thread. "
                        "Respond concisely and helpfully. Current model: %s, API: %s",
                        llm_config.model_name, llm_config.api_url);
            add_message2messages(system_prompt, "system", llm_handle);
            LOG_D("Updated system message with new configuration");
        }
    }
    else
    {
        LOG_E("No existing LLM handle");
    }

    /** Build success response */
    LOG_D("Building success response...");
    res_json = cJSON_CreateObject();
    if (res_json == RT_NULL)
    {
        LOG_E("Error: cJSON_CreateObject failed for success response");
        goto error;
    }

    LOG_D("Adding response fields...");
    cJSON_AddBoolToObject(res_json, "success", cJSON_True);
    cJSON_AddStringToObject(res_json, "message", "Configuration updated successfully");

    LOG_D("Serializing response...");
    res_str = cJSON_PrintUnformatted(res_json);
    if (res_str == RT_NULL)
    {
        LOG_E("Error: cJSON_PrintUnformatted failed");
        cJSON_Delete(res_json);
        goto error;
    }

    LOG_D("Sending response: %s", res_str);
    session->request->result_code = 200;
    webnet_session_set_header(session, mimetype, 200, "OK", strlen(res_str));
    webnet_session_write(session, (const rt_uint8_t *)res_str, strlen(res_str));
    LOG_D("Response sent successfully");
    goto cleanup;

error_method:
    LOG_E("Error: Method not POST - building error response");
    res_json = cJSON_CreateObject();
    if (res_json != RT_NULL)
    {
        cJSON_AddBoolToObject(res_json, "success", cJSON_False);
        cJSON_AddStringToObject(res_json, "error", "Method not allowed (use POST)");
        res_str = cJSON_PrintUnformatted(res_json);
        if (res_str != RT_NULL)
        {
            session->request->result_code = 405;
            webnet_session_set_header(session, mimetype, 405, "Method Not Allowed", strlen(res_str));
            webnet_session_write(session, (const rt_uint8_t *)res_str, strlen(res_str));
            cJSON_free(res_str);
            LOG_E("Method error response sent");
        }
        cJSON_Delete(res_json);
    }
    goto cleanup;

error:
    LOG_E("Error: Config update failed - building error response");
    res_json = cJSON_CreateObject();
    if (res_json != RT_NULL)
    {
        cJSON_AddBoolToObject(res_json, "success", cJSON_False);
        cJSON_AddStringToObject(res_json, "error", "Invalid config data or update failed");
        res_str = cJSON_PrintUnformatted(res_json);
        if (res_str != RT_NULL)
        {
            session->request->result_code = 400;
            webnet_session_set_header(session, mimetype, 400, "Bad Request", strlen(res_str));
            webnet_session_write(session, (const rt_uint8_t *)res_str, strlen(res_str));
            cJSON_free(res_str);
            LOG_E("Error response sent");
        }
        cJSON_Delete(res_json);
    }
    goto cleanup;

cleanup:
    LOG_D("Cleaning up resources...");
    if (res_str && res_str != original_post_data)
    {
        cJSON_free(res_str);
    }
    if (res_json)
    {
        cJSON_Delete(res_json);
    }
    if (req_json)
    {
        cJSON_Delete(req_json);
    }
    if (post_data != original_post_data && post_data != RT_NULL)
    {
        rt_free(post_data);
    }
    LOG_D("=== CGI config end ===");
    return;
}

/** Initialize LLM WebNet interface
 *  Creates LLM handle with current configuration
 *  @return 0 on success, -1 on failure
 */
static int llm_webnet_init(void)
{
    char system_prompt[512];

    LOG_D("=== llm_webnet_init called ===");
    LOG_D("Current llm_handle: %p", llm_handle);

    if (llm_handle != RT_NULL)
    {
        LOG_D("LLM handle already exists, skipping initialization");
        return 0;
    }

    /** Load default configuration (already set during initialization) */
    LOG_D("Loading default config from memory...");
    load_config_from_memory();

    /** Print current configuration */
    LOG_D("Current configuration:");
    LOG_D("  API Key: %s", llm_config.api_key);
    LOG_D("  Model: %s", llm_config.model_name);
    LOG_D("  URL: %s", llm_config.api_url);
    LOG_D("  Configured: %s", llm_config.is_configured ? "YES" : "NO");

    LOG_D("Creating LLM handle...");

    llm_handle = create_llm_t();
    if (llm_handle == RT_NULL)
    {
        LOG_E("ERROR: Failed to create LLM handle for WebNet!");
        return -1;
    }
    LOG_D("LLM handle created successfully: %p", llm_handle);

    llm_handle->messages = cJSON_CreateArray();
    if (llm_handle->messages == RT_NULL)
    {
        LOG_E("ERROR: Failed to create messages array!");
        LOG_D("Cleaning up LLM handle...");
        delete_llm_t(llm_handle);
        llm_handle = RT_NULL;
        return -1;
    }
    LOG_D("Messages array created successfully: %p", llm_handle->messages);

    llm_handle->get_answer = get_llm_answer;

    /** Build system prompt with configuration information */
    rt_snprintf(system_prompt, sizeof(system_prompt),
                "You are a helpful AI assistant for AIoT smart terminal on RT-Thread. "
                "Respond concisely and helpfully. Current model: %s, API: %s",
                llm_config.model_name, llm_config.api_url);
    LOG_D("System prompt: %s", system_prompt);

    add_message2messages(system_prompt, "system", llm_handle);

    LOG_D("=== LLM WebNet initialization completed successfully ===");
    LOG_D("Final state:");
    LOG_D("  LLM Handle: %p", llm_handle);
    LOG_D("  Messages Array: %p", llm_handle ? llm_handle->messages : RT_NULL);
    LOG_D("  API_KEY: %s", llm_config.api_key);
    LOG_D("  MODEL: %s", llm_config.model_name);
    LOG_D("  URL: %s", llm_config.api_url);
    return 0;
}

static void cgi_chat_handler(struct webnet_session *session)
{
    const char *mimetype = "application/json";
    char *post_data = RT_NULL;
    char *original_post_data = RT_NULL;
    cJSON *req_json = RT_NULL, *msg_item = RT_NULL;
    char *user_msg = RT_NULL, *ai_reply = RT_NULL;
    cJSON *res_json = RT_NULL;
    char *res_str = RT_NULL;
    rt_size_t post_len = 0;
    rt_bool_t stream_mode = RT_FALSE;
    struct llm_stream_context stream_ctx = { RT_NULL, RT_FALSE };

    LOG_D("=== CGI chat called: method=%d, content_len=%d ===", session->request->method, session->request->content_length);

    if (session->request->method != WEBNET_POST)
    {
        LOG_E("Error: Not POST");
        goto error_method;
    }

    post_data = session->request->query;
    original_post_data = post_data;
    post_len = session->request->content_length;
    if (post_data == RT_NULL || post_len == 0 || post_len > 4096)
    {
        LOG_E("Error: No/invalid post_data (len=%d)", post_len);
        goto error;
    }

    if (post_data[post_len - 1] != '\0')
    {
        char *temp = rt_strdup(post_data);
        if (temp)
        {
            temp[post_len] = '\0';
            post_data = temp;
        }
    }

    LOG_D("POST data (parsed): '%s' (len=%d)", post_data, post_len);

    /* Parse JSON payload: { "message": "..." } or { "reset": true } */
    req_json = cJSON_Parse(post_data);
    if (req_json == RT_NULL)
    {
        LOG_E("Error: cJSON_Parse failed on: %s", post_data);
        goto error;
    }

    msg_item = cJSON_GetObjectItem(req_json, "stream");
    if (msg_item)
    {
        if (cJSON_IsBool(msg_item))
        {
            stream_mode = cJSON_IsTrue(msg_item);
        }
        else if (cJSON_IsNumber(msg_item))
        {
            stream_mode = (msg_item->valuedouble != 0);
        }
        else if (cJSON_IsString(msg_item))
        {
            const char *stream_val = cJSON_GetStringValue(msg_item);
            if (stream_val &&
                    (strcmp(stream_val, "true") == 0 || strcmp(stream_val, "1") == 0 || strcmp(stream_val, "TRUE") == 0))
            {
                stream_mode = RT_TRUE;
            }
        }
    }

    /* Check whether this is a reset request */
    msg_item = cJSON_GetObjectItem(req_json, "reset");
    if (msg_item && cJSON_IsTrue(msg_item))
    {
        LOG_D("Reset request received");
        /* Clear chat history and rebuild the system message */
        clear_messages(llm_handle);
        add_message2messages("You are a helpful AI assistant for AIoT smart terminal on RT-Thread. Respond concisely and helpfully.", "system", llm_handle);

        /* Build the success response payload */
        res_json = cJSON_CreateObject();
        if (res_json == RT_NULL)
        {
            LOG_E("Error: cJSON_CreateObject failed for reset response");
            goto error;
        }
        cJSON_AddBoolToObject(res_json, "success", cJSON_True);
        cJSON_AddStringToObject(res_json, "response", "Conversation reset successfully");
        res_str = cJSON_PrintUnformatted(res_json);
        if (res_str == RT_NULL)
        {
            LOG_E("Error: cJSON_PrintUnformatted failed for reset response");
            cJSON_Delete(res_json);
            goto error;
        }

        LOG_D("Reset response sent: %s", res_str);
        session->request->result_code = 200;
        webnet_session_set_header(session, mimetype, 200, "OK", strlen(res_str));
        webnet_session_write(session, (const rt_uint8_t *)res_str, strlen(res_str));
        goto cleanup;
    }

    /* Handle a standard chat message request */
    msg_item = cJSON_GetObjectItem(req_json, "message");
    if (msg_item == RT_NULL || !cJSON_IsString(msg_item))
    {
        LOG_E("Error: No 'message' field in JSON");
        goto error;
    }
    user_msg = cJSON_GetStringValue(msg_item);

    if (user_msg == RT_NULL || strlen(user_msg) == 0)
    {
        LOG_E("Error: Empty user_msg");
        goto error;
    }

    if (llm_handle == RT_NULL)
    {
        LOG_E("Error: LLM handle is NULL");
        goto error;
    }

    add_message2messages(user_msg, "user", llm_handle);

    if (stream_mode)
    {
        LOG_D("Streaming mode enabled for this request");

        stream_ctx.session = session;

        session->request->result_code = 200;
        webnet_session_set_header(session, "text/event-stream", 200, "OK", -1);
        webnet_session_write(session, (const rt_uint8_t *)":rt-thread-stream\n\n", strlen(":rt-thread-stream\n\n"));

        llm_handle->stream_cb = llm_webnet_stream_on_chunk;
        llm_handle->stream_user_data = &stream_ctx;

        LOG_D("Calling get_llm_answer (streaming)...");
        ai_reply = llm_handle->get_answer(llm_handle, llm_handle->messages);

        llm_handle->stream_cb = RT_NULL;
        llm_handle->stream_user_data = RT_NULL;

        if (ai_reply == RT_NULL)
        {
            LOG_E("Error: streaming get_llm_answer returned NULL (check API/net)");
            ai_reply = rt_strdup("Mock reply: Success! WebNet CGI + LLM integrated. (Local llm works; if real API fails, check key/net.)");
        }

        if (ai_reply != RT_NULL)
        {
            if (stream_ctx.has_delta == RT_FALSE)
            {
                llm_webnet_stream_send_event(session, "delta", ai_reply);
            }
            add_message2messages(ai_reply, "assistant", llm_handle);
            llm_webnet_stream_send_event(session, "final", ai_reply);
        }
        else
        {
            llm_webnet_stream_send_event(session, "error", "LLM response failed");
        }

        llm_webnet_stream_send_event(session, "done", "[DONE]");
        goto cleanup;
    }

    LOG_D("Calling get_llm_answer...");
    ai_reply = llm_handle->get_answer(llm_handle, llm_handle->messages);
    if (ai_reply == RT_NULL)
    {
        LOG_E("Error: get_llm_answer returned NULL (check API/net in CGI context)");
        ai_reply = rt_strdup("Mock reply: Success! WebNet CGI + LLM integrated. (Local llm works; if real API fails, check key/net.) Your msg: Hello!");
    }

    /* Append assistant reply to preserve message history */
    add_message2messages(ai_reply, "assistant", llm_handle);

    /* Build JSON response { "success": true, "response": "..." } */
    res_json = cJSON_CreateObject();
    if (res_json == RT_NULL)
    {
        LOG_E("Error: cJSON_CreateObject failed");
        goto error;
    }
    cJSON_AddBoolToObject(res_json, "success", cJSON_True);
    cJSON_AddStringToObject(res_json, "response", ai_reply);
    res_str = cJSON_PrintUnformatted(res_json);
    if (res_str == RT_NULL)
    {
        LOG_E("Error: cJSON_PrintUnformatted failed");
        goto error;
    }

    LOG_D("Sending response: %s", res_str);

    session->request->result_code = 200;
    webnet_session_set_header(session, mimetype, 200, "OK", strlen(res_str));
    webnet_session_write(session, (const rt_uint8_t *)res_str, strlen(res_str));

    goto cleanup;

error_method:
    LOG_E("Error: Method not POST");

    res_json = cJSON_CreateObject();
    if (res_json != RT_NULL)
    {
        cJSON_AddBoolToObject(res_json, "success", cJSON_False);
        cJSON_AddStringToObject(res_json, "error", "Method not allowed (use POST)");
        res_str = cJSON_PrintUnformatted(res_json);
        if (res_str != RT_NULL)
        {
            session->request->result_code = 405;
            webnet_session_set_header(session, mimetype, 405, "Method Not Allowed", strlen(res_str));
            webnet_session_write(session, (const rt_uint8_t *)res_str, strlen(res_str));
            cJSON_free(res_str);
        }
        cJSON_Delete(res_json);
    }
    else
    {
        const char *error_resp = "{\"success\":false,\"error\":\"Method not allowed\"}";
        session->request->result_code = 405;
        webnet_session_set_header(session, mimetype, 405, "Method Not Allowed", strlen(error_resp));
        webnet_session_write(session, (const rt_uint8_t *)error_resp, strlen(error_resp));
    }
    goto cleanup;

error:
    LOG_E("Error: General failure");

    res_json = cJSON_CreateObject();
    if (res_json != RT_NULL)
    {
        cJSON_AddBoolToObject(res_json, "success", cJSON_False);
        cJSON_AddStringToObject(res_json, "error", "Invalid request or LLM error");
        res_str = cJSON_PrintUnformatted(res_json);
        if (res_str != RT_NULL)
        {
            session->request->result_code = 400;
            webnet_session_set_header(session, mimetype, 400, "Bad Request", strlen(res_str));
            webnet_session_write(session, (const rt_uint8_t *)res_str, strlen(res_str));
            cJSON_free(res_str);
        }
        cJSON_Delete(res_json);
    }
    else
    {
        const char *error_resp = "{\"success\":false,\"error\":\"Internal server error\"}";
        session->request->result_code = 500;
        webnet_session_set_header(session, mimetype, 500, "Internal Server Error", strlen(error_resp));
        webnet_session_write(session, (const rt_uint8_t *)error_resp, strlen(error_resp));
    }

cleanup:
    if (ai_reply && ai_reply != original_post_data) rt_free(ai_reply);
    if (res_str && res_str != original_post_data) cJSON_free(res_str);
    if (res_json) cJSON_Delete(res_json);
    if (req_json) cJSON_Delete(req_json);
    /* Free post_data only when we allocated a new buffer */
    if (post_data != original_post_data && post_data != RT_NULL)
    {
        rt_free(post_data);
    }
    LOG_D("=== CGI chat end ===");
    return;
}

/* Command to display current LLM configuration */
static void show_llm_config(void)
{
    LOG_I("=== Current LLM Configuration ===");
    LOG_I("API Key: %s", llm_config.api_key[0] ? llm_config.api_key : "NOT SET");
    LOG_I("Model Name: %s", llm_config.model_name[0] ? llm_config.model_name : "NOT SET");
    LOG_I("API URL: %s", llm_config.api_url[0] ? llm_config.api_url : "NOT SET");
    LOG_I("Is Configured: %s", llm_config.is_configured ? "YES" : "NO");
    LOG_I("LLM Handle: %s", llm_handle ? "CREATED" : "NULL");
    LOG_I("================================");
}
MSH_CMD_EXPORT(show_llm_config, Show current LLM configuration);

/* Start webnet llm chat */
void webnet_llm_mode(void)
{
    if (llm_webnet_init() != 0)
    {
        LOG_E("LLM init failed, skipping WebNet init.");
        return;
    }

    webnet_cgi_register("chat", cgi_chat_handler);
    webnet_cgi_register("config", cgi_config_handler);
    webnet_cgi_register("get_config", cgi_get_config_handler);

    webnet_init();
    show_llm_config();
}
MSH_CMD_EXPORT(webnet_llm_mode, Start webnet llm chat);
